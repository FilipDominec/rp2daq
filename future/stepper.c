

// Stepper support using Stepstick
#define NANOPOS_AT_ENDSTOP  (uint32_t)(1<<31)   // so that motor can move symmetrically from origin
#define NANOSTEP_PER_MICROSTEP  256             // for fine-grained position control
#define MAX_STEPPER_COUNT 16
typedef struct __attribute__((packed)) {
    uint8_t  initialized;      
    uint8_t  dir_pin;      // always byte 0
    uint8_t  step_pin;
    uint8_t  lowstop_pin;
    uint8_t  disable_pin;
    uint8_t  endstop_override;
    uint32_t nanopos;
    uint32_t target_nanopos;
    uint32_t max_nanospeed;
    uint32_t inertia_coef;
    uint32_t initial_nanopos;
} stepper_struct;
volatile stepper_struct stepper[MAX_STEPPER_COUNT];


// stepper move finished message
int stepper_finished_report_message[] = {3, STEPPER_MOVE, 255, 0};

// stepper status message
int stepper_status_message[] = {8+4, GET_STEPPER_STATUS, 0, 0, 0, 0,0,0,0, 0,0,0,0};

typedef struct __attribute__((packed)) {
    uint8_t  message_type;      // always byte 0
    uint8_t  stepper_number;
    uint8_t  dir_pin;
    uint8_t  step_pin;
    uint8_t  endswitch_pin;
    uint8_t  disable_pin;
    uint32_t inertia_coef;
} cmd_stepper_new_struct;
void stepper_new() {
	cmd_stepper_new_struct* S = (void*)command_buffer;

	uint8_t m = S->stepper_number;
	if (m<MAX_STEPPER_COUNT) {
		stepper[m].initialized = 1;
		stepper[m].endstop_override = 0;
		stepper[m].dir_pin     = S->dir_pin;
		stepper[m].step_pin    = S->step_pin;
		stepper[m].lowstop_pin = S->endswitch_pin;
		stepper[m].disable_pin = S->disable_pin;
		stepper[m].inertia_coef  = max(S->inertia_coef, 1); // prevent div/0 error

		gpio_init(stepper[m].dir_pin); gpio_set_dir(stepper[m].dir_pin, GPIO_OUT);
		gpio_init(stepper[m].step_pin); gpio_set_dir(stepper[m].step_pin, GPIO_OUT);
		if (stepper[m].lowstop_pin) {
			gpio_set_dir(stepper[m].lowstop_pin, GPIO_IN);
			gpio_pull_up(stepper[m].lowstop_pin); }
		if (stepper[m].disable_pin) {
			gpio_init(stepper[m].disable_pin);
			gpio_set_dir(stepper[m].disable_pin, GPIO_OUT);
        }

		if ((stepper[m].nanopos == 0)) { // nanopos==0 means motor has not been initialized yet
			stepper[m].nanopos = NANOPOS_AT_ENDSTOP; // default position !=0 to allow going back
			stepper[m].target_nanopos = stepper[m].nanopos;  // motor stands still when (re)defined
		}
	}
}

typedef struct __attribute__((packed))  {
    uint8_t  message_type;
    uint8_t  stepper_number;
    uint32_t motor_targetpos;
    uint32_t max_nanospeed;
    uint8_t  endstop_override;
    uint8_t  reset_nanopos;
} cmd_stepper_move_struct;
void stepper_move() {
	cmd_stepper_move_struct* S = (void*)command_buffer;
    uint8_t m = S->stepper_number;
    if (m<MAX_STEPPER_COUNT) {
		if (S->reset_nanopos) {stepper[m].nanopos = NANOPOS_AT_ENDSTOP;} // a.k.a. relative movement
        stepper[m].initial_nanopos      = stepper[m].nanopos; // remember the starting position (for smooth start)
        stepper[m].target_nanopos       = S->motor_targetpos;
        stepper[m].max_nanospeed        = S->max_nanospeed;
        stepper[m].endstop_override     = S->endstop_override;
		if (stepper[m].target_nanopos == stepper[m].nanopos) {
			stepper_finished_report_message[2] = m;
			stepper_finished_report_message[3] = 0;
			serial_write(stepper_finished_report_message,
					sizeof(stepper_finished_report_message)/sizeof(int) );
		}
    }
}

typedef struct __attribute__((packed)) {
	uint8_t  message_type;
	uint8_t  stepper_number;
} cmd_get_stepper_status;
void get_stepper_status() {
	cmd_get_stepper_status* S = (void*)command_buffer;
	uint8_t m = S->stepper_number;
	if (m<MAX_STEPPER_COUNT) {
        stepper_status_message[2] = m;
        stepper_status_message[3] = (stepper[m].nanopos == stepper[m].target_nanopos)? 1 : 0;
        stepper_status_message[4] = (!gpio_get(stepper[m].lowstop_pin))?  1 : 0;

        stepper_status_message[5] = (uint8_t)(stepper[m].nanopos>>24) & 0xFF;
        stepper_status_message[6] = (uint8_t)(stepper[m].nanopos>>16) & 0xFF;
        stepper_status_message[7] = (uint8_t)(stepper[m].nanopos>>8) & 0xFF;
        stepper_status_message[8] = (uint8_t)(stepper[m].nanopos) & 0xFF;

        uint64_t ts = (uint64_t)to_us_since_boot(get_absolute_time());
        stepper_status_message[9] = (uint8_t)(ts>>(24)) & 0xFF;
        stepper_status_message[10] = (uint8_t)(ts>>(16)) & 0xFF;
        stepper_status_message[11] = (uint8_t)(ts>>(8)) & 0xFF;
        stepper_status_message[12] = (uint8_t)(ts>>32) & 0xFF;

        serial_write(stepper_status_message, sizeof(stepper_status_message)/sizeof(int) );
	}
}



						//if ((!stepper[m].endstop_override) && (!gpio_get(stepper[m].lowstop_pin))) {
							//stepper[m].max_nanospeed = 0;
							//stepper_finished_report_message[2] = m;
							//stepper_finished_report_message[3] = 1;
						//}
