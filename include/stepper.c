// TODO Measure timing if NANOPOS_AT_ENDSWITCH is int32 instead; what was a trouble on atmega8 may be fine
// TODO Try how many steppers can really be controlled (e.g. when ADC runs at 500 kSps...)
// TODO Disperse motor service routines consecutively in time (e.g. change 10kHz timer to 80kHz ?)

// Stepper support using Stepstick
#define NANOPOS_AT_ENDSWITCH  (uint32_t)(1<<31)   // so that motor can move symmetrically from origin
#define NANOSTEP_PER_MICROSTEP  256             // for fine-grained speed and position control
#define MAX_STEPPER_COUNT 16
#define STEPPER_IS_MOVING(m)	(stepper[m].max_nanospeed > 0)
#define ENDSWITCH_TEST(m) ((stepper[m].endswitch_gpio >= 0) && \
			(!stepper[m].endswitch_ignore) && (!gpio_get(stepper[m].endswitch_gpio)))


typedef struct __attribute__((packed)) {
    uint8_t  initialized;      
    uint8_t  dir_gpio;
    uint8_t  step_gpio;
    uint8_t  endswitch_gpio;
    uint8_t  disable_gpio;
    uint8_t  endswitch_ignore;
	uint8_t  endswitch_expected;
    uint32_t nanopos;
    uint32_t target_nanopos;
    uint32_t max_nanospeed;
    uint32_t inertia_coef;
    uint32_t previous_nanopos;
    uint8_t  previous_endswitch;
    uint64_t start_time_us; 
} stepper_config_t;
volatile stepper_config_t stepper[MAX_STEPPER_COUNT];



struct __attribute__((packed)) {
    uint8_t report_code;
    uint32_t initial_nanopos;
} stepper_init_report;

void stepper_init() {
	/* Rp2daq allows to control up to 16 independent stepper motors, provided that
	 * each motor has its current driver compatible with Stepstick A4988. For this
	 * driver rp2daq generates two control signals - "dir" and "step" - determining 
	 * the direction and speed of rotation, respectively. The GPIO numbers of these
	 * signals are mandatory parameters.
	 *
	 * Optionally one can provide the "disable" GPIO number which, when connected to 
	 * the "!enable" pin on A4988 will automatically turn off current to save energy.
	 *
	 * Independent of the A4988, rp2daq accepts the "endswitch" GPIO which 
	 * automatically stops the stepper whenever it reaches the minimum or maximum 
	 * position. Having a dedicated end stop is both safety and convenience measure, 
	 * allowing one to easily calibrate the stepper position upon restart. 
	 *
	 * The "inertia" parameter allows for smooth ac-/de-celeration of the stepper, 
	 * preventing it from losing steps at startup even at high rotation speeds. The 
	 * default value is usually OK unless the stepper moves some heavy mass.
	 *
	 * This command only defines constants and initializes GPIO states, but does not 
	 * move the stepper; for this one uses the stepper_move() command. 
	 */
	struct __attribute__((packed)) {
		uint8_t  stepper_number;	// min=0	max=15 
		uint8_t  dir_gpio;			// min=0	max=24
		uint8_t  step_gpio;			// min=0	max=24
		int8_t   endswitch_gpio;	// min=-1	max=24		default=-1
		int8_t   disable_gpio;		// min=-1	max=25		default=-1
		uint32_t inertia;			// min=0	max=10000	default=30
	} * args = (void*)(command_buffer+1);

	uint8_t m = args->stepper_number;
	if (m<MAX_STEPPER_COUNT) {
		stepper[m].dir_gpio     = args->dir_gpio;
		stepper[m].step_gpio    = args->step_gpio;
		stepper[m].endswitch_gpio = args->endswitch_gpio;
		stepper[m].disable_gpio = args->disable_gpio;
		stepper[m].inertia_coef  = max(args->inertia, 1); // prevent div/0 error
		stepper[m].endswitch_ignore = 0;
		stepper[m].previous_endswitch = 0;
		stepper[m].endswitch_expected = 1;

		gpio_init(stepper[m].dir_gpio); gpio_set_dir(stepper[m].dir_gpio, GPIO_OUT);
		gpio_init(stepper[m].step_gpio); gpio_set_dir(stepper[m].step_gpio, GPIO_OUT);
		if (stepper[m].endswitch_gpio >= 0) {
            gpio_init(stepper[m].endswitch_gpio); 
			gpio_set_dir(stepper[m].endswitch_gpio, GPIO_IN);
			gpio_pull_up(stepper[m].endswitch_gpio); }
		if (stepper[m].disable_gpio >= 0) {
			gpio_init(stepper[m].disable_gpio);
			gpio_set_dir(stepper[m].disable_gpio, GPIO_OUT);
        }

		stepper[m].nanopos = NANOPOS_AT_ENDSWITCH; // default position !=0 to allow going back
		stepper[m].target_nanopos = stepper[m].nanopos;  // motor stands still when (re)defined
		//if (!stepper[m].initialized) { // nanopos==0 means motor has not been initialized yet
		//}

		stepper[m].initialized = 1;
	}

	stepper_init_report.initial_nanopos = stepper[m].nanopos;
	prepare_report(&stepper_init_report, sizeof(stepper_init_report), 0,0,0);
}




