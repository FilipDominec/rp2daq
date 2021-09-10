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
uint8_t stepper_dir_pin[MAX_STEPPER_COUNT]    = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0} ;
uint8_t stepper_step_pin[MAX_STEPPER_COUNT]    ;
uint8_t stepper_lowstop_pin[MAX_STEPPER_COUNT] ;
uint8_t stepper_disable_pin[MAX_STEPPER_COUNT] ; 

// default motor position (it is nonzero to allow going both directions; it is unsigned to make division truncate consistently
#define NANOPOS_AT_ENDSTOP  (uint32_t)(1<<31) 
// maximum main loop cycles per step (higher value enables finer control of speed, but smaller range)
#define NANOSTEP_PER_MICROSTEP  256  
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
uint8_t True = {1};  // constants for USB communication
uint8_t False = {0};

#define IN_BUF_LEN 40
uint8_t in_buf[IN_BUF_LEN];     // input message buffer, first byte always indicates the type of message (and its expected length)
uint16_t in_buf_ptr = 0;         // pointer behind the last received byte

//#define MESSAGE_TIMEOUT_LIMIT 100 // TODO implement fail-safe timeout
//uint8_t in_buf_timeout = 0; // erase the input buffer if more than MESSAGE_TIMEOUT_LIMIT main loop cycles pass without receiving next byte 

#define OUT_BUF_LEN 4000
uint8_t out_buf[OUT_BUF_LEN];		// output message buffer
int16_t out_buf_ptr = 0;			// pointer to buffer end

void transmit_out_buf(uint32_t total_bytes) {
    for (out_buf_ptr=0; out_buf_ptr<total_bytes; out_buf_ptr++) { Serial.write(out_buf[out_buf_ptr]); }
}

//#define SERIAL_WRITE(var) ({for (uint8_t p=0; p<sizeof(var); p++) { Serial.write(*((char*)((&var)+p))); };}) # what's wrong?
#define SERIAL_WRITE(var) ({ char* buf=(char *)&var; for (uint8_t p=0; p<sizeof(var); p++) { Serial.write(buf[p]); };})
#define max(a,b)  ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a > _b ? _a : _b; })
#define min(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a < _b ? _a : _b; })


#define CMD_IDENTIFY  0		// blink LED, return "espdaq" + custom 24B identifier
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

#define CMD_GET_PIN  6		// simple digital input
struct cmd_get_pin_struct  {  
    B message_type;      // always byte 0 
    B pin_number;    
} __attribute__((packed));     

