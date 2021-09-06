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

// == HARDWARE CONTROL PINS AND VARIABLES == {{{

// Following code allows for flexible control of up to 256 stepper motors. Just add a new column to the following arrays. 
// Note: using "nanopos" and "target_nanospeed" are essential for stepping speeds less than one microstep per cycle.

#define MAX_STEPPER_COUNT 16
uint8_t stepper_dir_pin[MAX_STEPPER_COUNT]     ;
uint8_t stepper_step_pin[MAX_STEPPER_COUNT]    ;
uint8_t stepper_lowstop_pin[MAX_STEPPER_COUNT] ;
uint8_t stepper_disable_pin[MAX_STEPPER_COUNT] ; 

#define NANOPOS_AT_ENDSTOP  (uint32_t)(1<<31)
#define NANOSTEP_PER_MICROSTEP  256  // maximum main loop cycles per step (higher value enables finer control of speed, but smaller range)
uint8_t endstop_override[MAX_STEPPER_COUNT];	// e.g. for moving out of endstop position
uint8_t endstop_flag[MAX_STEPPER_COUNT];	// remember the end stop event occurred
uint32_t nanopos[MAX_STEPPER_COUNT];	// current motor position 
uint32_t target_nanopos[MAX_STEPPER_COUNT]; // set from the computer; (zero usually corresponds to the lower end switch)
uint32_t target_nanospeed[MAX_STEPPER_COUNT]  ; // set from the computer; always ensure that 0 < target_nanospeed < NANOSTEP_PER_MICROSTEP 
uint32_t motor_inertia_coef[MAX_STEPPER_COUNT];		 // smooth ramp-up and ramp-down of motor speed 

uint32_t initial_nanopos[MAX_STEPPER_COUNT]; // used for smooth speed control
//uint8_t auto_motor_turnoff[MAX_STEPPER_COUNT] = {0}; // todo: if set to 1, the motor "disable" pin will be set when not moving
//uint8_t was_at_end_switch[MAX_STEPPER_COUNT] = {0};

/*}}}*/




// == COMPUTER COMMUNICATION: generic hardware-control messages == 

typedef uint8_t  B;  // visual compatibility with python's struct module
typedef uint16_t H;
typedef uint32_t I;
typedef int32_t  i;

#define IN_BUF_LEN 40
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

#define CMD_GET_STEPPER_STATUS 3		// just report the current nanopos and status
struct cmd_get_stepper_status_struct  {  
    uint8_t message_type;                
    uint8_t stepper_number;    // at byte 1
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
    I adcvalue = 0;
    for(int16_t j=0; j<16; j++) {
        adcvalue += analogRead(S.adc_pin);
    }
    memcpy(out_buf, &adcvalue, sizeof(adcvalue));
    transmit_out_buf(4);
 // TODO
};


void stepper_go(cmd_stepper_go_struct S)  // set the stepper motor to start moving to a given (nano)position
{
    uint8_t m = S.stepper_number;
    if (m<MAX_STEPPER_COUNT) {
        initial_nanopos[m]      = nanopos[m]; // remember the starting position (for speed control)
        target_nanopos[m]       = S.motor_targetpos;  
        target_nanospeed[m]     = S.motor_speed; 
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



//}

#define max(a,b)  ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a > _b ? _a : _b; })
#define min(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a < _b ? _a : _b; })
/*}}}*/
void transmit_out_buf(uint32_t total_bytes) {
    for (out_buf_ptr=0; out_buf_ptr<total_bytes; out_buf_ptr++) { Serial.write(out_buf[out_buf_ptr]); }
}

void process_messages() {
    //digitalWrite(LED_BUILTIN, HIGH); sleep_us(1000); digitalWrite(LED_BUILTIN, LOW); 
    if ((in_buf[0] == CMD_IDENTIFY) && (in_buf_ptr == sizeof(cmd_identify_struct))) {
        uint8_t byteArray[6] = {'r','p','2','d','a','q'}; memcpy(out_buf, byteArray, 6);
        uint8_t byteBrray[6] = {'2','1','0','8','2','9'}; memcpy(out_buf+6, byteBrray, 6);
		pico_get_unique_board_id((pico_unique_board_id_t*)(out_buf+12));
        transmit_out_buf(30);
        in_buf_ptr = 0;
    } else if ((in_buf[0] == CMD_STEPPER_GO) && (in_buf_ptr == sizeof(cmd_stepper_go_struct))) {
        stepper_go(*((cmd_stepper_go_struct*)(in_buf)));
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
        in_buf_ptr = 0;

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
    Serial.begin(460800); // rm or is this relevant?
    //Serial.begin(460800, SERIAL_8N1);

    pinMode(LED_BUILTIN,  OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); sleep_ms(100); digitalWrite(LED_BUILTIN, LOW); 
}/*}}}*/



void loop() {

    while (Serial.available() && (in_buf_ptr<IN_BUF_LEN)) { 
        in_buf[in_buf_ptr] = Serial.read(); 
            in_buf_ptr += 1; 
            process_messages();
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
