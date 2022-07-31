
// Stepper support using Stepstick
#define NANOPOS_AT_ENDSWITCH  (uint32_t)(1<<31)   // so that motor can move symmetrically from origin
#define NANOSTEP_PER_MICROSTEP  256             // for fine-grained position control
#define MAX_STEPPER_COUNT 16
#define STEPPER_IS_MOVING(m)	(stepper[m].max_nanospeed > 0)
#define ENDSWITCH_TEST(m) ((stepper[m].endswitch_pin) && \
			(!stepper[m].endswitch_ignore) && (!gpio_get(stepper[m].endswitch_pin)))


typedef struct __attribute__((packed)) {
    uint8_t  initialized;      
    uint8_t  dir_pin;      // always byte 0
    uint8_t  step_pin;
    uint8_t  endswitch_pin;
    uint8_t  disable_pin;
    uint8_t  endswitch_ignore;
	uint8_t  endswitch_expected;
    uint32_t nanopos;
    uint32_t target_nanopos;
    uint32_t max_nanospeed;
    uint32_t inertia_coef;
    uint32_t previous_nanopos;
    uint8_t  previous_endswitch;
} stepper_config_t;
volatile stepper_config_t stepper[MAX_STEPPER_COUNT];



struct __attribute__((packed)) {
    uint8_t report_code;
    uint32_t initial_nanopos;
} stepper_init_report;

void stepper_init() {
	struct __attribute__((packed)) {
		uint8_t  stepper_number;	// min=0	max=15 
		uint8_t  dir_pin;			// min=0	max=24
		uint8_t  step_pin;			// min=0	max=24
		int8_t   endswitch_pin;		// min=-1	max=24		default=-1
		int8_t   disable_pin;		// min=-1	max=25		default=-1
		uint32_t inertia;			// min=0	max=10000	default=30
	} * args = (void*)(command_buffer+1);

	uint8_t m = args->stepper_number;
	if (m<MAX_STEPPER_COUNT) {
		stepper[m].dir_pin     = args->dir_pin;
		stepper[m].step_pin    = args->step_pin;
		stepper[m].endswitch_pin = args->endswitch_pin;
		stepper[m].disable_pin = args->disable_pin;
		stepper[m].inertia_coef  = max(args->inertia, 1); // prevent div/0 error
		stepper[m].endswitch_ignore = 0;
		stepper[m].previous_endswitch = 0;
		stepper[m].endswitch_expected = 1;

		gpio_init(stepper[m].dir_pin); gpio_set_dir(stepper[m].dir_pin, GPIO_OUT);
		gpio_init(stepper[m].step_pin); gpio_set_dir(stepper[m].step_pin, GPIO_OUT);
		if (stepper[m].endswitch_pin >= 0) {
			gpio_set_dir(stepper[m].endswitch_pin, GPIO_IN);
			gpio_pull_up(stepper[m].endswitch_pin); }
		if (stepper[m].disable_pin >= 0) {
			gpio_init(stepper[m].disable_pin);
			gpio_set_dir(stepper[m].disable_pin, GPIO_OUT);
        }

		stepper[m].nanopos = NANOPOS_AT_ENDSWITCH; // default position !=0 to allow going back
		stepper[m].target_nanopos = stepper[m].nanopos;  // motor stands still when (re)defined
		//if (!stepper[m].initialized) { // nanopos==0 means motor has not been initialized yet
		//}

		stepper[m].initialized = 1;
	}

	stepper_init_report.initial_nanopos = stepper[m].nanopos;
	tx_header_and_data(&stepper_init_report, sizeof(stepper_init_report), 0,0,0);
}




struct __attribute__((packed)) {
    uint8_t report_code;
    uint8_t stepper_number;
    uint8_t endswitch;
		// even sorcerers are subject to work safety regulations, else a motor "ends" them
    uint32_t nanopos; // TODO
    uint16_t steppers_init_bitmask; // TODO
    uint16_t steppers_moving_bitmask; // TODO
    uint16_t steppers_endswitch_bitmask; // TODO
} stepper_status_report;

void stepper_status() {
	struct __attribute__((packed)) {
		uint8_t  stepper_number;	// min=0	max=15
	} * args = (void*)(command_buffer+1);
	uint8_t m = args->stepper_number;
	stepper_status_report.stepper_number = m;
	stepper_status_report.endswitch = (ENDSWITCH_TEST(m)); // ? 1 : 0 TODO 
	stepper_status_report.nanopos = stepper[m].nanopos;

	stepper_status_report.steppers_init_bitmask = 0;
	stepper_status_report.steppers_moving_bitmask = 0;
	stepper_status_report.steppers_endswitch_bitmask = 0;
	
	for (uint8_t n=0; n<MAX_STEPPER_COUNT; n++) {
		if (stepper[n].initialized) 
			stepper_status_report.steppers_init_bitmask |= (1<<n);
		if (STEPPER_IS_MOVING(n))
			stepper_status_report.steppers_moving_bitmask |= (1<<n);
		if (ENDSWITCH_TEST(n))
			stepper_status_report.steppers_endswitch_bitmask |= (1<<n);
	}

	//uint64_t ts = (uint64_t)to_us_since_boot(get_absolute_time()); TODO
	tx_header_and_data(&stepper_status_report, sizeof(stepper_status_report), 0, 0, 0);
}



