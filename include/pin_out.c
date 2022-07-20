
struct {
    uint8_t report_code;
} pin_set_report;

void pin_set() {
	struct  __attribute__((packed)) {
		uint8_t n_pin;   // min=0 max=25
		uint8_t value; 	 // min=0 max=1
		uint8_t high_z; 	 // min=0 max=1 default=0
		uint8_t pull_up; 	 // min=0 max=1 default=0
	} * args = (void*)(command_buffer+1);
	gpio_init(args->n_pin); gpio_set_dir(args->n_pin, GPIO_OUT);
    gpio_put(args->n_pin, args->value);

	tx_header_and_data(&pin_set_report, sizeof(pin_set_report), 0, 0, 0);
}
