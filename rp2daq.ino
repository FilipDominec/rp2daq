#include <Adafruit_TinyUSB.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
/*
TODO for 210330:
	use structure for msg analysis
	init PWM by computer
	init stepper by computer
	etc etc: full program-defined GPIO function
	main cycle - hook on ISR

	check duty cycle of loop code sections



	
 */

// == APPROACH AND SCANNING CONTROL == {{{

uint8_t approach_active = 0;
uint8_t approach_stepper_number;    
int32_t approach_motor_targetpos;   
int32_t approach_motor_speed;       
int32_t approach_motor_cycle_steps; 
int16_t approach_piezo_speed;       

int16_t scan_x_start = 0;       
int16_t scan_x_end = 0;       
int16_t scan_x_speed = 0;       
int16_t scan_x_sampling_step = 100;       
int16_t scan_y_position = 0;       

uint8_t piezo_feedback_active = 0;
uint8_t piezo_scan_active = 0;

//#define SUBSAMPLE_BITS    8		//  TODO this is piezo control should allow for slower scanning than 1 LSB per cycle
#define ADC_THRESHOLD    1000	// feedback from the 12bit built-in ADC: full value of 4095 corresponds to ca. 3.3 V
/*}}}*/
// == HARDWARE CONTROL PINS AND VARIABLES == {{{

// Following code allows for flexible control of up to 256 stepper motors. Just add a new column to the following arrays. 
// Note: using "nanopos" and "target_nanospeed" are essential for stepping speeds less than one microstep per cycle.

#define MAX_STEPPER_COUNT 16
                                       //  motor1  ...
uint8_t stepper_dir_pin[MAX_STEPPER_COUNT]     ;
uint8_t stepper_step_pin[MAX_STEPPER_COUNT]    ;
uint8_t stepper_lowstop_pin[MAX_STEPPER_COUNT] ;
uint8_t stepper_disable_pin[MAX_STEPPER_COUNT] ; // with Protoneer's CNC board: set to 12 for all motors 

#define NANOPOS_AT_ENDSTOP  (uint32_t)(1<<31)
#define NANOSTEP_PER_MICROSTEP  256  // maximum main loop cycles per step (higher value enables finer control of speed, but smaller range)
uint8_t endstop_override[MAX_STEPPER_COUNT];	// e.g. for moving out of endstop position
uint8_t endstop_flag[MAX_STEPPER_COUNT];	// remember the end stop event occured
uint32_t nanopos[MAX_STEPPER_COUNT];	// current motor position 
uint32_t target_nanopos[MAX_STEPPER_COUNT]; // set from the computer; (zero usually corresponds to the lower end switch)
uint32_t target_nanospeed[MAX_STEPPER_COUNT]  ; // set from the computer; always ensure that 0 < target_nanospeed < NANOSTEP_PER_MICROSTEP 
uint32_t motor_inertia_coef[MAX_STEPPER_COUNT];		 // smooth ramp-up and ramp-down of motor speed 

uint32_t initial_nanopos[MAX_STEPPER_COUNT]   = {0}; // used for smooth speed control
//uint8_t auto_motor_turnoff[MAX_STEPPER_COUNT] = {0}; // if set to 1, the motor "disable" pin will be set when not moving
//uint8_t was_at_end_switch[MAX_STEPPER_COUNT] = {0};

#define DAC_CLOCK     5		// common signals to digital-analog converters (DAC)
#define DAC_LATCHEN  18
#define DAC_DATA0    19		// synchronous data outputs to four DACs
#define DAC_DATA1    21
#define DAC_DATA2    14
#define DAC_DATA3    27

#define NDAC_BCK       4      // testing the preferred dual 16-bit TDA1543
#define NDAC_WS		   12
#define NDAC_DATA01    26		// first 2 channels
#define NDAC_DATA23    25		// next 2 channels

int16_t x = 0;		// Cartesian coords will be linearly combined for the four-quadrant piezo transducer
int16_t y = 0;
int16_t z = 0;

// Piezo controlling pseudo-constants
#define ADC_PIN          4				// input from the ADC
/*}}}*/




