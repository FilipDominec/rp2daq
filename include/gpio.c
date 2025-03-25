
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

	gpio_init(args->gpio);  // is may be useful to report conflict e.g. with PWM output
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



#define GPIO_OUT_SEQ_MAXLEN 16  // (todo) make this longer when command variable-length data
struct __attribute__((packed)) {
	uint64_t start_timestamp_us;
	volatile int8_t  seq_stage;
	volatile uint8_t seq_len;
	volatile int32_t gpio_mask;
	volatile int32_t value[GPIO_OUT_SEQ_MAXLEN];     // Binary value will be set as the outputs
	volatile int32_t wait_us[GPIO_OUT_SEQ_MAXLEN];   // Microseconds to wait after setting this value
} gpio_out_seq_config;

struct __attribute__((packed)) {
    uint8_t report_code;
	uint64_t start_timestamp_us;
	uint64_t end_timestamp_us;
} gpio_out_seq_report;
// todo timestamps for start/end of seq

int64_t gpio_seq_callback(alarm_id_t id, __unused void *user_data) { 
    // Interrupt service routine for updating the GPIO sequence, initiated by the gpio_out_seq command.
    // Note sub-microsecond timing could be trimmed by busy_wait_at_least_cycles(uint32_t minimum_cycles), 
    // and clock_get_hz(clk_sys), but won't be implemented - busy waiting should be avoided in IRQ.
    // TODO migrate this ISR to the second core which is dedicated to such real-time tasks; hook it to a 
    // busy loop checking for https://forums.raspberrypi.com/viewtopic.php?f=145&t=304201&p=1820770&hilit=Hermannsw+systick#p1822677.
    
	gpio_out_seq_config.seq_stage++;
	if (gpio_out_seq_config.seq_stage >= gpio_out_seq_config.seq_len) { // if beyond last sequence stage
        gpio_out_seq_report.end_timestamp_us = time_us_64(); 
        gpio_out_seq_report.start_timestamp_us = gpio_out_seq_config.start_timestamp_us; 

		prepare_report(&gpio_out_seq_report, sizeof(gpio_out_seq_report), 0, 0, 0);
		return 0;
	} else { 
		//if (gpio_out_seq_config.value[gpio_out_seq_config.seq_stage] >= 0)
        gpio_put_masked(gpio_out_seq_config.gpio_mask, gpio_out_seq_config.value[gpio_out_seq_config.seq_stage]); 
		//return min(-1, -gpio_out_seq_config.wait_us[gpio_out_seq_config.seq_stage]); // negative delay = more accurate timing
		return -gpio_out_seq_config.wait_us[gpio_out_seq_config.seq_stage]; // negative delay = more accurate timing
	};
}


