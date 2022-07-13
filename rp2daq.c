#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "pico/unique_id.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

#define TUD_OPT_HIGH_SPEED (1)
//#define CFG_TUD_CDC_EP_BUFSIZE 256 // legacy; needs to go into tusb_config.h that is being used

#define VERSION ("220122")

#define LED_PIN 25
#define DEBUG_PIN 4
#define DEBUG2_PIN 5
#define BLINK_LED_US(duration) gpio_put(LED_PIN, 1); busy_wait_us_32(duration); gpio_put(LED_PIN, 0); 

#define ARRAY_LEN(arr)   (sizeof(arr)/sizeof((arr)[0]))

uint8_t command_buffer[1024];

#define TXBUF_LEN 256    // report headers (and shorter data payloads) are staged here to be sent
#define TXBUF_COUNT 8    // up to 8 reports can be quickly scheduled for tx if USB is busy
uint8_t  txbuf[(TXBUF_LEN*TXBUF_COUNT)];
uint16_t txbuf_struct_len[TXBUF_COUNT];
void*    txbuf_data_ptr[TXBUF_COUNT];
uint32_t txbuf_data_len[TXBUF_COUNT];
uint8_t  txbuf_tofill, txbuf_tosend;
volatile uint8_t txbuf_lock;

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





// === INCOMING COMMAND HANDLERS AND OUTGOING REPORTS ===
// @new_features: If a new functionality is added, please make a copy of any of following command 
// handlers and don't forget to register this new function in the command_table below;
// The corresponding method in the pythonic interface will then be auto-generated upon RP2DAQ restart



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






struct {
    uint8_t report_code;
    uint8_t tmp;
    uint16_t tmpH;
    uint8_t _data_count;
    uint8_t _data_bitwidth;
} test_report;

void test() {
	struct  __attribute__((packed)) {
		uint8_t p,c; 	
	} * args = (void*)(command_buffer+1);

    test_report.tmp = 42;
    test_report.tmpH = 4200;
	test_report._data_count = 30;
	test_report._data_bitwidth = 8;
	
	uint8_t data[14+16+1] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', };
	data[args->p] = args->c; // for messaging DEBUG only

	tx_header_and_data(&test_report, sizeof(test_report), 
			&data, test_report._data_count * test_report._data_bitwidth/8,
			1);

}



struct {
    uint8_t report_code;
} pin_out_report;

void pin_out() {
	struct  __attribute__((packed)) {
		uint8_t n_pin;   // min=0 max=25
		uint8_t value; 	 // min=0 max=1
	} * args = (void*)(command_buffer+1);
	gpio_init(args->n_pin); gpio_set_dir(args->n_pin, GPIO_OUT);
    gpio_put(args->n_pin, args->value);

	// testing only busy_wait_us_32(97000);
    //fwrite(&pin_out_report, sizeof(pin_out_report), 1, stdout);
	//fflush(stdout); 
}



struct __attribute__((packed)) {
    uint8_t report_code;
    uint16_t _data_count; 
    uint8_t _data_bitwidth;
    uint8_t channel_mask;
    uint16_t blocks_to_send;
} internal_adc_report;

#include "include/adc_internal.c"

void internal_adc() {
	struct __attribute__((packed)) {
		uint8_t channel_mask;		// default=1		min=0		max=31
		uint8_t infinite;			// default=0		min=0		max=1
		uint16_t blocksize;			// default=1000		min=1		max=8192
		uint16_t blocks_to_send;	// default=1		min=0		
		uint16_t clkdiv;			// default=96		min=96		max=65535
	} * args = (void*)(command_buffer+1);

	internal_adc_config.channel_mask = args->channel_mask; 
	internal_adc_config.infinite = args->infinite; 
	internal_adc_config.blocksize = args->blocksize; 
	internal_adc_config.clkdiv = args->clkdiv; 
	if (args->blocks_to_send) {
		internal_adc_config.blocks_to_send = args->blocks_to_send; 
		iADC_DMA_start(); 
	}
}



// === I/O MESSAGING INFRASTRUCTURE ===
 
typedef struct { void (*command_func)(void); void (*report_struct); } message_descriptor;
message_descriptor message_table[] = // #new_features: add your command to this table
        {   
                {&identify,		&identify_report},  
                {&pin_out,		&pin_out_report},
                {&test,			&test_report},
                {&internal_adc, &internal_adc_report},
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
            if ((packet_data = (uint8_t) getchar_timeout_us(0)) == PICO_ERROR_TIMEOUT) {
                sleep_ms(1);
            }
            command_buffer[i] = packet_data;
        }
        if (command_buffer[0] >= ARRAY_LEN(message_table)) {
            return; // todo: report overflow
        }

        message_entry = message_table[command_buffer[0]];
        message_entry.command_func();
    }
}


// === CORE0 AND CORE1 BUSY ROUTINES ===

void core1_main() { // busy loop on second CPU core takes care of real-time tasks
    while (true) {
		while (!iADC_DMA_IRQ_triggered) { };   
	    gpio_put(LED_PIN, 1); busy_wait_us_32(50); gpio_put(LED_PIN, 0); busy_wait_us_32(50); 
	    gpio_put(LED_PIN, 1); busy_wait_us_32(50); gpio_put(LED_PIN, 0); busy_wait_us_32(50); 
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

	BLINK_LED_US(5000)
	busy_wait_us_32(100000); 
	BLINK_LED_US(5000)

	while (true)  // busy loop on core0 handles mostly communication
	{ 
		//tight_loop_contents(); // does nothing

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

		//if (iADC_DMA_IRQ_triggered) {
			//iADC_DMA_IRQ_triggered = 0;
			//fwrite(iADC_buffer_choice ? iADC_buffer0 : iADC_buffer1, internal_adc_config.blocksize, 2, stdout);
			//fflush(stdout); 
		//}
	}
}




