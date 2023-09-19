
struct __attribute__((packed)) {
    uint8_t report_code; // identifies command & report type
} gpio_out_report;

void gpio_out() {
    /* Changes the output state of the specified *gpio*, i.e. general-purpose input/output pin. The 
	 * optional arguments, if not left default, determine the gpio's multi-state logic behaviour. 
	 * Namely, if *high_z* = 1 and no "pulls" are set, the gpio behaves as if disconnected (the impedance 
	 * is over 1 megaohm). 
     * 
     * This still can be changed by setting *pull_up* or *pull_down*.
     * 
     * __This command results in single near-immediate report.__
     */
	struct  __attribute__((packed)) {
		uint8_t gpio;		// min=0 max=25          The number of the gpio to be configured
		uint8_t value;		// min=0 max=1           Output value (i.e. 0 or 3.3 V) for high_z = 0
		uint8_t high_z; 	// min=0 max=1 default=0 High-impedance (i.e. not sinking nor sourcing current, only through pull-up/-down if set)
		uint8_t pull_up; 	// min=0 max=1 default=0 If high_z = 1, connects to 3.3V through built-in resistor  
		uint8_t pull_down; 	// min=0 max=1 default=0 If high_z = 1, connects to 0V through built-in resistor  
	} * args = (void*)(command_buffer+1);
	gpio_init(args->gpio); 

	if (args->high_z) 
	{	
		gpio_set_dir(args->gpio, GPIO_IN);
		if (args->pull_up) { gpio_pull_up(args->gpio); };
		if (args->pull_up) { gpio_pull_down(args->gpio); };

        // TODO test also gpio_disable_pulls() - RP2 is has, in fact, five-state gpios
        // setting both pulls enables a "bus keep" function, i.e. a weak pull to whatever is current high/low state of GPIO
	} else {
		gpio_put(args->gpio, args->value);
		gpio_set_dir(args->gpio, GPIO_OUT);
	}
    // TODO for outputs: gpio_set_slew_rate(GPIO_SLEW_RATE_SLOW) or GPIO_SLEW_RATE_FAST
    // TODO     and:     gpio_set_drive_strength()
	prepare_report(&gpio_out_report, sizeof(gpio_out_report), 0, 0, 0);
}


// TODO multiple gpio setting at once: with gpio_get_all(), gpio_xor_mask() etc.



struct __attribute__((packed)) {
    uint8_t report_code;
    uint8_t gpio;
    uint8_t value;
} gpio_in_report;

void gpio_in() {
    /* Checks the digital state of a gpio. Most useful if the gpio is configured as high-impedance input.
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

}




