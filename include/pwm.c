struct __attribute__((packed)) {
    uint8_t report_code;
} pwm_configure_pair_report;

void pwm_configure_pair() {
    /* Sets frequency for a "PWM slice", i.e. pair of GPIOs 
     * 
     * To control usual small servos, set `wrap_value=65535, clkdiv=20` to get 190 Hz
     * cycle. Value of 10000 (0.8ms pulse) then turns servo near its minimum value,
     * and value of 30000 (2.4ms pulse) turns it near maximum value. YMMV.
     * (see https://en.wikipedia.org/wiki/Servo_control)
     *
     * When PWM is low-pass filtered to generate analog signal (like a poor man's DAC),
     * clkdiv=1 is recommended as it gives optimum duty-cycle resolution; the wrap value 
     * can be reduced to get faster cycle, thus more efficient filtering & better time resolution.
     *
     * > [!NOTE]
     * > Note while almost all GPIOs can be enabled for PWM output, there are
     * > only 16 channels (e.g. GPIOs 0, 16 will have the same value, if these are enabled for PWM output),
     * > and there are only 8 PWM slices. As a result, GPIOs 0, 1, 16 and 17 share 
     * > also the same clkdiv, wrap and clkdiv_int_frac values. Changing them for one of these 
     * > pins changes it for other, too.)
     * 
     * *This command results in one near-immediate report.*
     */ 
	struct __attribute__((packed)) {
		uint8_t gpio;				// default=0		min=0		max=25    Selected pin for PWM output. Note not all pins are independent, see above. 
		uint16_t wrap_value;		// default=999		min=1		max=65535 Value at which PWM counter resets for a new cycle.
		uint16_t clkdiv;			// default=1		min=1		max=255   Clock divider for PWM.
		uint8_t clkdiv_int_frac;	// default=0		min=0		max=15    Fine tuning of the frequency by clock divider dithering. 
	} * args = (void*)(command_buffer+1);

	// TODO remember settings - reconfig only if undefined, or if they change
	
    uint slice_num = pwm_gpio_to_slice_num(args->gpio);
    pwm_config config = pwm_get_default_config();
    pwm_init(slice_num, &config, true);
	pwm_set_wrap(slice_num, args->wrap_value);
    pwm_set_clkdiv_int_frac(slice_num, args->clkdiv, args->clkdiv_int_frac);

	prepare_report(&pwm_configure_pair_report, sizeof(pwm_configure_pair_report), 0, 0, 0);
}



struct __attribute__((packed)) {
    uint8_t report_code;
} pwm_set_value_report;

void pwm_set_value() {
    /* Quickly sets duty cycle for one GPIO
     * 
     * It is assumed this GPIO already was configured by `pwm_configure_pair()`.
     * 
     * *This command results in one near-immediate report.*
     */ 
	struct __attribute__((packed)) {
		uint8_t gpio;				// default=0		min=0		max=25
		uint16_t value;				// default=0		min=0		max=65535  The counter value at which PWM pin 
                // switches from 1 to 0. For example, set `value` to `wrap_value`//2 (defined by `pwm_configure_pair`) 
                // to achieve a 50% duty cycle.
	} * args = (void*)(command_buffer+1);

	// TODO remember settings - check if GPIO is defined as pwm, if not, init it here
	
    gpio_set_function(args->gpio, GPIO_FUNC_PWM);

    pwm_set_gpio_level(args->gpio, args->value); 

	prepare_report(&pwm_set_value_report, sizeof(pwm_set_value_report), 0, 0, 0);
}

// TODO facilitate further PWM uses for accurate timer, pulse counter, and freq & duty 
//		cycle measurement
