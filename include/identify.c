// It's advisable to not change this message. If you do, make sure you properly adjust 
// the device detection routine in rp2daq.py - this report is the only with hard-wired format.

struct __attribute__((packed)) {    
    uint8_t report_code;
    uint16_t _data_count;
    uint8_t _data_bitwidth;
} identify_report;

void identify() {   
	struct  __attribute__((packed)) { 
        uint8_t flush_buffer;    // min=0 max=1 default=1
	} * args = (void*)(command_buffer+1);

    // This ditches all pending messages, which can make a python script in computer wait
    // for a callback that never comes. 
    // But it ensures the device is correctly detected upon re-connect.
    if (args->flush_buffer)
        txbuf_tosend = txbuf_tofill; 

	uint8_t text[14+16+1] = FIRMWARE_VERSION;
	pico_get_unique_board_id_string(text+14, 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1);
	identify_report._data_count = sizeof(text)-1;
	identify_report._data_bitwidth = 8;
	tx_header_and_data(&identify_report, sizeof(identify_report), &text, sizeof(text)-1, 1);
}
