struct __attribute__((packed)) {
    uint8_t report_code;
    uint16_t _data_count; 
    uint8_t _data_bitwidth;
    uint8_t channel_mask;
    uint16_t blocks_to_send;
} pwm_set_report;

void pwm_set() {
	struct __attribute__((packed)) {
		uint8_t slice;				// default=0		min=0		max=31
		uint8_t value_A;			// default=0		min=0		max=1
		uint16_t ;					// default=1000		min=1		max=8192
		uint16_t blocks_to_send;	// default=1		min=0		
		uint16_t clkdiv;			// default=96		min=96		max=65535
	} * args = (void*)(command_buffer+1);

    gpio_set_function(args->pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(args->pin);
    pwm_config config = pwm_get_default_config();
    pwm_init(slice_num, &config, true);
	pwm_set_wrap(slice_num, args->wrap);
    pwm_set_clkdiv_int_frac(slice_num, args->clkdiv, args->clkdiv_decimal);  //max 255
    pwm_set_gpio_level(args->pin, args->value_A);
}
