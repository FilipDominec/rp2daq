struct __attribute__((packed)) {
    uint8_t report_code;
} pwm_configure_pair_report;

void pwm_configure_pair() {
	struct __attribute__((packed)) {
		uint8_t pin;				// default=0		min=0		max=25
		uint16_t wrap_value;		// default=999		min=1		max=65535
		uint16_t clkdiv;			// default=1		min=1		max=65535
		uint8_t clkdiv_int_frac;	// default=0		min=0		max=9
		// note: clkdiv of 833 yields 300 Hz (at wrap=999) - suitable for servos
		//		clkdiv of   1 is a good default for most other uses
	} * args = (void*)(command_buffer+1);

	// TODO remember settings - reconfig only on change (or if undefined)
    uint slice_num = pwm_gpio_to_slice_num(args->pin);
    pwm_config config = pwm_get_default_config();
    pwm_init(slice_num, &config, true);
	pwm_set_wrap(slice_num, args->wrap_value);
    pwm_set_clkdiv_int_frac(slice_num, args->clkdiv, args->clkdiv_int_frac);
    //pwm_set_gpio_level(args->pin, args->value_A); value re-set needed?

	tx_header_and_data(&pwm_configure_pair_report, sizeof(pwm_configure_pair_report), 0, 0, 0);
}



struct __attribute__((packed)) {
    uint8_t report_code;
} pwm_set_value_report;

void pwm_set_value() {
	struct __attribute__((packed)) {
		uint8_t pin;				// default=0		min=0		max=25
		uint16_t value;				// default=0		min=0		max=65535
	} * args = (void*)(command_buffer+1);

	// TODO remember settings - if pin not defined as pwm
    gpio_set_function(args->pin, GPIO_FUNC_PWM);

	tx_header_and_data(&pwm_set_value_report, sizeof(pwm_set_value_report), 0, 0, 0);
}