// == COMPUTER COMMUNICATION: generic hardware-control messages == 

typedef uint8_t  B;  // visual compatibility with python's struct module
typedef uint16_t H;
typedef uint32_t I;
typedef int32_t  i;

#define IN_BUF_LEN 40
// TODO TODO  test volatile!
uint8_t in_buf[IN_BUF_LEN];     // input message buffer, first byte always indicates the type of message (and its expected length)
uint8_t in_buf_ptr = 0;         // pointer behind the last received byte

//#define MESSAGE_TIMEOUT_LIMIT 100 // TODO implement fail-safe 
//uint8_t in_buf_timeout = 0; // erase the input buffer if more than MESSAGE_TIMEOUT_LIMIT main loop cycles pass without receiving next byte 

#define OUT_BUF_LEN 4000
uint8_t out_buf[OUT_BUF_LEN];		// output message buffer
int16_t out_buf_ptr = 0;			// pointer to buffer end

uint8_t scan_data[OUT_BUF_LEN];   // raw data recorded by the scanning routine
int16_t scan_data_ptr = 0;



#define CMD_IDENTIFY  123		// blink LED, return "espdaq" + custom 24B identifier
struct cmd_identify_struct {  
    B message_type;      // always byte 0 
} __attribute__((packed));     // (prevent byte padding of uint8 and uint16 to four bytes)

#define CMD_STEPPER_GO  1		// set the stepper motor to move to a given position
struct cmd_stepper_go_struct  {  
    B message_type;      // always byte 0 
    B stepper_number;    // at byte 1
    I motor_targetpos;   // at byte 2
    I motor_speed;       // at byte 6
    B endstop_override;    // at byte 10
    B reset_zero_pos;
} __attribute__((packed));     

#define CMD_APPROACH 2		// safely approach the sample to the STM tip, using both stepper and piezo{{{
struct cmd_approach_struct  {  
    uint8_t message_type;                
    uint8_t stepper_number;    // at byte 1
    int32_t motor_targetpos;   // at byte 2
    int32_t motor_speed;       // at byte 6
    int32_t motor_cycle_steps; // at byte 10
    int16_t piezo_speed;       // at byte 14
} __attribute__((packed)); 
#define CMD_GET_STEPPER_STATUS 3		// just report the current nanopos and status
struct cmd_get_stepper_status_struct  {  
    uint8_t message_type;                
    uint8_t stepper_number;    // at byte 1
} __attribute__((packed)); 
#define CMD_GET_STM_STATUS 4		// just report the current nanopos and status
struct cmd_get_stm_status_struct  {  
    uint8_t message_type;                
    // todo
} __attribute__((packed)); 
#define CMD_INIT_STEPPER  5		// set the stepper motor to move to a given position
struct cmd_init_stepper_struct  {  
    B message_type;      // always byte 0 
    B stepper_number;    
    B dir_pin;    
    B step_pin;    
    B endswitch_pin;    
    B disable_pin;    
    I motor_inertia;    
} __attribute__((packed));     

#define CMD_SET_PIEZO  9		// set a concrete position on the piezo
struct cmd_set_piezo_struct  {  
    uint8_t message_type;      // always byte 0
    int16_t piezo_x;			   // at byte 1			(note that full uint32 range may not be used by the dac)
    int16_t piezo_y;			   // at byte 5			
    int16_t piezo_z;			   // at byte 9		
} __attribute__((packed));
#define CMD_LINESCAN 10
struct cmd_linescan_struct  {  // todo
    uint8_t message_type;                
    int16_t scan_x_start;			   // at byte  1
    int16_t scan_x_end;                // at byte  5
    int16_t scan_x_speed;              // at byte  9
    int16_t scan_x_sampling_step;      // at byte 13
    int16_t scan_y_position;           // at byte 17
} __attribute__((packed)); 

/*}}}*/

#define CMD_SET_PWM 20
struct __attribute__((packed)) cmd_set_PWM_struct {
    B message_type; 
    B channel; 
    I value; 
};