struct __attribute__((packed)) {
    uint8_t report_code;
    uint8_t stepper_number;
    uint32_t nanopos;
    uint8_t endswitch_ignored;
    uint8_t endswitch_triggered;
    uint8_t endswitch_expected;
    uint16_t steppers_init_bitmask;		
    uint16_t steppers_moving_bitmask;	
    uint16_t steppers_endswitch_bitmask;
} stepper_move_report;  // (transmitted when a stepper actually finishes its move)

void stepper_move() {
	struct __attribute__((packed))  {
		uint8_t  stepper_number;		// min=0 max=15
		uint32_t to;					
		uint32_t speed;					// min=1 max=10000
		int8_t  endswitch_ignore;		// min=-1 max=1		default=-1
		int8_t  endswitch_expect;		// min=-1 max=1		default=-1

		int8_t  reset_nanopos;			// min=0 max=1		default=0
	} * args = (void*)(command_buffer+1);

    uint8_t m = args->stepper_number; if (m<MAX_STEPPER_COUNT) {
		if (args->reset_nanopos) {stepper[m].nanopos = NANOPOS_AT_ENDSWITCH;} // a.k.a. relative movement

		if (args->endswitch_ignore == -1) { // auto-set value
			stepper[m].endswitch_ignore = stepper[m].previous_endswitch;
		} else {		// user-set value
			stepper[m].endswitch_ignore   = args->endswitch_ignore;
		}

		if (args->endswitch_expect != -1) 
			stepper[m].endswitch_expected =  args->endswitch_expect;

        stepper[m].previous_nanopos      = stepper[m].nanopos; // remember the starting position (for smooth start)
        stepper[m].target_nanopos       = args->to;
        stepper[m].max_nanospeed        = max(args->speed, 1); // if zero, it means motor is idle
    }
}

void mk_tx_stepper_report(uint8_t n)
{
	stepper_move_report.stepper_number = n;
	stepper_move_report.nanopos = stepper[n].nanopos;
	stepper_move_report.endswitch_ignored = stepper[n].endswitch_ignore;
	stepper_move_report.endswitch_triggered = stepper[n].previous_endswitch;
	stepper_move_report.endswitch_expected = stepper[n].endswitch_expected;

	stepper_move_report.steppers_init_bitmask = 0;
	stepper_move_report.steppers_moving_bitmask = 0;
	stepper_move_report.steppers_endswitch_bitmask = 0;
	
	for (uint8_t m=0; m<MAX_STEPPER_COUNT; m++) {
		if (stepper[m].initialized) 
			stepper_move_report.steppers_init_bitmask |= (1<<m);
		if (STEPPER_IS_MOVING(m))
			stepper_move_report.steppers_moving_bitmask |= (1<<m);

		if (n==m) {
			if (stepper[n].previous_endswitch) // better copy recent value here (eliminates glitches)
				stepper_move_report.steppers_endswitch_bitmask |= (1<<m);
			// note that endswitch_ignore zeroes this bit, too
		} else {
			if (ENDSWITCH_TEST(m))
				stepper_move_report.steppers_endswitch_bitmask |= (1<<m);
		}

        //uint64_t ts = (uint64_t)to_us_since_boot(get_absolute_time()); TODO
	}
	tx_header_and_data(&stepper_move_report, sizeof(stepper_move_report), 0, 0, 0);
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
		if (stepper[m].initialized) {  // one moving stepper takes ca. 450 CPU cycles (if not messaging) // TODO rm CHECK
			uint32_t new_nanopos, actual_nanospeed;

			//TODO if (STEPPER_IS_MOVING(m))  { // i.e. when stepper is moving
			if (stepper[m].nanopos != stepper[m].target_nanopos)  { // i.e. when stepper is moving
				actual_nanospeed = min(stepper[m].max_nanospeed,
						usqrt(udiff(stepper[m].nanopos, stepper[m].target_nanopos))*100/stepper[m].inertia_coef + 1);
				actual_nanospeed = min(actual_nanospeed,
						usqrt(udiff(stepper[m].nanopos, stepper[m].previous_nanopos))*100/stepper[m].inertia_coef + 1);

				if (stepper[m].nanopos < stepper[m].target_nanopos) {
				  gpio_put(stepper[m].dir_pin, 1);
				  new_nanopos = min(stepper[m].nanopos + actual_nanospeed, stepper[m].target_nanopos);
				} else {
				  gpio_put(stepper[m].dir_pin, 0);
				  new_nanopos = max(stepper[m].nanopos - actual_nanospeed, stepper[m].target_nanopos);
				}

				if (ENDSWITCH_TEST(m)) { 
					// occurs once stepper triggers end-stop switch
					stepper[m].max_nanospeed = 0; 
					stepper[m].target_nanopos = new_nanopos;  // immediate stop
					stepper[m].previous_endswitch = 1; // remember reason for stopping
					mk_tx_stepper_report(m);
					stepper[m].endswitch_expected = 0;
				} else if (new_nanopos == stepper[m].target_nanopos) {
					// occurs once move finishes successfully
					stepper[m].max_nanospeed = 0;
					//stepper[m].target_nanopos = new_nanopos;  // TODO rm?
					stepper[m].previous_endswitch = 0;
					mk_tx_stepper_report(m);
				}

				if (stepper[m].disable_pin >= 0) gpio_put(stepper[m].disable_pin, 0);
			} else { // i.e. when stepper is not moving
			  new_nanopos = stepper[m].nanopos;
			  if (stepper[m].disable_pin >= 0) gpio_put(stepper[m].disable_pin, 1);
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
