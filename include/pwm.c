struct __attribute__((packed)) {
    uint8_t report_code;
} pwm_configure_pair_report;

void pwm_configure_pair() {
    /* Sets frequency for a pair of pins ("slice")
     * 
     * To control usual small servos, set `wrap=65536, clkdiv=20` to get 190 Hz
     * cycle. Value of 10000 (0.8ms pulse) then turns servo near its minimum value,
     * and value of 30000 (2.4ms pulse) turns it near maximum value. YMMV.
     * (see https://en.wikipedia.org/wiki/Servo_control)
     *
     * When PWM is smoothed to generate analog signal (like a poor man's DAC),
     * clkdiv=1 will yield best results; the wrap value can be reduced to get 
     * faster cycle, thus more efficient filtering & better time resolution.
     *
     * Note while almost all GPIO pins can be enabled for PWM output, there are
     * only 16 channels (e.g. pins 0, 16 will have the same value, if PWM output 
     * enabled), and there are only 8 slices (e.g. pins 0, 1, 16 and 17 sharing 
     * also the same clkdiv, wrap and other config)
     * 
     * __This command results in one near-immediate report.__
     */ 
	struct __attribute__((packed)) {
		uint8_t pin;				// default=0		min=0		max=25
		uint16_t wrap_value;		// default=999		min=1		max=65535
		uint16_t clkdiv;			// default=1		min=1		max=255
		uint8_t clkdiv_int_frac;	// default=0		min=0		max=15
	} * args = (void*)(command_buffer+1);

	// TODO remember settings - reconfig only if undefined, or if they change
	
    uint slice_num = pwm_gpio_to_slice_num(args->pin);
    pwm_config config = pwm_get_default_config();
    pwm_init(slice_num, &config, true);
	pwm_set_wrap(slice_num, args->wrap_value);
    pwm_set_clkdiv_int_frac(slice_num, args->clkdiv, args->clkdiv_int_frac);

	tx_header_and_data(&pwm_configure_pair_report, sizeof(pwm_configure_pair_report), 0, 0, 0);
}



struct __attribute__((packed)) {
    uint8_t report_code;
} pwm_set_value_report;

void pwm_set_value() {
    /* Quickly sets duty cycle for one pin
     * 
     * It is assumed this pin already was configured by `pwm_configure_pair()`.
     * 
     * __This command results in one near-immediate report.__
     */ 
	struct __attribute__((packed)) {
		uint8_t pin;				// default=0		min=0		max=25
		uint16_t value;				// default=0		min=0		max=65535
	} * args = (void*)(command_buffer+1);

	// TODO remember settings - check if pin is defined as pwm, if not, init it here
	
    gpio_set_function(args->pin, GPIO_FUNC_PWM);

    pwm_set_gpio_level(args->pin, args->value); 
    //pwm_set_gpio_level(args->pin, 1025); 

	tx_header_and_data(&pwm_set_value_report, sizeof(pwm_set_value_report), 0, 0, 0);
}

// TODO facilitate further PWM uses for accurate timer, pulse counter, and freq & duty 
//		cycle measurement