void gpio_out_seq() {
    /* Sets (optionally) multiple GPIO outputs at once; (optionally) sets them 
	 * multiple times in an accurately timed short sequence of bit patterns. 
	 *
	 * If you need to change several pins simultaneously (within 1 ns), and/or in 
	 * quite accurate time delay independent on how USB is busy (within 2 us jitter), 
	 * this somewhat complex command offers an advantage over the simpler gpio_out() 
	 * command (calling a command takes some 2ms). Typically this is necessary for custom 
	 * digital protocols, resistor ladders, charlieplexing etc. 
	 *
	 * The *gpio_mask* and *value* parameters accept bit mask; e.g. if you wish to
	 * set GPIO 0 to logical high and GPIO 4 to logical low, use gpio_mask=1+16 and
	 * value0=1. The following parameters can be all 0, unless one wants to define 
	 * a sequence that changes the GPIOs in time.
	 *
	 * Not all wait times and values have to be set; the defaults (negative values)
	 * mean the unused entries won't be used. 
     * 
     * *This command results in one report when the sequence is finished.*
     */
	struct  __attribute__((packed)) {
		uint32_t gpio_mask;	// Only *gpio* numbers corresponding to "1" bits in mask will be initialized as outputs and changed
		int32_t value0;     // default=-1 min=-1 Binary value will be set as the outputs
		int32_t wait_us0;   // default=-1 min=-1 Microseconds to wait after setting this value
		int32_t value1;     // default=-1 min=-1 Next binary value, used if not negative...
		int32_t wait_us1;   // default=-1 min=-1 
		int32_t value2;     // default=-1 min=-1 
		int32_t wait_us2;   // default=-1 min=-1 
		int32_t value3;     // default=-1 min=-1 
		int32_t wait_us3;   // default=-1 min=-1 
		int32_t value4;     // default=-1 min=-1 
		int32_t wait_us4;   // default=-1 min=-1 
		int32_t value5;     // default=-1 min=-1 
		int32_t wait_us5;   // default=-1 min=-1 
		int32_t value6;     // default=-1 min=-1 
		int32_t wait_us6;   // default=-1 min=-1 
		int32_t value7;     // default=-1 min=-1 
		int32_t wait_us7;   // default=-1 min=-1 
		int32_t value8;     // default=-1 min=-1 
		int32_t wait_us8;   // default=-1 min=-1 
		int32_t value9;     // default=-1 min=-1 
		int32_t wait_us9;   // default=-1 min=-1 
		int32_t value10;    // default=-1 min=-1 
		int32_t wait_us10;  // default=-1 min=-1 
		int32_t value11;    // default=-1 min=-1 
		int32_t wait_us11;  // default=-1 min=-1 
		int32_t value12;    // default=-1 min=-1 
		int32_t wait_us12;  // default=-1 min=-1 
		int32_t value13;    // default=-1 min=-1 
		int32_t wait_us13;  // default=-1 min=-1 
		int32_t value14;    // default=-1 min=-1 
		int32_t wait_us14;  // default=-1 min=-1 
		int32_t value15;    // default=-1 min=-1 
		int32_t wait_us15;  // default=-1 min=-1 
	} * args = (void*)(command_buffer+1);
    // Note: could be extended trivially, adjusting GPIO_OUT_SEQ_MAXLEN. Max cmd len given by RX_BUF_LEN.
    // But better solution is to implement data array also for commands in the future. 

    gpio_out_seq_config.start_timestamp_us = time_us_64(); 
	gpio_out_seq_config.gpio_mask = args->gpio_mask;
	gpio_out_seq_config.seq_len = 0;
	for (uint8_t i=0; i<GPIO_OUT_SEQ_MAXLEN; i++) { // verbatim copy of the command values
		gpio_out_seq_config.value[i] = *((int32_t*)(((int32_t*)(&args->value0)) +(2*i) ));  
		gpio_out_seq_config.wait_us[i] = *((int32_t*)(((int32_t*)(&args->wait_us0)) +(2*i) )); // -2???
		if (gpio_out_seq_config.value[i] != -1) gpio_out_seq_config.seq_len = i+1;
	}
    
	// (re)init the masked GPIOs as output
	gpio_set_dir_out_masked(args->gpio_mask); // FIXME did nothing? 
	for (uint8_t i=0; i<=25; i++) { 
        if (((1<<i) & gpio_out_seq_config.gpio_mask) & (1<<i & ~GPIO_OE)) {
            gpio_init(i);  // this is necessary only if the pin isn't initialized as output yet
            // (otherwise it introduces a mostly harmless sub-microsecond 0 glitch in output)
            gpio_set_dir(i, GPIO_OUT);
        }
    }
    
	//Output the first sequence values
    gpio_out_seq_config.seq_stage = 0;
    if (gpio_out_seq_config.value[0] >= 0) { // TODO check all ill cases like this
        gpio_put_masked(args->gpio_mask, gpio_out_seq_config.value[0]); 
    }
    add_alarm_in_us(gpio_out_seq_config.wait_us[0], 
            gpio_seq_callback,  // launch next IRQ update
            NULL,  // no user data needed
            true); // ensures the alarm IRQ chain does not break on delay (and report is always sent)
	//};

	// A report will be sent from the callback function when the sequence ends.  
	
}




