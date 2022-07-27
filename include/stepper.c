
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
} stepper_config;
volatile stepper_config stepper[MAX_STEPPER_COUNT];



struct __attribute__((packed)) {
    uint8_t report_code;
} stepper_init_report;

void stepper_init() {
	struct __attribute__((packed)) {
		uint8_t  stepper_number; 
		uint8_t  dir_pin;
		uint8_t  step_pin;
		uint8_t  endswitch_pin;
		uint8_t  disable_pin;
		uint32_t inertia_coef;
	} * args = (void*)(command_buffer+1);

	uint8_t m = args->stepper_number;
	if (m<MAX_STEPPER_COUNT) {
		stepper[m].initialized = 1;
		stepper[m].endstop_override = 0;
		stepper[m].dir_pin     = args->dir_pin;
		stepper[m].step_pin    = args->step_pin;
		stepper[m].lowstop_pin = args->endswitch_pin;
		stepper[m].disable_pin = args->disable_pin;
		stepper[m].inertia_coef  = max(args->inertia_coef, 1); // prevent div/0 error

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

	tx_header_and_data(&stepper_init_report, sizeof(stepper_init_report), 0,0,0);
}




struct __attribute__((packed)) {
    uint8_t report_code;
    uint8_t stepper_number; // TODO
    uint8_t steppers_init_bitmask; // TODO
    uint8_t steppers_moving_bitmask; // TODO
    uint8_t steppers_endstop_bitmask; // TODO
} stepper_move_report; // used later, when stepper actually finishes its move

void stepper_move() {
	struct __attribute__((packed))  {
		uint8_t  stepper_number;
		uint32_t motor_targetpos;
		uint32_t max_nanospeed;
		uint8_t  endstop_override;
		uint8_t  reset_nanopos;
	} * args = (void*)(command_buffer+1);

    uint8_t m = args->stepper_number; if (m<MAX_STEPPER_COUNT) {
		if (args->reset_nanopos) {stepper[m].nanopos = NANOPOS_AT_ENDSTOP;} // a.k.a. relative movement
        stepper[m].initial_nanopos      = stepper[m].nanopos; // remember the starting position (for smooth start)
        stepper[m].target_nanopos       = args->motor_targetpos;
        stepper[m].max_nanospeed        = args->max_nanospeed;
        stepper[m].endstop_override     = args->endstop_override;
		if (stepper[m].target_nanopos == stepper[m].nanopos) {
			// TODO tx finish
			//stepper_finished_report_message[2] = m;
			//stepper_finished_report_message[3] = 0;
			//serial_write(stepper_finished_report_message,
					//sizeof(stepper_finished_report_message)/sizeof(int) );
		}
    }
}


struct __attribute__((packed)) {
    uint8_t report_code;
    uint8_t stepper_number; // TODO
    uint8_t steppers_init_bitmask; // TODO
    uint8_t steppers_moving_bitmask; // TODO
    uint8_t steppers_endstop_bitmask; // TODO
} stepper_status_report; // used later, when stepper actually finishes its move

// stepper status message
//int stepper_status_message[] = {8+4, GET_STEPPER_STATUS, 0, 0, 0, 0,0,0,0, 0,0,0,0};


void get_stepper_status() {
	struct __attribute__((packed)) {
		uint8_t  stepper_number;
	} * args = (void*)(command_buffer+1);
	uint8_t m = args->stepper_number;
	if (m<MAX_STEPPER_COUNT) {
        //stepper_status_message[2] = m;
        //stepper_status_message[3] = (stepper[m].nanopos == stepper[m].target_nanopos)? 1 : 0;
        //stepper_status_message[4] = (!gpio_get(stepper[m].lowstop_pin))?  1 : 0;
//
        //stepper_status_message[5] = (uint8_t)(stepper[m].nanopos>>24) & 0xFF;
        //stepper_status_message[6] = (uint8_t)(stepper[m].nanopos>>16) & 0xFF;
        //stepper_status_message[7] = (uint8_t)(stepper[m].nanopos>>8) & 0xFF;
        //stepper_status_message[8] = (uint8_t)(stepper[m].nanopos) & 0xFF;
//
        //uint64_t ts = (uint64_t)to_us_since_boot(get_absolute_time());
        //stepper_status_message[9] = (uint8_t)(ts>>(24)) & 0xFF;
        //stepper_status_message[10] = (uint8_t)(ts>>(16)) & 0xFF;
        //stepper_status_message[11] = (uint8_t)(ts>>(8)) & 0xFF;
        //stepper_status_message[12] = (uint8_t)(ts>>32) & 0xFF;
//
        //serial_write(stepper_status_message, sizeof(stepper_status_message)/sizeof(int) );
	}
}


inline uint32_t usqrt(uint32_t val) { // fast and terribly inaccurate uint32 square root
#define USQRTBOOST 1  // defaults to 0, set to 1 for faster convergence when 1<iterN<5
#define USQRTITERN 3 // increase iterations until accuracy is ok for you
#define USQRTSTART 100 // best if close to likely result (use 1<<16 if no guess)
    uint32_t a, b;
    if (val < 2) return val; /* avoid div/0 */
    a = USQRTSTART;
	for (uint8_t j=0; j<USQRTITERN; j++) {
		b = val / a;
        a = (a + b)>>(USQRTBOOST+1);
    }
    return a<<USQRTBOOST;
}

inline uint32_t udiff(uint32_t a, uint32_t b) {  // absolute value of difference
	return (max(a,b)-min(a,b));
}


void stepper_update() {
	for (uint8_t m=0; m<MAX_STEPPER_COUNT; m++) {
		if (stepper[m].initialized) {  // one running stepper takes ca. 450 CPU cycles (if not messaging)
			uint32_t new_nanopos, actual_nanospeed;

			if (stepper[m].nanopos != stepper[m].target_nanopos)  { // i.e. when stepper is running
				actual_nanospeed = min(stepper[m].max_nanospeed,
						usqrt(udiff(stepper[m].nanopos, stepper[m].target_nanopos))*100/stepper[m].inertia_coef + 1);
				actual_nanospeed = min(actual_nanospeed,
						usqrt(udiff(stepper[m].nanopos, stepper[m].initial_nanopos))*100/stepper[m].inertia_coef + 1);

				if (stepper[m].nanopos < stepper[m].target_nanopos) {
				  gpio_put(stepper[m].dir_pin, 1);
				  new_nanopos = min(stepper[m].nanopos + actual_nanospeed, stepper[m].target_nanopos);
				} else {
				  gpio_put(stepper[m].dir_pin, 0);
				  new_nanopos = max(stepper[m].nanopos - actual_nanospeed, stepper[m].target_nanopos);
				}

				// occurs once move finishes successfully:
				if (new_nanopos == stepper[m].target_nanopos) {
					//stepper_finished_report_message[2] = m; TODO REPORT
					//stepper_finished_report_message[3] = 0;
					//serial_write(stepper_finished_report_message,
							//sizeof(stepper_finished_report_message)/sizeof(int) );
				}

				// occurs once stepper triggers end-stop switch TODO REPORT
				/*
				if ((!stepper[m].endstop_override) && (!gpio_get(stepper[m].lowstop_pin))) {
					stepper[m].max_nanospeed = 0;
					stepper[m].target_nanopos = new_nanopos;  // immediate stop
					stepper_finished_report_message[2] = m;
					stepper_finished_report_message[3] = 1;
					//serial_write(stepper_finished_report_message,
							//sizeof(stepper_finished_report_message)/sizeof(int) );
					if (stepper[m].disable_pin) gpio_put(stepper[m].disable_pin, 1);
				}
				*/

				if (stepper[m].disable_pin) gpio_put(stepper[m].disable_pin, 0);
			} else { // i.e. when stepper is not running
			  new_nanopos = stepper[m].nanopos;
			  if (stepper[m].disable_pin) gpio_put(stepper[m].disable_pin, 1);
			}

			//for (uint8_t j=0; j< abs((new_nanopos/NANOSTEP_PER_MICROSTEP) -
						//(stepper[m].nanopos/NANOSTEP_PER_MICROSTEP)); j++) {
			for (uint8_t j=0; 
					j< udiff((new_nanopos/NANOSTEP_PER_MICROSTEP), 
						(stepper[m].nanopos/NANOSTEP_PER_MICROSTEP)); 
					j++) {
				gpio_put(stepper[m].step_pin, 1); 
				busy_wait_us_32(1);// TODO safety margin, but 50ns pulse seems OK?
				gpio_put(stepper[m].step_pin, 0);
			}
			stepper[m].nanopos = new_nanopos;
		}
	}
}
