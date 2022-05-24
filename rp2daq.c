#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "pico/unique_id.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

#include "include/adc_internal.c"
// #pragma pack (1)

#define VERSION ("220122")

#define LED_PIN 25
#define DEBUG_PIN 4
#define DEBUG2_PIN 5

uint8_t command_buffer[1024];

// === OUTGOING REPORTS === 

struct {
    uint16_t report_length;
    uint8_t report_code;
    uint8_t diameter;
} * report_prototype_a;

struct {
    uint16_t report_length;
    uint8_t report_code;
    uint16_t curvature;
} * report_prototype_b;
// ... more reports to come ...

void* list_of_reports[] = 
    {
        {&report_prototype_a}, 
        {&report_prototype_b} 
    };



// === INCOMING COMMAND HANDLERS ===
// @new_features: If a new functionality is added, please make a copy of any of following command 
// handlers and don't forget to register this new function in the command_table below;
// The corresponding method in the pythonic interface will then be auto-generated upon RP2DAQ restart

void identify() {   
	struct  __attribute__((packed)) { 
	} * args = (void*)(command_buffer+1);

	uint8_t text[14+16+1] = {'r','p','2','d','a','q','_', '2','2','0','1','2','0', '_'};
	pico_get_unique_board_id_string(text+14, 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1);
	fwrite(text, sizeof(text)-1, 1, stdout);
	fflush(stdout); 
}

void internal_adc() {
	struct __attribute__((packed)) {
		uint8_t channel_mask;		// default=1		min=0		max=31
		uint8_t infinite;			// default=0		min=0		max=1
		uint16_t blocksize;			// default=1000		min=1		max=2048
		uint16_t blockcount;		// default=1		min=0		max=2048
		uint16_t clkdiv;			// default=96		min=96		max=1000000
	} * args = (void*)(command_buffer+1);

	internal_adc_config.channel_mask = args->channel_mask; 
	internal_adc_config.blocksize = args->blocksize; 
	internal_adc_config.clkdiv = args->clkdiv; 
	if (args->blockcount) {
		internal_adc_config.blockcount = args->blockcount - 1; 
		iADC_DMA_start(); 
	}
}

void test() {
	struct  __attribute__((packed)) {
		uint8_t p,c; 	
	} * args = (void*)(command_buffer+1);

	uint8_t text[14+16+1] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', };
	text[args->p] = args->c; // for messaging DEBUG only
	fwrite(text, sizeof(text)-1, 1, stdout);
	fflush(stdout); 
}



// === I/O MESSAGING INFRASTRUCTURE ===

typedef struct { void (*command_func)(void); } command_descriptor;
command_descriptor command_table[] = // #new_features: add your command to this table
        {   
                {&identify},  
                {&internal_adc},
                {&test},
        };  

void get_next_command() {
    int packet_size;
    uint8_t packet_data;
    command_descriptor command_entry;
    command_buffer[0] = 0x00;

    // Get the number of bytes of the command packet.
    // The first byte is the command ID and the following bytes
    // are the associated data bytes
    if ((packet_size = getchar_timeout_us(0)) == PICO_ERROR_TIMEOUT) {
        return; // TODO allow 16bit packet_size
    } else {
        // get the rest of the packet
        for (int i = 0; i < packet_size; i++) {
            if ((packet_data = (uint8_t) getchar_timeout_us(0)) == PICO_ERROR_TIMEOUT) {
                sleep_ms(1);
            }
            command_buffer[i] = packet_data;
        }


        // the first byte is the command ID.
        // look up the function and execute it.
        // data for the command starts at index 1 in the command_buffer
        command_entry = command_table[command_buffer[0]];

        // uncomment to see the command and first byte of data
        //fwrite(command_buffer,1,2,stdout);

        command_entry.command_func();

    }
}


// === CORE0 AND CORE1 BUSY ROUTINES ===

void core1_main() { // busy loop on second CPU core takes care of real-time tasks
    while (true) {
		while (!iADC_DMA_IRQ_triggered) { };   
	    gpio_put(LED_PIN, 1); busy_wait_us_32(50);    gpio_put(LED_PIN, 0);busy_wait_us_32(50); 
	    gpio_put(LED_PIN, 1); busy_wait_us_32(50);    gpio_put(LED_PIN, 0);busy_wait_us_32(50); 
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

	// Setup routines for subsystems - ran once to initialize hardware & constants
	iADC_DMA_setup();

    gpio_put(LED_PIN, 1); busy_wait_us_32(5000);    
    gpio_put(LED_PIN, 0); busy_wait_us_32(100000); 
    gpio_put(LED_PIN, 1); busy_wait_us_32(5000);    
    gpio_put(LED_PIN, 0); 

	while (true)  // busy loop on core0 handles mostly communication
	{ 
		tight_loop_contents();

		get_next_command();

		if (iADC_DMA_IRQ_triggered) {
			iADC_DMA_IRQ_triggered = 0;

			gpio_put(DEBUG2_PIN, 1); 
			// Notes: printf() is not for binary data; putc() is slow; puts() is disguised putc
			// fwrite blocks code execution, but transmits >850 kBps (~limit of USB 1.1)
			// for message length >50 B (or, if PC rejects data, fwrite returns in <40us)
			fwrite(iADC_buffer_choice ? iADC_buffer0 : iADC_buffer1, internal_adc_config.blocksize, 2, stdout);
			fflush(stdout); 
			gpio_put(DEBUG2_PIN, 0); 

		}
	}
}




