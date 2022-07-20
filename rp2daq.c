#include <stdio.h>
#include <string.h>
#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <pico/binary_info.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>


#include "rp2daq.h"
#include "include/identify.c"
#include "include/pin_out.c"
#include "include/adc_internal.c"
#include "include/pwm.c"
//#include "include/stepper.c"
//#include "include/"

// === I/O MESSAGING INFRASTRUCTURE ===
// If a new functionality is added, please make a copy of any of following command 
// handlers and don't forget to register this new function in the command_table below;
// The corresponding method in the pythonic interface will then be auto-generated upon RP2DAQ restart
 
typedef struct { void (*command_func)(); void (*report_struct); } message_descriptor;
message_descriptor message_table[] = // #new_features: add your command to this table
        {   
                {&identify,			&identify_report},  
                {&pin_out,			&pin_out_report},
                {&internal_adc,		&internal_adc_report},
                {&pwm_configure_pair, &pwm_configure_pair_report},
                {&pwm_set_value,	&pwm_set_value_report},
			 // {command handler,	report struct instance}
        };  


void get_next_command() {
    int packet_size;
    uint8_t packet_data;
    message_descriptor message_entry;
    command_buffer[0] = 0x00;

    // Get the number of bytes of the command packet.
    // The next byte is the command ID and the following bytes
    // are the associated data bytes
    if ((packet_size = getchar_timeout_us(0)) == PICO_ERROR_TIMEOUT) {
        return; // TODO allow 16bit packet_size e.g. for DAC output
    } else {
        // get the rest of the packet
        for (int i = 0; i < packet_size; i++) {
            while ((packet_data = (uint8_t) getchar_timeout_us(0)) == PICO_ERROR_TIMEOUT) {
                busy_wait_us_32(1);
            }
            command_buffer[i] = packet_data;
        }
        if (command_buffer[0] >= ARRAY_LEN(message_table)) {
            return; // todo: report overflow
        }

        message_entry = message_table[command_buffer[0]];
        message_entry.command_func(command_buffer);
    }
}


void tx_header_and_data(void* headerptr, uint16_t headersize, void* dataptr, 
		uint16_t datasize, uint8_t make_copy_of_data) {
	while (txbuf_lock); txbuf_lock=1;
	memcpy(&txbuf[TXBUF_LEN*txbuf_tofill], headerptr, headersize);
	if (make_copy_of_data) { // short data appended after the header (256B limit)
		txbuf_struct_len[txbuf_tofill] = headersize + datasize;
		txbuf_data_ptr[txbuf_tofill] = 0x0000;
		txbuf_data_len[txbuf_tofill] = 0;
		memcpy(&txbuf[TXBUF_LEN*txbuf_tofill+headersize], dataptr, datasize);
	} else {  // data referenced; saves memory copy but needs persistent array until sent
		txbuf_struct_len[txbuf_tofill] = headersize;
		txbuf_data_ptr[txbuf_tofill] = dataptr;
		txbuf_data_len[txbuf_tofill] = datasize;
	}
	txbuf_tofill = (txbuf_tofill + 1) % TXBUF_COUNT;
	txbuf_lock=0;
}



// === CORE0 AND CORE1 BUSY ROUTINES ===

void core1_main() { // busy loop on second CPU core takes care of real-time tasks
    while (true) {
    }
}

int main() {
	// Global initialization
    bi_decl(bi_program_description("RP2 as universal platform for data acquisition and experiment automation"));
    bi_decl(bi_program_url("https://github.com/FilipDominec/rp2daq"));
    bi_decl(bi_1pin_with_name(LED_PIN, "Diagnostic LED, other pins assigned run-time"));

    set_sys_clock_khz(250000, false); // reasonable overclock with safe margin
    stdio_set_translate_crlf(&stdio_usb, false); // crucial for correct binary data transmission
    stdio_init_all();

	gpio_init(LED_PIN); gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_init(DEBUG_PIN); gpio_set_dir(DEBUG_PIN, GPIO_OUT); // DEBUG
	gpio_init(DEBUG2_PIN); gpio_set_dir(DEBUG2_PIN, GPIO_OUT); // DEBUG

    multicore_launch_core1(core1_main); 

    // auto-assign reports to corresponding commands, note 1st byte of any report_struct has to be its code
    for (uint8_t report_code = 0; report_code < ARRAY_LEN(message_table); report_code++) {
		*((uint8_t*)(message_table[report_code].report_struct)) = report_code; 
    }

	// Setup routines for subsystems - ran once to initialize hardware & constants
	iADC_DMA_setup();

	BLINK_LED_US(5000);
	busy_wait_us_32(100000); 
	BLINK_LED_US(5000);

	while (true)  // busy loop on core0 handles mostly communication
	{ 
		get_next_command();

		if (txbuf_tosend != txbuf_tofill) {
			gpio_put(DEBUG2_PIN, 1); 
			// Notes: printf() is not for binary data; putc() is slow; puts() is disguised putc
			// fwrite blocks code execution, but transmits >850 kBps (~limit of USB 1.1)
			// for message length >50 B (or, if PC rejects data, fwrite returns in <40us)
			fwrite(&txbuf[TXBUF_LEN*txbuf_tosend], txbuf_struct_len[txbuf_tosend], 1, stdout);
			if (txbuf_data_len[txbuf_tosend]) {
				fwrite(txbuf_data_ptr[txbuf_tosend], txbuf_data_len[txbuf_tosend], 1, stdout);
			}
			fflush(stdout);
			txbuf_tosend = (txbuf_tosend + 1) % TXBUF_COUNT;
			gpio_put(DEBUG2_PIN, 0); 
		}
	}
}