#define CMD_INIT_PWM 21
struct __attribute__((packed)) cmd_init_PWM_struct  {  
    B message_type;     
    B assign_channel;	
    B assign_pin;		
    B bit_resolution;	
    I freq_Hz;			
    I initial_value;	
};

#define CMD_GET_ADC 22
struct __attribute__((packed)) cmd_get_ADC_struct  {  
    B message_type;     
    B adc_pin;	
    B oversampling_count;	
};



void set_PWM(cmd_set_PWM_struct S)
{
	B m = 3;
    if (m<MAX_STEPPER_COUNT) { initial_nanopos[m]      = nanopos[m];  } // XXX
};

void init_PWM(cmd_init_PWM_struct S)
{
	B m = 3;
    if (m<MAX_STEPPER_COUNT) { initial_nanopos[m]      = nanopos[m];  } // XXX
};


void get_ADC(cmd_get_ADC_struct S)
{
 // TODO
};


void stepper_go(cmd_stepper_go_struct S)  // set the stepper motor to start moving to a given (nano)position
{
    uint8_t m = S.stepper_number;
    if (m<MAX_STEPPER_COUNT) {
        initial_nanopos[m]      = nanopos[m]; // remember the starting position (for speed control)
        target_nanopos[m]       = S.motor_targetpos;  
        target_nanospeed[m]     = S.motor_speed;  //  note: for STM, set this:  approach_active  = 0;
        endstop_override[m]     = S.endstop_override; 
        if (S.reset_zero_pos) {initial_nanopos[m] = NANOPOS_AT_ENDSTOP; nanopos[m] = NANOPOS_AT_ENDSTOP;};    
        endstop_flag[m]         = 0;   // flag only resets upon a new stepper_go message
    }
};


void init_stepper(cmd_init_stepper_struct S) {
       uint8_t m = S.stepper_number;

       if (m<MAX_STEPPER_COUNT) {
               stepper_dir_pin[m]     = S.dir_pin;
               stepper_step_pin[m]    = S.step_pin;
               stepper_lowstop_pin[m] = S.endswitch_pin;
               stepper_disable_pin[m] = S.disable_pin;
               endstop_override[m]    = 0; 
               motor_inertia_coef[m]  = S.motor_inertia;
               if (motor_inertia_coef[m] == 0) { motor_inertia_coef[m]  = 1;} // prevents div by 0 error
               endstop_flag[m]        = 0;
               pinMode(stepper_dir_pin[m],  OUTPUT);
               pinMode(stepper_step_pin[m],  OUTPUT);
               if (stepper_lowstop_pin[m]) pinMode(stepper_lowstop_pin[m],  INPUT_PULLUP);
               if (stepper_disable_pin[m]) pinMode(stepper_disable_pin[m],  OUTPUT);

               if (nanopos[m] == 0) {
    // default position (nonzero to allow going both directions, signed int are not used as their division truncate toward zero)
                       nanopos[m] = NANOPOS_AT_ENDSTOP; 
                       target_nanopos[m] = nanopos[m];  // motor stands still when (re)defined
               }
       }       
};



