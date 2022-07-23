
struct {
    uint8_t report_code;
} pin_set_report;

void pin_set() {
	struct  __attribute__((packed)) {
		uint8_t pin;		// min=0 max=25
		uint8_t value;		// min=0 max=1
		uint8_t high_z; 	// min=0 max=1 default=0
		uint8_t pull_up; 	// min=0 max=1 default=0
	} * args = (void*)(command_buffer+1);
	gpio_init(args->pin); 

	if (args->high_z) 
	{	
		gpio_set_dir(args->pin, GPIO_IN);
		if (args->pull_up) {
			gpio_pull_up(args->pin);
		} else {
			gpio_pull_down(args->pin);
		}
	} else {
		gpio_put(args->pin, args->value);
		gpio_set_dir(args->pin, GPIO_OUT);
	}

	tx_header_and_data(&pin_set_report, sizeof(pin_set_report), 0, 0, 0);
}



struct {
    uint8_t report_code;
    uint8_t pin;
    uint8_t value;
} pin_get_report;

void pin_get() {
	struct  __attribute__((packed)) {
		uint8_t pin;		// min=0 max=25
	} * args = (void*)(command_buffer+1);
	pin_get_report.pin = args->pin;
	pin_get_report.value = gpio_get(args->pin);
	tx_header_and_data(&pin_get_report, sizeof(pin_get_report), 0, 0, 0);
}
