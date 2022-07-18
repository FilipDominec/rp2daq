
struct {    
    uint8_t report_code;
    uint8_t _data_count;
    uint8_t _data_bitwidth;
} identify_report;

void identify() {   
	struct  __attribute__((packed)) { 
	} * args = (void*)(command_buffer+1);

	uint8_t text[14+16+1] = {"rp2daq_220120_"};
	pico_get_unique_board_id_string(text+14, 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1);
	fwrite(text, sizeof(text)-1, 1, stdout);
	fflush(stdout); 
}