#define DACBITS 16						// dac bit depth{{{
#define bitmask		(1<<(DACBITS-1))
#define dac_value_shift (128*256)
//(1<<(DACBITS-1)) FIXME
#define dummyb_max	7
#define bit_max		DACBITS
void set_piezo_raw(int16_t dac_word0, int16_t dac_word1, int16_t dac_word2, int16_t dac_word3) {
    // Communication with 2x TDA1543 dual 16-bit DACs, using I2S protocol implemented in firmware. This takes 20-25 us.
    //
    // Note 0: Here we are not using the built-in I2S interface of ESP32 since we want to supply data to multiple DACs at once.
    // Note 1: By much trial and error, I learned that the TDA1543 chips require the most significant bit 
    //   extending to exactly (!) 7+1 leading bits of the signal. In fact we are loading 23 bits that contain the 16bit 
    //   number at their end. Otherwise the input data got truncated.
    // Note 2: The digitalWrite routine takes ca. 60 nanoseconds by default, which is just enough for the fastest 
    //   timing delays from the TDA1543 datasheet. Thanks to digitalWrite() being rather inefficient, no waiting routines 
    //   are needed, unless one overclocks the ESP32 or uses direct port access.
    // Note 3: From the nature of the I2S protocol, the DAC channels 0 and 1 might be expected to be out-of-sync if 
    //   if the Left and Right channels are set up incorrectly (by a single 100 us cycle). Currently the actual output 
    //   signal of both DAC channels looks to be perfectly in sync anyway.

    digitalWrite(NDAC_WS, HIGH);
    digitalWrite(NDAC_BCK, LOW);
    for (uint8_t bit=0; bit<bit_max; bit++) {
        if (dac_word0 & bitmask) {digitalWrite(NDAC_DATA01, HIGH);} else {digitalWrite(NDAC_DATA01, LOW);};  dac_word0 = dac_word0 << 1; 
        if (dac_word2 & bitmask) {digitalWrite(NDAC_DATA23, HIGH);} else {digitalWrite(NDAC_DATA23, LOW);};  dac_word2 = dac_word2 << 1;
        if (bit==0) {
            for (uint8_t dummyb=0; dummyb<dummyb_max; dummyb++) {
                digitalWrite(NDAC_BCK, HIGH); // the digitalWrite routine takes ca. 60 ns in default ESP32 - this is crucial for timing
                digitalWrite(NDAC_BCK, LOW);
            }
        }
        digitalWrite(NDAC_BCK, HIGH);
        digitalWrite(NDAC_BCK, LOW);
        if (bit==(bit_max-2)) { digitalWrite(NDAC_WS, LOW);	}	
    }
    for (uint8_t bit=0; bit<bit_max; bit++) {
        if (dac_word1 & bitmask) {digitalWrite(NDAC_DATA01, HIGH);} else {digitalWrite(NDAC_DATA01, LOW);};  dac_word1 = dac_word1 << 1;
        if (dac_word3 & bitmask) {digitalWrite(NDAC_DATA23, HIGH);} else {digitalWrite(NDAC_DATA23, LOW);};  dac_word3 = dac_word3 << 1;
        if (bit==0) {
            for (uint8_t dummyb=0; dummyb<dummyb_max; dummyb++) {
                digitalWrite(NDAC_BCK, HIGH);
                digitalWrite(NDAC_BCK, LOW);
            }
        }
        digitalWrite(NDAC_BCK, HIGH);
        digitalWrite(NDAC_BCK, LOW);
        if (bit==(bit_max-2)) { digitalWrite(NDAC_WS, HIGH);	}	
    }
}

#define HARD_LIMIT_LOW  -(1<<(DACBITS-1))						// "Hard" limits prevent DAC over- and under- flows. Should never be exceeded.
#define HARD_LIMIT_HIGH (1<<(DACBITS-1))	
#define HARD_LIMIT_CENTRE (HARD_LIMIT_LOW+HARD_LIMIT_HIGH)/2
#define HARD_LIMIT_EXTENT (HARD_LIMIT_HIGH-HARD_LIMIT_LOW)
#define SOFT_LIMIT_LOW    (HARD_LIMIT_CENTRE-HARD_LIMIT_EXTENT/6+1)  // "Soft" limits prevent STM range clipping during Cartesian-quadrant conv.
#define SOFT_LIMIT_HIGH   (HARD_LIMIT_CENTRE+HARD_LIMIT_EXTENT/6-1)  // Note: if e.g. x=y=0, the z coord can go up to hard limits without trouble
//void set_piezo(int16_t target_x, int16_t target_y, int16_t target_z) {
//set_piezo_slow(target_x, target_y, target_z, 0);
//}

