
struct __attribute__((packed)) {
    uint8_t report_code; // identifies command & report type
} pin_set_report;

void pin_set() {
    /* Changes the output state of the specified *pin*. The optional arguments, if not left default, determine the pin's
     * multi-state logic behaviour. Namely, if *high_z* = 1 and no "pulls" are set, the pin behaves as if 
     * disconnected (the impedance is over 1 megaohm). 
     * 
     * This still can be changed by setting *pull_up* or *pull_down*.
     * 
     * __This command results in single near-immediate report.__
     */
	struct  __attribute__((packed)) {
		uint8_t pin;		// min=0 max=25 The number of the pin to be configured
		uint8_t value;		// min=0 max=1  Output value (i.e. 0 or 3.3 V) for high_z = 0
		uint8_t high_z; 	// min=0 max=1 default=0 High-impedance (i.e. not sinking nor sourcing current)
		uint8_t pull_up; 	// min=0 max=1 default=0 If high_z = 1, connects to 3.3V through built-in resistor  
		uint8_t pull_down; 	// min=0 max=1 default=0 If high_z = 1, connects to 0V through built-in resistor  
	} * args = (void*)(command_buffer+1);
	gpio_init(args->pin); 

	if (args->high_z) 
	{	
		gpio_set_dir(args->pin, GPIO_IN);
		if (args->pull_up) { gpio_pull_up(args->pin); };
		if (args->pull_up) { gpio_pull_down(args->pin); };

        // TODO test also gpio_disable_pulls() - RP2 is has, in fact, five-state pins
        // setting both pulls enables a "bus keep" function, i.e. a weak pull to whatever is current high/low state of GPIO
	} else {
		gpio_put(args->pin, args->value);
		gpio_set_dir(args->pin, GPIO_OUT);
	}
    // TODO for outputs: gpio_set_slew_rate(GPIO_SLEW_RATE_SLOW) or GPIO_SLEW_RATE_FAST
    // TODO     and:     gpio_set_drive_strength()
	tx_header_and_data(&pin_set_report, sizeof(pin_set_report), 0, 0, 0);
}


// TODO multiple pin setting at once: with gpio_get_all(), gpio_xor_mask() etc.



struct __attribute__((packed)) {
    uint8_t report_code;
    uint8_t pin;
    uint8_t value;
} pin_get_report;

void pin_get() {
    /* Checks the digital state of a pin. Most useful if the pin is configured as high-impedance input.
     * 
     * __This command results in single near-immediate report.__
     */
	struct  __attribute__((packed)) {
		uint8_t pin;		// min=0 max=25
	} * args = (void*)(command_buffer+1);
	pin_get_report.pin = args->pin;
	pin_get_report.value = gpio_get(args->pin);
	tx_header_and_data(&pin_get_report, sizeof(pin_get_report), 0, 0, 0);
}




struct __attribute__((packed)) {
    uint8_t report_code;
    uint32_t pin;
    uint32_t events;
} pin_on_change_report;

void pin_on_change_IRQ(uint pin, uint32_t events) {
	pin_on_change_report.pin = pin;
	pin_on_change_report.events = events;
	//BLINK_LED_US(100000);
	// TODO this really should be debounced; test with resistors

	tx_header_and_data(&pin_on_change_report, sizeof(pin_on_change_report), 0, 0, 0);
}

void pin_on_change() {
    /* Sets up a pin to issue a report every time the pin changes its state. This is sensitive to both external and internal events.
     * 
     * __Fixme__: in current firmware, edge events cannot be turned off! 
     * 
     * __This command potentially results in multiple delayed reports. Note that input signal over 10kHz may result in some events being not reported.__
     */
	struct  __attribute__((packed)) {
		uint8_t pin;		// min=0 max=25 Pin specification
		uint8_t on_rising_edge;  // min=0 max=1 default=1 Reports on pin going from logical 0 to 1
		uint8_t on_falling_edge; // min=0 max=1 default=1 Reports on pin going from logical 1 to 0
	} * args = (void*)(command_buffer+1);
	
	uint8_t edge_mask = 0;

	if (args->on_rising_edge) edge_mask |= GPIO_IRQ_EDGE_RISE;
	if (args->on_falling_edge) edge_mask |= GPIO_IRQ_EDGE_FALL;
    
    // FIXME: edge_mask=0 does not stop acq?
    //          try gpio_remove_raw_irq_handler()
	gpio_set_irq_enabled_with_callback(args->pin, edge_mask, true, &pin_on_change_IRQ);
}




