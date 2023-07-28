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
#include "include/gpio.c"
#include "include/adc_builtin.c"
#include "include/pwm.c"
#include "include/stepper.c"
//#include "include/"

// === I/O MESSAGING INFRASTRUCTURE ===
// Note the commands & reports here will be auto-assigned unique numbers according to their 
// order in the following table. This happens at runtime in firmware, and independently,
// at import-time in Python; there is no explicit numbering. Shuffling these lines and
// uploading new recompiled firmware makes no difference in function, excepting message
// numbers.
//
// If a new functionality is added to this firmware, we suggest to start with a copy of 
// e.g. the `identify` command handler. Then don't forget to register this new function 
// along with the report structure in the message_table below.
// The corresponding method in the pythonic interface will then be auto-generated.
 
typedef struct { void (*command_func)(); void (*report_struct); } message_descriptor;
message_descriptor message_table[] = // #new_features: add your command to this table
        {   
                {&identify,			&identify_report},  
                {&gpio_out,			&gpio_out_report},
                {&gpio_in,			&gpio_in_report},
                {&gpio_on_change,	&gpio_on_change_report},
                {&adc,				&adc_report},
                {&pwm_configure_pair, &pwm_configure_pair_report},
                {&pwm_set_value,	&pwm_set_value_report},
                {&stepper_init,		&stepper_init_report},
                {&stepper_status,	&stepper_status_report},
                {&stepper_move,		&stepper_move_report},
			 // {handler fn ref,	report struct instance ref}
        };  


inline void rx_next_command() {
    int packet_size;
    uint8_t packet_data;
    message_descriptor message_entry;
    command_buffer[0] = 0x00;

    // Tries to pull a byte from the USB built-in buffer. If not available, just quit.
    if ((packet_size = getchar_timeout_us(0)) == PICO_ERROR_TIMEOUT) {
        return; 
    } else {
        // If a byte is present, it encodes the length of the respective command. Get it entire.
        // The next byte is the command ID and the following bytes are the associated data bytes
        // (small todo: allow 16bit packet_size e.g. for DAC output)
        for (int i = 0; i < packet_size; i++) {
            while ((packet_data = (uint8_t) getchar_timeout_us(0)) == PICO_ERROR_TIMEOUT) 
                busy_wait_us_32(1);
            command_buffer[i] = packet_data;
        }

        // Check for unknown message - this should never happen. (todo: report it)
        if (command_buffer[0] >= ARRAY_LEN(message_table)) 
            return; 

        // Seek for the associated command handler function, implemented in the include/*.c files
        message_entry = message_table[command_buffer[0]];
        message_entry.command_func(command_buffer);
    }
}

inline void tx_next_report() {
    fwrite(&txbuf[TXBUF_LEN*txbuf_tosend], txbuf_struct_len[txbuf_tosend], 1, stdout);
    if (txbuf_data_len[txbuf_tosend]) {
        fwrite(txbuf_data_ptr[txbuf_tosend], txbuf_data_len[txbuf_tosend], 1, stdout);
    }
    fflush(stdout);
    if (txbuf_data_write_lock_ptr[txbuf_tosend]) {
        *txbuf_data_write_lock_ptr[txbuf_tosend] = 0; // clear buffer lock to allow writing
    }
}


// Generic routine that should be used for scheduling any report (i.e. ordinary outgoing message)
// for being transmitted as soon as possible. When make_copy_of_data=0, it is done within 1 us. 
void prepare_report(void* headerptr, uint16_t headersize, void* dataptr, 
		uint16_t datasize, uint8_t make_copy_of_data) {
    prepare_report_wrl(headerptr, headersize, dataptr, datasize, make_copy_of_data, 0x0000); 
}