#define max(a,b)  ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a > _b ? _a : _b; })
#define min(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a < _b ? _a : _b; })
void set_piezo_slow(int16_t target_x, int16_t target_y, int16_t target_z, int16_t speed_limit) { 
    while ((x!=target_x) || (y!=target_y) || (z!= target_z)) { 
        if (speed_limit == 0) { 
            x=target_x; y=target_y; z= target_z; 
        } else {
            if (x > target_x) {x = max(target_x, x-speed_limit);}
            if (x < target_x) {x = min(target_x, x+speed_limit);}

            if (y > target_y) {y = max(target_y, y-speed_limit);}
            if (y < target_y) {y = min(target_y, y+speed_limit);}

            if (z > target_z) {z = max(target_z, z-speed_limit);}
            if (z < target_z) {z = min(target_z, z+speed_limit);}
            sleep_us(100); 
        }

        set_piezo_raw(+ x + y + z,
                - x + y + z,
                - x - y + z,
                + x - y + z); 
    }				 
}
/*}}}*/
void transmit_out_buf(uint32_t total_bytes) {
    for (out_buf_ptr=0; out_buf_ptr<total_bytes; out_buf_ptr++) { Serial.write(out_buf[out_buf_ptr]); }
}

void process_messages() {
    if ((in_buf[0] == CMD_IDENTIFY) && (in_buf_ptr == sizeof(cmd_identify_struct))) {
        uint8_t byteArray[6] = {'r','p','2','d','a','q'}; memcpy(out_buf, byteArray, 6);
        uint8_t byteBrray[6] = {'2','1','0','8','2','9'}; memcpy(out_buf+6, byteBrray, 6);
		pico_get_unique_board_id((pico_unique_board_id_t*)(out_buf+12));
        transmit_out_buf(30);
        in_buf_ptr = 0;
    } else if ((in_buf[0] == CMD_STEPPER_GO) && (in_buf_ptr == sizeof(cmd_stepper_go_struct))) {
        //digitalWrite(LED_BUILTIN, HIGH); sleep_us(1); digitalWrite(LED_BUILTIN, LOW); 
        stepper_go(*((cmd_stepper_go_struct*)(in_buf)));
        in_buf_ptr = 0;
    } else if ((in_buf[0] == CMD_APPROACH) && (in_buf_ptr == sizeof(cmd_approach_struct))) {
        set_piezo_slow(0,0,SOFT_LIMIT_LOW, 100); // fast retract piezo to centre top 

        approach_stepper_number     = *((uint8_t*)(in_buf+ 1)); // at byte 1
        approach_motor_targetpos    = *((int32_t*)(in_buf+ 2)); // at byte 2
        approach_motor_speed        = *((int32_t*)(in_buf+ 6)); // at byte 6
        approach_motor_cycle_steps  = *((int32_t*)(in_buf+10)); // at byte 10
        approach_piezo_speed        = *((int16_t*)(in_buf+14)); // at byte 14

        piezo_feedback_active = 0;
        piezo_scan_active = 0;
        approach_active  = 1;

        in_buf_ptr = 0;

    } else if ((in_buf[0] == CMD_INIT_STEPPER) && (in_buf_ptr == sizeof(cmd_init_stepper_struct))) {
        uint8_t m = in_buf[1];

        if (m<MAX_STEPPER_COUNT) {
            init_stepper(*((cmd_init_stepper_struct*)(in_buf)));
        }	
        in_buf_ptr = 0;
    } else if ((in_buf[0] == CMD_GET_STEPPER_STATUS) && (in_buf_ptr == sizeof(cmd_get_stepper_status_struct))) {
        uint8_t m = in_buf[1];
        if (m<MAX_STEPPER_COUNT) {
            if (nanopos[m] == target_nanopos[m]) {out_buf[0] = 0;} else {out_buf[0] = 1;}; 
            out_buf[1] = endstop_flag[m];
            memcpy(out_buf+2, &nanopos[m], sizeof(nanopos[m])); out_buf_ptr += sizeof(nanopos[m]);
        }
        transmit_out_buf(6);
        digitalWrite(LED_BUILTIN, HIGH); sleep_us(1000); digitalWrite(LED_BUILTIN, LOW); 
        in_buf_ptr = 0;
    } else if ((in_buf[0] == CMD_GET_STM_STATUS) && (in_buf_ptr == sizeof(cmd_get_stm_status_struct))) {
        //if (approach_active) {out_buf[0] = (uint8_t)1;} else {out_buf[0] = 0;}; 
        //if (piezo_feedback_active) {out_buf[1] = (uint8_t)1;} else {out_buf[1] = 0;}; 
        //if (piezo_scan_active) {out_buf[1] += (uint8_t)2;}; 
        //memcpy(out_buf+2,  &x, 2);
        //memcpy(out_buf+4,  &y, 2);
        //memcpy(out_buf+6, &z, 2);
        uint16_t adcvalue = 0;
        //for(int16_t j=0; j<1; j++) {
        //adcvalue += analogRead(ADC_PIN);
        //}

        for(out_buf_ptr=0; out_buf_ptr<OUT_BUF_LEN; out_buf_ptr+=2) { // todo: OUT_BUF_LEN/2
            adcvalue = analogRead(ADC_PIN);;		
            //out_buf[out_buf_ptr*2] 
            memcpy(out_buf+out_buf_ptr, &adcvalue, 2);
        };
        for (out_buf_ptr=0; out_buf_ptr<OUT_BUF_LEN;  out_buf_ptr++)  { Serial.write(out_buf[out_buf_ptr]); } // todo use transm...
        //transmit_out_buf(12);
        //for (int16_t j=0; j<scan_data_ptr; j++) { Serial.write(123); }
        //for (int16_t j=0; j<scan_data_ptr; j++) { Serial.write(scan_data[j]); }
        scan_data_ptr = 0;
        in_buf_ptr = 0;
    } else if ((in_buf[0] == CMD_SET_PIEZO) && (in_buf_ptr == sizeof(cmd_set_piezo_struct))) {
        //set_piezo_slow(*((int16_t*)(in_buf+ 2)), *((int16_t*)(in_buf+ 4)), *((int16_t*)(in_buf+ 8)), 100); 
        in_buf_ptr = 0;
        set_piezo_slow(x, y, SOFT_LIMIT_LOW, 100); 
        set_piezo_slow(*((int16_t*)(in_buf+ 1)), *((int16_t*)(in_buf+ 3)), SOFT_LIMIT_LOW, 100); 
    } else if ((in_buf[0] == CMD_LINESCAN) && (in_buf_ptr == sizeof(cmd_linescan_struct))) {
        scan_x_start		= *((int16_t*)(in_buf+ 1)); 
        scan_x_end        = *((int16_t*)(in_buf+ 3));
        scan_x_speed      = *((int16_t*)(in_buf+ 5));
        scan_x_sampling_step  = *((int16_t*)(in_buf+7));
        scan_y_position = *((int16_t*)(in_buf+9)); 

        // safely go to the initial piezo position and enable scanning (in the main routine)
        piezo_feedback_active = 1;
        piezo_scan_active = 1;

        in_buf_ptr = 0;

        //out_buf[0] = x;
        //out_buf[1] = y;
        //for (out_buf_ptr=0; out_buf_ptr<5; out_buf_ptr++) {
        //Serial.write(out_buf[out_buf_ptr]);
        //}
        //target_nanopos[0] = *((uint32_t*)(buffer+1));     // how to receive an uint32
        //uint32_t tmp =  42*256*256+256+123;				// how to send an uint32
        //memcpy(out_buf+out_buf_ptr, &tmp, sizeof(tmp)); out_buf_ptr += sizeof(tmp);
    } else if ((in_buf[0] == CMD_SET_PWM) && (in_buf_ptr == sizeof(cmd_set_PWM_struct))) {
        set_PWM(*((cmd_set_PWM_struct*)(in_buf)));
        in_buf_ptr = 0;
    } else if ((in_buf[0] == CMD_INIT_PWM) && (in_buf_ptr == sizeof(cmd_init_PWM_struct))) {
        init_PWM(*((cmd_init_PWM_struct*)(in_buf)));
        in_buf_ptr = 0;
    } else if ((in_buf[0] == CMD_GET_ADC) && (in_buf_ptr == sizeof(cmd_get_ADC_struct))) {
        get_ADC(*((cmd_get_ADC_struct*)(in_buf)));
        in_buf_ptr = 0;
    };
};


