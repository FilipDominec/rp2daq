// TODO Try how many steppers can really be controlled (e.g. when ADC runs at 500 kSps...)
// TODO Disperse motor service routines consecutively in time (e.g. change 10kHz timer to 80kHz ?)




struct __attribute__((packed)) {
    uint8_t report_code;
    int32_t initial_nanopos;  // This is the nanoposition the stepper was initialized to; always 0 in current firmware.
} stepper_init_report;

void stepper_init() {
	/* Rp2daq allows to control up to 16 independent stepper motors, provided that
	 * each motor has its current driver compatible with Stepstick A4988. For this
	 * driver rp2daq generates two control signals - "dir" and "step" - determining 
	 * the direction and speed of rotation, respectively. The GPIO numbers of these
	 * signals are mandatory parameters.
	 *
	 * This command only defines constants and initializes the GPIO states, but does not 
	 * move the stepper; see `stepper_move()` for getting it moving. 
	 *
	 * > [!CAUTION]
	 * > Never disconnect a stepper from Stepstick when powered. Interrupting the 
	 * > current in its coils results in a voltage spike that may burn the driver chip.
	 *
	 */
	struct __attribute__((packed)) {
		uint8_t  stepper_number;	// min=0	max=15    The number of the stepper to be configured. 
		uint8_t  dir_gpio;			// min=0	max=24    Direction-controlling output GPIO pin. 
		uint8_t  step_gpio;			// min=0	max=24    Microstep-advancing output GPIO pin.
		int8_t   endswitch_gpio;	// min=-1	max=24	default=-1     GPIO that, once shorted to ground, can 
            // automatically stop the stepper whenever it reaches the minimum (or maximum) allowed
            // position. The end stop switch is both safety and convenience measure, 
            // allowing one to easily calibrate the stepper position upon restart. More details
            // are with the stepper_move() command.
		int8_t   disable_gpio;		// min=-1	max=25		default=-1     GPIO number that may be connected to 
	        // the "!enable" pin on A4988 module - will automatically turn off current to save energy when
            // the stepper is not moving. Note however it also loses its holding force. 
		uint32_t inertia;			// min=0	max=10000	default=30  Allows for smooth acc-/deceleration of the stepper, preventing it from losing steps at startup even at high rotation speeds. The default value is usually OK unless the stepper moves some heavy mass.
	} * args = (void*)(command_buffer+1);

	uint8_t m = args->stepper_number;
	if (m<MAX_STEPPER_COUNT) {
		stepper[m].dir_gpio     = args->dir_gpio;
		stepper[m].step_gpio    = args->step_gpio;
		stepper[m].endswitch_gpio = args->endswitch_gpio;
		stepper[m].disable_gpio = args->disable_gpio;
		stepper[m].inertia_coef  = max(args->inertia, 1); // prevent div/0 error
		stepper[m].endswitch_sensitive = 0;
		stepper[m].reset_nanopos_at_endswitch = 0;
		stepper[m].move_reached_endswitch = 0;

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

		stepper[m].nanopos = NANOPOS_DEFAULT; 
		stepper[m].target_nanopos = stepper[m].nanopos;  // motor stands still when (re)defined, (todo) is it useful?
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
    int32_t nanopos;
    uint16_t steppers_init_bitmask;
    uint16_t steppers_moving_bitmask;
    uint16_t steppers_endswitch_bitmask;
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
	stepper_status_report.endswitch = (ENDSWITCH_TEST(m)); 
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

    stepper_status_report.timestamp_us = time_us_64(); 
	prepare_report(&stepper_status_report, sizeof(stepper_status_report), 0, 0, 0);
}







struct __attribute__((packed)) {
    uint8_t report_code;
    uint8_t stepper_number;
    int32_t nanopos;
    uint8_t endswitch_was_sensitive;
    uint8_t endswitch_triggered;
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
	stepper_move_report.endswitch_was_sensitive = stepper[n].endswitch_sensitive;
	stepper_move_report.endswitch_triggered = stepper[n].move_reached_endswitch;

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
			if (stepper[n].move_reached_endswitch) // better copy recent value here (eliminates glitches)
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
	 * > [!TIP]
     * > The units of position are nanosteps, i.e., 1/256 of a microstep. So typically if you have a motor
     * > with 200 steps per turn and your A4988-compatible driver is hard-wired for 16 microsteps/step, it takes 
     * > about a million (200x256x16 = 819200) nanosteps per turn.
     * >
     * > The "speed" is in nanosteps per 0.1 ms update cycle; thus setting speed=82 turns the motor in 
     * > the above example once in second. Setting minimal speed=1 gives 0.732 RPM. Note most stepper motors 
     * > won't be able to turn much faster than 600 RPM.
     *
     * The "endswitch_sensitive_down" option is by default set to 1, i.e., the motor will immediately stop its 
     * movement towards more negative target positions when the end switch pin gets connected to zero. 
     *
     * On the contrary, "endswitch_sensitive_up" is by default set to 0, i.e. the motor will move towards 
     * more positive target positions independent of the end switch pin.
     *
     * Note: The defaults for the two above endswitch-related options assume you installed the endswitch at the 
     * lowest end of the stepper range. Upon reaching the endswitch, the stepper position is typically 
     * calibrated and it is straightforward to move upwards from the endswitch, without any change to the defaults.
     * Alternately, one can swap these two options if the endswitch is mounted on the highest end 
     * of the range. Or one can use different settings before/after the first calibration to allow the motor 
     * going beyond the end-switch(es) - if this is safe.
     *
     * "reset_nanopos_at_endswitch" will reset the position if endswitch triggers the end of the 
     * movement. This is a convenience option for easy calibration of position using the endswitch. 
	 * Note that the nanopos can also be manually reset by re-issuing the `stepper_init()` function. 
     *
     * "relative" if set to true, rp2daq will add the `to` value to current nanopos; movement  
	 * then becomes relative to the position of the motor when the command is issued. 
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
		int32_t to;					
		uint32_t speed;					// min=1 max=10000
		int8_t  endswitch_sensitive_up;		// min=0 max=1	default=0
		int8_t  endswitch_sensitive_down;	// min=0 max=1	default=1
		int8_t  relative;		        // min=0 max=1	default=0
		int8_t  reset_nanopos_at_endswitch;	// min=0 max=1	default=0
	} * args = (void*)(command_buffer+1);

    uint8_t m = args->stepper_number; 
	stepper[m].start_time_us = time_us_64();
	if (args->relative) 
		stepper[m].target_nanopos += args->to;  // i.e. relative movement
	else 	
		stepper[m].target_nanopos = args->to;  // i.e. movement w.r.t. initial zero position
	stepper[m].reset_nanopos_at_endswitch = args->reset_nanopos_at_endswitch; // i.e. calibration

    stepper[m].endswitch_sensitive = (
            (args->endswitch_sensitive_up   && (stepper[m].nanopos < args->to)) ||
            (args->endswitch_sensitive_down && (stepper[m].nanopos > args->to)));


	stepper[m].previous_nanopos     = stepper[m].nanopos; // remember the starting position (for smooth start)
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

inline uint32_t udiff(int32_t a, int32_t b) {  // absolute value of difference
	return (max(a,b)-min(a,b));
}

void stepper_update() {
	for (uint8_t m=0; m<MAX_STEPPER_COUNT; m++) {
		if (stepper[m].initialized) {  // one moving stepper takes ca. 450 CPU cycles (if not messaging) // TODO rm CHECK
			int32_t new_nanopos, actual_nanospeed;

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
					stepper[m].move_reached_endswitch = 1; // remember reason for stopping
					mk_tx_stepper_report(m);
                    if ((stepper[m].move_reached_endswitch) && (stepper[m].reset_nanopos_at_endswitch)) {
                        stepper[m].nanopos = NANOPOS_DEFAULT; 
                        new_nanopos = NANOPOS_DEFAULT;
                        stepper[m].target_nanopos = NANOPOS_DEFAULT;} // useful for end switch calibration
				} else if (new_nanopos == stepper[m].target_nanopos) { // if the move finishes successfully
					stepper[m].max_nanospeed = 0;
					//stepper[m].target_nanopos = new_nanopos;  // TODO rm?
					stepper[m].move_reached_endswitch = 0;
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