struct __attribute__((packed)) {
    uint8_t report_code;
	uint64_t timestamp_us;
    uint8_t stepper_number;
    uint8_t endswitch;
    uint32_t nanopos; // TODO
    uint16_t steppers_init_bitmask; // TODO
    uint16_t steppers_moving_bitmask; // TODO
    uint16_t steppers_endswitch_bitmask; // TODO
} stepper_status_report;

void stepper_status() {
	/* Returns the position and endswitch status of the stepper selected by "stepper_number".
	 *
	 * Additionally, returns three 16-bit integers (bitmasks) for all indices 0..15, 
	 * describing if each corresponding stepper was initialized, if it is actively moving and 
	 * if it is currently at endswitch. 
	 *
	 * These bitmasks are particularly useful when multiple steppers are to be synchronized, 
	 * e.g., into a two-dimensional movement. (New set of stepper_move() commands then would 
	 * be issued only when all relevant bits in steppers_moving_bitmask are cleared.)
	 *
     * __Results in one immediate report.__
	 */ 
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
    stepper_status_report.timestamp_us = time_us_64(); 
	prepare_report(&stepper_status_report, sizeof(stepper_status_report), 0, 0, 0);
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
    uint64_t start_time_us; 
    uint64_t end_time_us; 
} stepper_move_report;  // (transmitted when a stepper actually finishes its move)

void mk_tx_stepper_report(uint8_t n)
{
    // Reporting function called when 1) stepper reaches target nanoposition, or 2) endswitch,
    // or when target is current nanopos. 
	stepper_move_report.stepper_number = n;
	stepper_move_report.nanopos = stepper[n].nanopos;
	stepper_move_report.endswitch_ignored = stepper[n].endswitch_ignore;
	stepper_move_report.endswitch_triggered = stepper[n].previous_endswitch;
	stepper_move_report.endswitch_expected = stepper[n].endswitch_expected;

	stepper_move_report.steppers_init_bitmask = 0;
	stepper_move_report.steppers_moving_bitmask = 0;
	stepper_move_report.steppers_endswitch_bitmask = 0;
	stepper_move_report.start_time_us = stepper[n].start_time_us;
	stepper_move_report.end_time_us = time_us_64();
	
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
	}
	prepare_report(&stepper_move_report, sizeof(stepper_move_report), 0, 0, 0);
}