void setup() {/*{{{*/
	//set_sys_clock_khz(250000, false);
    Serial.begin(460800);
    //Serial.begin(460800, SERIAL_8N1);

    pinMode(LED_BUILTIN,  OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); 
	sleep_ms(300);
    digitalWrite(LED_BUILTIN, LOW); 
}/*}}}*/



void loop() {

    while (Serial.available() && (in_buf_ptr<IN_BUF_LEN)) { 
        in_buf[in_buf_ptr] = Serial.read(); 
            in_buf_ptr += 1; 
            process_messages();
        if (in_buf[0] != 0xFF)  // esp32's serial sometimes spits out extra 0xFF byte 
        {
        }
    }
        //in_buf_timeout = 0; 

    for (uint8_t m=0; m<MAX_STEPPER_COUNT; m++) { 
		if (stepper_dir_pin[m]) {
			uint32_t new_nanopos, actual_nanospeed;
			//if (!digitalRead(stepper_lowstop_pin[m])) { target_nanospeed[m] = 32; target_nanopos[m] = nanopos[m]+target_nanospeed[m]+1; } // get out from lower end switch
			if ((!digitalRead(stepper_lowstop_pin[m])) && (!endstop_override[m])) { 
					endstop_flag[m] = 1; target_nanospeed[m] = 0; target_nanopos[m] = nanopos[m];
					digitalWrite(LED_BUILTIN, HIGH); 
					} // stop at endswitch
			else {digitalWrite(LED_BUILTIN, LOW); }

				// TODO override option!
			//else if (was_at_end_switch[m]) { nanopos[m] = 0; target_nanopos[m] = 1; } // when freshly got from the end stop: calibrate position to zero
			//was_at_end_switch[m] = !digitalRead(stepper_lowstop_pin[m]); // remember end stop state

			actual_nanospeed = min(target_nanospeed[m],      (abs((int32_t)(nanopos[m] - target_nanopos[m])))/motor_inertia_coef[m]+1);
			actual_nanospeed = min(actual_nanospeed, (abs((int32_t)(nanopos[m] - initial_nanopos[m])))/motor_inertia_coef[m]+1);

			if (stepper_disable_pin[m]) {
			  digitalWrite(stepper_disable_pin[m], LOW); // XXX TEST
			  //if (nanopos[m] != target_nanopos[m]) { digitalWrite(stepper_disable_pin[m], LOW);} else { digitalWrite(stepper_disable_pin[m], HIGH);}
			}

			if (nanopos[m] < target_nanopos[m]) { 
			  digitalWrite(stepper_dir_pin[m], HIGH); 
			  new_nanopos = min(nanopos[m] + actual_nanospeed, target_nanopos[m]);
			} else if (nanopos[m] > target_nanopos[m]) { 
			  digitalWrite(stepper_dir_pin[m], LOW); 
			  new_nanopos = max(nanopos[m] - actual_nanospeed, target_nanopos[m]);
			} else { 
			  new_nanopos = nanopos[m];
			};

			//TODO test higher speeds up to 16*256
			// for (uint8_t j=0, j< abs((new_nanopos/NANOSTEP_PER_MICROSTEP) - (nanopos[m]/NANOSTEP_PER_MICROSTEP)), j++) {
				//digitalWrite(stepper_step_pin[m], HIGH);
				//ets_delay_us(1);  
				//digitalWrite(stepper_step_pin[m], LOW);
			//};
			if ( (new_nanopos / NANOSTEP_PER_MICROSTEP) != (nanopos[m] / NANOSTEP_PER_MICROSTEP)) {digitalWrite(stepper_step_pin[m], HIGH);}
			nanopos[m] = new_nanopos;
		}
		sleep_us(5); 
		digitalWrite(stepper_step_pin[m], LOW);
	}
  // 10 kHz main loop cycle; todo: should use timer interrupt instead
  sleep_us(100); 
}
