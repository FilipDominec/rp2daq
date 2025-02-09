
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
     * *This command results in single, meaningless, near-immediate report.*
     */
	struct  __attribute__((packed)) {
		uint8_t gpio;		// min=0 max=25          The number of the gpio to be configured
		uint8_t value;		// min=0 max=1           Output value (i.e. 0 or 3.3 V)
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
     * *This command results in single, meaningless, near-immediate report.*
     */
	struct  __attribute__((packed)) {
		uint8_t gpio;		// min=0 max=25          The number of the gpio to be configured
		uint8_t value;		// min=0 max=1           Output value (i.e. 0 or 3.3 V), valid if not set to high-impedance mode.
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
     * *This command results in single, meaningless, near-immediate report.*
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
    uint8_t value; // 1 if pin connected to >2 V; 0 if connected to <1 V 
} gpio_in_report;

void gpio_in() {
    /* Returns the digital state of a gpio pin. 
     *
     * Typically used when the pin is set to high-z or pull-up/down.
     * 
	 * > [!CAUTION]
	 * > The maximum allowed voltage at any pin is 3.3V.
	 *
     * *This command results in single near-immediate report.*
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
    uint32_t gpio;   // The pin at which the event was detected. 
    uint32_t events; // The value of 4 corresponds to falling edge, 8 to rising edge.
	uint64_t time_us; // Timestamp in microseconds.
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
     * *This command potentially results in multiple later reports. Note that input signal of over 10-50kHz may result in some events not being reported.*
     */
	struct  __attribute__((packed)) {
		uint8_t gpio;		// min=0 max=25 gpio specification
		uint8_t on_rising_edge;  // min=0 max=1 default=1 Reports on gpio rising from logical 0 to 1
		uint8_t on_falling_edge; // min=0 max=1 default=1 Reports on gpio falling from logical 1 to 0
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



struct __attribute__((packed)) {
    uint8_t report_code;
} gpio_out_seq_report;

void gpio_out_seq() {
    /* Sets (optionally) multiple outputs at once; optionally sets them multiple times 
	 * in an accurately timed sequence. 
	 *
	 * If you need to change several pins simultaneously (within 1 ns), and/or in 
	 * quite accurate time delay independent on how USB is busy (within 2 us), this 
	 * somewhat complex command offers an advantage over the simpler gpio_out() 
	 * commands (which each take some 2ms). Typically this is necessary for custom 
	 * digital protocols, resistor ladders, charlieplexing etc. 
	 *
	 * The *gpio_mask* and *value* parameters accept bit mask; e.g. if you wish to
	 * set GPIO 0 to logical high and GPIO 4 to logical low, use gpio_mask=1+16 and
	 * value0=1. The following parameters can be all 0, unless one wants to define 
	 * a sequence that changes the GPIOs in time.  
	 *
	 * Not all wait times and values have to be set; the defaults negative value
	 * means they won't be used. 
	 *
     * __Fixme__: the sequence should be given as variable-length data array instead
     * 
     * *This command results in one report when the sequence is finished.*
     */
	struct  __attribute__((packed)) {
		uint32_t gpio_mask;		  // Only *gpio* numbers corresponding to "1" bits be changed
		int32_t value0;     // default=-1 Binary value will be set as the outputs
		int32_t wait_us0;   // default=-1 Microseconds to wait after setting this value
		int32_t value1;     // default=-1 Next binary value, if not negative...
		int32_t wait_us1;   // default=-1 
		int32_t value2;     // default=-1 
		int32_t wait_us2;   // default=-1 
		int32_t value3;     // default=-1 
		int32_t wait_us3;   // default=-1 
		int32_t value4;     // default=-1 
		int32_t wait_us4;   // default=-1 
		int32_t value5;     // default=-1 
		int32_t wait_us5;   // default=-1 
		int32_t value6;     // default=-1 
		int32_t wait_us6;   // default=-1 
		int32_t value7;     // default=-1 
		int32_t wait_us7;   // default=-1 
	} * args = (void*)(command_buffer+1);

	gpio_set_dir_out_masked(args->gpio_mask);
	gpio_put_masked(args->gpio_mask, args->value0);
	// (todo) this is just quick & dirty hack; should be IRQ-driven ... hardware_alarm_set_target()
	if (args->value0 > -1) { gpio_put_masked (args->gpio_mask, args->value0); }
	if (args->wait_us0 > -1) { busy_wait_us_32(args->wait_us0); }
	if (args->value1 > -1) { gpio_put_masked (args->gpio_mask, args->value1); }
	if (args->wait_us1 > -1) { busy_wait_us_32(args->wait_us1); }
	if (args->value2 > -1) { gpio_put_masked (args->gpio_mask, args->value2); }
	if (args->wait_us2 > -1) { busy_wait_us_32(args->wait_us2); }
	if (args->value3 > -1) { gpio_put_masked (args->gpio_mask, args->value3); }
	if (args->wait_us3 > -1) { busy_wait_us_32(args->wait_us3); }

	if (args->value4 > -1) { gpio_put_masked (args->gpio_mask, args->value4); }
	if (args->wait_us4 > -1) { busy_wait_us_32(args->wait_us4); }
	if (args->value5 > -1) { gpio_put_masked (args->gpio_mask, args->value5); }
	if (args->wait_us5 > -1) { busy_wait_us_32(args->wait_us5); }
	if (args->value6 > -1) { gpio_put_masked (args->gpio_mask, args->value6); }
	if (args->wait_us6 > -1) { busy_wait_us_32(args->wait_us6); }
	if (args->value7 > -1) { gpio_put_masked (args->gpio_mask, args->value7); }
	if (args->wait_us7 > -1) { busy_wait_us_32(args->wait_us7); }

	// (todo) 1-3 microsecond jitter (delay) due to other hardware IRQ on this CPU core
	// (todo) sub-microsecond delays: see timing https://forums.raspberrypi.com/viewtopic.php?t=333928
	prepare_report(&gpio_out_seq_report, sizeof(gpio_out_seq_report), 0, 0, 0);
}



//• enum gpio_slew_rate { GPIO_SLEW_RATE_SLOW = 0, GPIO_SLEW_RATE_FAST = 1 }
//Slew rate limiting levels for GPIO outputs Slew rate limiting increases the minimum rise/fall time when a GPIO
//output is lightly loaded, which can help to reduce electromagnetic emissions.
//• enum
//gpio_drive_strength { GPIO_DRIVE_STRENGTH_2MA = 0, GPIO_DRIVE_STRENGTH_4MA = 1, GPIO_DRIVE_STRENGTH_8MA = 2,
//GPIO_DRIVE_STRENGTH_12MA = 3 }

// TODO multiple gpio setting at once: with gpio_get_all(), gpio_xor_mask() etc.