void stepper_move() {
    /* Starts stepping motor movement from current position towards the new position given by "to". The 
     * motor has to be initialized by stepper_init first (please refer to this command for more details on 
     * stepper control). 
     *
     * The units of position are nanosteps, i.e., 1/256 of a microstep. So typically if you have a motor
     * with 1.8 degree/step and your A4988-compatible driver uses 16 microsteps/step, it takes 
     * 360/1.8*256*16 = 819200 nanosteps per turn.
     *
     * The "speed" is in nanosteps per 0.1 ms update cycle; thus setting the speed to 82 turns the motor in 
     * the above example once in second. Note most stepper motors won't turn much faster than 600 RPM.
     *
     * When no callback is provided, this command blocks your program until the movement is finished. 
     * Using asychronous commands one can easily make multiple steppers move at once.
     *
     * The initial and terminal part of the movement are smoothly ac-/de-celerated, however issuing
     * this command to an already moving stepper results in its immediate stopping before it starts 
     * moving smoothly again.
     *
     * __This command results one report after the movement is finished. Thus it may be immediate 
     * or delayed by seconds, minutes or hours, depending on distance and speed. __
     */ 
	struct __attribute__((packed))  {
		uint8_t  stepper_number;		// min=0 max=15
		uint32_t to;					
		uint32_t speed;					// min=1 max=10000
		int8_t  endswitch_ignore;		// min=-1 max=1		default=-1
		int8_t  endswitch_expect;		// min=-1 max=1		default=-1
		int8_t  reset_nanopos;			// min=0 max=1		default=0
	} * args = (void*)(command_buffer+1);

    uint8_t m = args->stepper_number; 
	stepper[m].start_time_us = time_us_64();
	if (args->reset_nanopos) {stepper[m].nanopos = NANOPOS_AT_ENDSWITCH;} // a.k.a. relative movement

	if (args->endswitch_ignore == -1) { // auto-set value
		stepper[m].endswitch_ignore = stepper[m].previous_endswitch;
	} else {		// user-set value
		stepper[m].endswitch_ignore = args->endswitch_ignore;
	}

	if (args->endswitch_expect != -1) 
		stepper[m].endswitch_expected = args->endswitch_expect;

	stepper[m].previous_nanopos      = stepper[m].nanopos; // remember the starting position (for smooth start)
	stepper[m].target_nanopos       = args->to;
	stepper[m].max_nanospeed        = max(args->speed, 1); // if zero, it means motor is idle
   
	// Normally will not report until stepper finishes, which may take some seconds or minutes.
    // An exception is obviously when no movement is necessary, this results in one immediate report:
    if (stepper[m].nanopos == stepper[m].target_nanopos) { mk_tx_stepper_report(m); }
}


inline uint32_t usqrt(uint32_t val) { // fast and terribly inaccurate uint32 square root
// other approaches at https://stackoverflow.com/questions/34187171/fast-integer-square-root-approximation
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
				
				// TODO can be optimized: avoid usqrt if "udiff()^2 > max_nanospeed"
				actual_nanospeed = min(stepper[m].max_nanospeed, 
						usqrt(udiff(stepper[m].nanopos, stepper[m].target_nanopos))*100/stepper[m].inertia_coef + 1);
				actual_nanospeed = min(actual_nanospeed,
						usqrt(udiff(stepper[m].nanopos, stepper[m].previous_nanopos))*100/stepper[m].inertia_coef + 1);

				if (stepper[m].nanopos < stepper[m].target_nanopos) {
				  gpio_put(stepper[m].dir_gpio, 1);
				  new_nanopos = min(stepper[m].nanopos + actual_nanospeed, stepper[m].target_nanopos);
				} else {
				  gpio_put(stepper[m].dir_gpio, 0);
				  new_nanopos = max(stepper[m].nanopos - actual_nanospeed, stepper[m].target_nanopos);
				}

				if (ENDSWITCH_TEST(m)) { // if the stepper triggers end-stop switch
					stepper[m].max_nanospeed = 0; 
					stepper[m].target_nanopos = new_nanopos;  // immediate stop
					stepper[m].previous_endswitch = 1; // remember reason for stopping
					mk_tx_stepper_report(m);
					stepper[m].endswitch_expected = 0;
				} else if (new_nanopos == stepper[m].target_nanopos) { // if the move finishes successfully
					stepper[m].max_nanospeed = 0;
					//stepper[m].target_nanopos = new_nanopos;  // TODO rm?
					stepper[m].previous_endswitch = 0;
					mk_tx_stepper_report(m);
				}

				if (stepper[m].disable_gpio >= 0) gpio_put(stepper[m].disable_gpio, 0);
			} else { // i.e. when stepper is not moving
			  new_nanopos = stepper[m].nanopos;
			  if (stepper[m].disable_gpio >= 0) gpio_put(stepper[m].disable_gpio, 1);
			}

			//for (uint8_t j=0; j< abs((new_nanopos/NANOSTEP_PER_MICROSTEP) -
						//(stepper[m].nanopos/NANOSTEP_PER_MICROSTEP)); j++) {
			// TODO just update a field in steps_to_go[m] array
			// then transmit them at once using gpio_put_masked(mask, value)
			//
			for (uint8_t j=0; 
					j< udiff((new_nanopos/NANOSTEP_PER_MICROSTEP), 
						(stepper[m].nanopos/NANOSTEP_PER_MICROSTEP)); 
					j++) {
				gpio_put(stepper[m].step_gpio, 1); 
				busy_wait_us_32(1);// TODO safety margin, but 50ns pulse seems OK?
				gpio_put(stepper[m].step_gpio, 0);
			}
			stepper[m].nanopos = new_nanopos;
		}
	}
}
