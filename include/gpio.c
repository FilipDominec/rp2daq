
struct __attribute__((packed)) {
    uint8_t report_code; // identifies command & report type
} gpio_out_report;

void gpio_out() {
    /* Changes the output state of the specified *gpio*, i.e. general-purpose input/output pin. 
     * Depending on the "value=0" or "value=1" parameter, it connects the pin directly to 0 V,
     * or 3.3 V, respectively.  
     *
     * This overrides previous high-impedance or pull-up/down state of the pin. 
     *
     * __This command results in single, meaningless, near-immediate report.__
     */
	struct  __attribute__((packed)) {
		uint8_t gpio;		// min=0 max=25          The number of the gpio to be configured
		uint8_t value;		// min=0 max=1           Output value (i.e. 0 or 3.3 V) for high_z = 0
	} * args = (void*)(command_buffer+1);
	gpio_init(args->gpio);  // is it necessary to avoid clash e.g. with PWM output

    gpio_put(args->gpio, args->value);
    gpio_set_dir(args->gpio, GPIO_OUT);
	prepare_report(&gpio_out_report, sizeof(gpio_out_report), 0, 0, 0);
}



struct __attribute__((packed)) {
    uint8_t report_code; // identifies command & report type
} gpio_pull_report;

void gpio_pull() {
    /* Changes the output state of the specified *gpio*, i.e. general-purpose input/output pin. 
     *
     * Depending on the "pull=0" or "pull=1" parameter, it engages one of the built-in cca. 
     * 50 kOhm resistors to pull the pin towards 0 or 3.3 V, respectively.  
     *
     * This overrides previous direct-output or high-impedance state of the pin. 
     *
     * __This command results in single, meaningless, near-immediate report.__
     */
	struct  __attribute__((packed)) {
		uint8_t gpio;		// min=0 max=25          The number of the gpio to be configured
		uint8_t value;		// min=0 max=1           Output value (i.e. 0 or 3.3 V) for high_z = 0
	} * args = (void*)(command_buffer+1);
	gpio_init(args->gpio);  // is it necessary to avoid clash e.g. with PWM output

    if (args->value) {
        gpio_pull_up(args->gpio); 
    } else {
        gpio_pull_down(args->gpio); 
    };

    gpio_set_dir(args->gpio, GPIO_IN);

	prepare_report(&gpio_pull_report, sizeof(gpio_pull_report), 0, 0, 0);
}



struct __attribute__((packed)) {
    uint8_t report_code; // identifies command & report type
} gpio_highz_report;

void gpio_highz() {
    /* Changes the output state of the specified *gpio*, i.e. general-purpose input/output pin. 
     *
     * The pin will become "high-impedance", or "floating", disconnected from any voltage 
     * supply or drain.
     *
     * This overrides previous direct-output or pull-up/down state of the pin. 
     *
     * __This command results in single, meaningless, near-immediate report.__
     */
	struct  __attribute__((packed)) {
		uint8_t gpio;		// min=0 max=25          The number of the gpio to be configured
	} * args = (void*)(command_buffer+1);
	gpio_init(args->gpio);  // is it necessary to avoid clash e.g. with PWM output

    gpio_set_dir(args->gpio, GPIO_IN);
    gpio_disable_pulls(args->gpio); 

	prepare_report(&gpio_highz_report, sizeof(gpio_highz_report), 0, 0, 0);
}



struct __attribute__((packed)) {
    uint8_t report_code;
    uint8_t gpio;
    uint8_t value;
} gpio_in_report;

void gpio_in() {
    /* Returns the digital state of a gpio pin. 
     *
     * Typically used when the pin is set to high-z or pull-up/down.
     * 
	 * > [!CAUTION]
	 * > The maximum allowed voltage at any pin is 3.3V.
	 *
     * __This command results in single near-immediate report.__
     */
	struct  __attribute__((packed)) {
		uint8_t gpio;		// min=0 max=25
	} * args = (void*)(command_buffer+1);
	gpio_in_report.gpio = args->gpio;
	gpio_in_report.value = gpio_get(args->gpio);
	prepare_report(&gpio_in_report, sizeof(gpio_in_report), 0, 0, 0);
}



struct __attribute__((packed)) {
    uint8_t report_code;
    uint32_t gpio;
    uint32_t events;
	uint64_t time_us;
} gpio_on_change_report;

void gpio_on_change_IRQ(uint gpio, uint32_t events) {
	gpio_on_change_report.time_us = time_us_64();
	gpio_on_change_report.gpio = gpio;
	gpio_on_change_report.events = events;
	//BLINK_LED_US(100000);
	// TODO this really should be debounced; test with resistors

	prepare_report(&gpio_on_change_report, sizeof(gpio_on_change_report), 0, 0, 0);
}

void gpio_on_change() {
    /* Sets up a gpio to issue a report every time the gpio changes its state. This is sensitive to both external and internal events.
     * 
     * __Fixme__: in current firmware, edge events cannot be turned off! 
     * 
     * __This command potentially results in multiple later reports. Note that input signal over 10kHz may result in some events not being reported.__
     */
	struct  __attribute__((packed)) {
		uint8_t gpio;		// min=0 max=25 gpio specification
		uint8_t on_rising_edge;  // min=0 max=1 default=1 Reports on gpio going from logical 0 to 1
		uint8_t on_falling_edge; // min=0 max=1 default=1 Reports on gpio going from logical 1 to 0
	} * args = (void*)(command_buffer+1);
	
	uint8_t edge_mask = 0;

	if (args->on_rising_edge) edge_mask |= GPIO_IRQ_EDGE_RISE;
	if (args->on_falling_edge) edge_mask |= GPIO_IRQ_EDGE_FALL;
    
    // FIXME: edge_mask=0 does not stop acq?
    //          try gpio_remove_raw_irq_handler()
	gpio_set_irq_enabled_with_callback(args->gpio, edge_mask, true, &gpio_on_change_IRQ);
	//if (edge_mask == 0) {unregister irq} // TODO 
        //gpio_set_irq_enabled_with_callback(args->gpio, edge_mask, false, &gpio_on_change_IRQ);

}







//• enum gpio_slew_rate { GPIO_SLEW_RATE_SLOW = 0, GPIO_SLEW_RATE_FAST = 1 }
//Slew rate limiting levels for GPIO outputs Slew rate limiting increases the minimum rise/fall time when a GPIO
//output is lightly loaded, which can help to reduce electromagnetic emissions.
//• enum
//gpio_drive_strength { GPIO_DRIVE_STRENGTH_2MA = 0, GPIO_DRIVE_STRENGTH_4MA = 1, GPIO_DRIVE_STRENGTH_8MA = 2,
//GPIO_DRIVE_STRENGTH_12MA = 3 }

// TODO multiple gpio setting at once: with gpio_get_all(), gpio_xor_mask() etc.