void prepare_report_wrl(void* headerptr, uint16_t headersize, void* dataptr, 
		uint16_t datasize, uint8_t make_copy_of_data, uint8_t* data_write_lock_ptr) {
	while (txbuf_lock); txbuf_lock=1;
	memcpy(&txbuf[TXBUF_LEN*txbuf_tofill], headerptr, headersize);
	if (make_copy_of_data) { // if data copied after the header (length must be <= TXBUF_LEN)
		// (small todo: check that (headersize + datasize) <= TXBUF_LEN, report error otherwise)
		txbuf_struct_len[txbuf_tofill] = headersize + datasize;
		txbuf_data_ptr[txbuf_tofill] = 0x0000;
		txbuf_data_len[txbuf_tofill] = 0;
		txbuf_data_write_lock_ptr[txbuf_tofill] = 0x0000;
		memcpy(&txbuf[TXBUF_LEN*txbuf_tofill+headersize], dataptr, datasize);
	} else {  // if data are referenced only; does not need a memory copy to be made, but needs 
                // them to stay there until sent, as ensured by the write_lock
		txbuf_struct_len[txbuf_tofill] = headersize;
		txbuf_data_ptr[txbuf_tofill] = dataptr;
		txbuf_data_len[txbuf_tofill] = datasize;
		txbuf_data_write_lock_ptr[txbuf_tofill] = data_write_lock_ptr;
	}
	txbuf_tofill = (txbuf_tofill + 1) % TXBUF_COUNT;
	txbuf_lock=0;
    // (small fixme: there is an unnecessary redundancy between XYZ_report._data_count XYZ_report._data_bitwidth
    //     and uint16_t datasize,)
}



// === CORE0 AND CORE1 BUSY ROUTINES ===

volatile uint8_t timer10khz_triggered = 0;
bool timer10khz_update_routine(struct repeating_timer *t) {
	timer10khz_triggered = 1;  // small todo: is there some more elegant semaphore for this ?
	return true;
}


void core1_main() { // CPU core1 takes care of real-time tasks
	// SDK: "Care should be taken with calling C library functions from both cores simultaneously 
	// as they are generally not designed to be thread safe. You can use the mutex_ API provided by 
	// the SDK in the pico_sync"
    while (true) {
        if (iADC_DMA_start_pending && !(iADC_buffers[iADC_buffer_choice].write_lock)) {
            iADC_DMA_start(1);
        }

		if (timer10khz_triggered) {
			timer10khz_triggered = 0;
			stepper_update();
		}
    }
}

int main() {  // CPU core0 can be fully occupied with USB communication
	// Global initialization
    bi_decl(bi_program_description("RP2 as universal platform for data acquisition and experiment automation"));
    bi_decl(bi_program_url("https://github.com/FilipDominec/rp2daq"));
    bi_decl(bi_1pin_with_name(PICO_DEFAULT_LED_PIN, "Diagnostic LED, other pins assigned run-time"));

    set_sys_clock_khz(250000, false); // reasonable overclock with safe margin
    stdio_set_translate_crlf(&stdio_usb, false); // crucial for correct binary data transmission
    stdio_init_all();

	gpio_init(PICO_DEFAULT_LED_PIN); gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
	gpio_init(DEBUG_PIN); gpio_set_dir(DEBUG_PIN, GPIO_OUT); // DEBUG
	gpio_init(DEBUG2_PIN); gpio_set_dir(DEBUG2_PIN, GPIO_OUT); // DEBUG

    multicore_launch_core1(core1_main); 

    // auto-assign report codes, note 1st byte of any report_struct has to be its code
    for (uint8_t report_code = 0; report_code < ARRAY_LEN(message_table); report_code++) {
		*((uint8_t*)(message_table[report_code].report_struct)) = report_code; 
    }


	// Setup routines for subsystems - ran once to initialize hardware & constants
	
	struct repeating_timer timer;
	long usPeriod = -100;  // negative value means "start to start" timing
	add_repeating_timer_us(usPeriod, timer10khz_update_routine, NULL, &timer);

	iADC_DMA_setup();

	BLINK_LED_US(5000);
	busy_wait_us_32(100000); 
	BLINK_LED_US(5000);

	while (true)  // busy loop on core0 handles mostly communication
	{ 
		rx_next_command();

		if (txbuf_tosend != txbuf_tofill) {
            tx_next_report();
			txbuf_tosend = (txbuf_tosend + 1) % TXBUF_COUNT;

			// Notes: fwrite() seems most appropriate for bulk data transfers; it  blocks code 
            // execution, but transmits >850 kBps (~limit of USB 1.1) for message length >50 B 
            // (or, if PC rejects data, fwrite returns in <40us). In contrast, printf() is not 
            // for binary data; putc() is slow; puts() is disguised putc in a loop.
            //
            // (small todo: if rp2daq.py not running in computer, fwrite returns rejected reports, 
            // adapt into error messaging?)
		}
	}
}