#define CMD_SET_PIN  7		// simple digital output 
struct cmd_set_pin_struct  {  
    B message_type;      // always byte 0 
    B pin_number;    
    B value;    
    B output_mode;    
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




/*}}}*/

void process_messages() {
    typeof(in_buf_ptr) cmd_length = in_buf_ptr; 
    in_buf_ptr = 0; // tentatively reset buffer for new message

    if ((in_buf[0] == CMD_IDENTIFY) && (cmd_length == sizeof(cmd_identify_struct))) {
        uint8_t text[12] = {'r','p','2','d','a','q',    '2','1','0','9','0','9'};
		pico_unique_board_id_t *unique_id; pico_get_unique_board_id(unique_id);
        SERIAL_WRITE(text);
        SERIAL_WRITE(*unique_id);
        digitalWrite(LED_BUILTIN, HIGH); sleep_us(1000); digitalWrite(LED_BUILTIN, LOW); 

    //in_buf_ptr = 0; // tentatively reset buffer for new message
    } else if ((in_buf[0] == CMD_STEPPER_GO) && (cmd_length == sizeof(cmd_stepper_go_struct))) {
        cmd_stepper_go_struct* S = (cmd_stepper_go_struct*)in_buf;
        uint8_t m = S->stepper_number;
        if (m<MAX_STEPPER_COUNT) {
            initial_nanopos[m]      = nanopos[m]; // remember the starting position (for speed control)
            target_nanopos[m]       = S->motor_targetpos;  
            target_nanospeed[m]     = S->motor_speed; 
            endstop_override[m]     = S->endstop_override; 
            if (S->reset_zero_pos) {initial_nanopos[m] = NANOPOS_AT_ENDSTOP; nanopos[m] = NANOPOS_AT_ENDSTOP;};    
            endstop_flag[m]         = 0;   // flag only resets upon a new stepper_go message
        }

    } else if ((in_buf[0] == CMD_INIT_STEPPER) && (cmd_length == sizeof(cmd_init_stepper_struct))) {
        cmd_init_stepper_struct* S = (cmd_init_stepper_struct*)in_buf;
        uint8_t m = S->stepper_number;
        if (m<MAX_STEPPER_COUNT) {
            stepper_dir_pin[m]     = S->dir_pin;
            stepper_step_pin[m]    = S->step_pin;
            stepper_lowstop_pin[m] = S->endswitch_pin;
            stepper_disable_pin[m] = S->disable_pin;
            endstop_override[m]    = 0; 
            motor_inertia_coef[m]  = S->motor_inertia;
            if (motor_inertia_coef[m] == 0) { motor_inertia_coef[m]  = 1;} // prevents div by 0 error
            endstop_flag[m]        = 0;
            pinMode(stepper_dir_pin[m],  OUTPUT);
            pinMode(stepper_step_pin[m],  OUTPUT);
            if (stepper_lowstop_pin[m]) pinMode(stepper_lowstop_pin[m],  INPUT_PULLUP);
            if (stepper_disable_pin[m]) pinMode(stepper_disable_pin[m],  OUTPUT);

            if (nanopos[m] == 0) {
                nanopos[m] = NANOPOS_AT_ENDSTOP; // (endstops are in the middle of the motor range, not 0)
                target_nanopos[m] = nanopos[m];  // motor stands still when (re)defined
            }
        }	

    } else if ((in_buf[0] == CMD_GET_PIN) && (cmd_length == sizeof(cmd_get_pin_struct))) {
        cmd_get_pin_struct* S = (cmd_get_pin_struct*)in_buf;
		if (digitalRead(S->pin_number)) { SERIAL_WRITE(True); } else { SERIAL_WRITE(False); }; 
        // TODO TEST

    } else if ((in_buf[0] == CMD_SET_PIN) && (cmd_length == sizeof(cmd_set_pin_struct))) {
        cmd_set_pin_struct* S = (cmd_set_pin_struct*)in_buf;
		if (S->output_mode) { 
            pinMode(S->pin_number, OUTPUT); 
            if (S->value) { digitalWrite(S->pin_number, HIGH); } else { digitalWrite(S->pin_number, LOW); }; 
        } else { 
            digitalWrite(S->pin_number, INPUT); 
            if (S->value) { 
                //digitalWrite(S->pin_number, INPUT_PULLUP); 
                set_pin_mode_digital_input_pull_up(S->pin_number)
            } else { 
                //digitalWrite(S->pin_number, LOW); 
                set_pin_mode_digital_input_pull_down(S->pin_number)
            }; 
        }; 
        // TODO TEST

    } else if ((in_buf[0] == CMD_GET_STEPPER_STATUS) && (cmd_length == sizeof(cmd_get_stepper_status_struct))) {
        uint8_t m = in_buf[1];
        if (m<MAX_STEPPER_COUNT) {
            if (nanopos[m] == target_nanopos[m]) { SERIAL_WRITE(False); } else { SERIAL_WRITE(True); }; 
            SERIAL_WRITE(endstop_flag[m]);
            SERIAL_WRITE(nanopos[m]);
        }

    } else if ((in_buf[0] == CMD_INIT_PWM) && (cmd_length == sizeof(cmd_init_PWM_struct))) {
        cmd_init_PWM_struct* S = (cmd_init_PWM_struct*) in_buf;
        // TODO

    } else if ((in_buf[0] == CMD_SET_PWM) && (cmd_length == sizeof(cmd_set_PWM_struct))) {
        cmd_set_PWM_struct* S = (cmd_set_PWM_struct*)in_buf;
        // TODO

    } else if ((in_buf[0] == CMD_GET_ADC) && (cmd_length == sizeof(cmd_get_ADC_struct))) {
        cmd_get_ADC_struct* S = (cmd_get_ADC_struct*)in_buf;
        I adcvalue = 0;
        for(int16_t j=0; j < S->oversampling_count; j++) { adcvalue += analogRead(S->adc_pin); }
        SERIAL_WRITE(adcvalue);

    } else {
        in_buf_ptr = cmd_length; // message not finished; will continue receiving bytes
    };
};


void setup() {/*{{{*/
    for(int16_t j=26; j < 29; j++) {  analogRead(j); } // first ADC reads are wrong
    pinMode(LED_BUILTIN,  OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); sleep_ms(100); digitalWrite(LED_BUILTIN, LOW); 

//pinMode(0, OUTPUT);pinMode(1, OUTPUT);pinMode(2, OUTPUT); pinMode(3, OUTPUT);pinMode(4, OUTPUT);pinMode(5, OUTPUT); //XXX

}/*}}}*/



void loop() {

    while (Serial.available() && (in_buf_ptr<IN_BUF_LEN)) { 
        in_buf[in_buf_ptr] = Serial.read(); 
            in_buf_ptr += 1; 
            process_messages();
    }
    //in_buf_timeout = 0; 

    for (uint8_t m=0; m<MAX_STEPPER_COUNT; m++) { 
		if (stepper_dir_pin[m]) { // TODO use motor[m].isdefined instead
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

            sleep_us(5); 
            digitalWrite(stepper_step_pin[m], LOW);
		}
	}
  // 10 kHz main loop cycle; todo: should use timer interrupt instead
  sleep_us(100); 
}
