
void iADC_DMA_setup();
void iADC_DMA_start();
void iADC_DMA_IRQ_handler();
void iADC_trigger_IRQ();
void iADC_start_or_schedule_after_trigger();
void iADC_start_or_schedule_after_usb();

struct { 
	uint8_t channel_mask;	
	uint8_t infinite;		
	uint16_t blocksize;		
    uint32_t blocks_to_send;	
	uint16_t clkdiv;		
    uint64_t start_time_us;	
    uint64_t end_time_us;	
	uint8_t waits_for_usb;		
	uint8_t waits_for_trigger;		
	uint8_t block_delayed_by_usb;		
	int8_t  trigger_gpio;		
	uint8_t trigger_on_falling_edge;
	//uint8_t trigger_timeout_max;		// TODO
	//uint8_t block_delayed_by_trigger;		// TODO
	//uint8_t block_terminated_by_trigger;	// TODO
} iADC_config;


// =============================================================================

struct __attribute__((packed)) {
    uint8_t report_code;
    uint16_t _data_count; 
    uint8_t _data_bitwidth;
	uint64_t start_time_us;
	uint64_t end_time_us;
    uint8_t channel_mask;
    uint32_t blocks_to_send;
    uint8_t block_delayed_by_usb;
} adc_report;

void adc() {
    /* Initiates analog-to-digital conversion (ADC), using the RP2040 built-in feature.
     *
     * When ADC is already active, this takes no action. Use adc_stop() first in such a case.  
     * 
     * __This command can result in one, several or infinitely many report(s). They can be 
     * almost immediate or delayed, depending on block size and timing.__
     */ 
	struct __attribute__((packed)) {
		uint8_t channel_mask;		// default=1		min=0		max=31 Masks 0x01, 0x02, 0x04 are GPIO26, 27, 28; mask 0x08 internal reference, 0x10 temperature sensor
		uint16_t blocksize;			// default=1000		min=1		max=8192 Number of sample points until a report is sent
		uint8_t infinite;			// default=0		min=0		max=1  Disables blocks_to_send countdown; reports will keep coming until stopped by adc(blocks_to_send=0)
		uint32_t blocks_to_send;	// default=1		min=1		         Limits the number of reports to be sent (if the 'infinite' option is not set)
		uint16_t clkdiv;			// default=96		min=96		max=65535 Sampling rate is 48MHz/clkdiv (e.g. 96 gives 500 ksps; 48000 gives 1000 sps etc.)
		int8_t trigger_gpio;		// default=-1		min=-1		max=24 GPIO number for start trigger (leave -1 to make ADC start immediately)
		uint8_t trigger_on_falling_edge;	// default=0		min=0		max=1 If set to 1, triggers on falling edge instead of rising edge.
	} * command = (void*)(command_buffer+1);
    // TODO implement send_data and send_statistics options

    if (!(iADC_config.blocks_to_send || iADC_config.infinite)) { // re-init of running ADC makes trouble
        iADC_config.channel_mask = command->channel_mask; 
        iADC_config.infinite = command->infinite; 
        iADC_config.blocksize = command->blocksize; 
        iADC_config.clkdiv = command->clkdiv; 
        iADC_config.blocks_to_send = command->blocks_to_send; 

        iADC_config.trigger_gpio = command->trigger_gpio;
        iADC_config.trigger_on_falling_edge = command->trigger_on_falling_edge;


        if (iADC_config.trigger_gpio > -1)
             gpio_set_irq_enabled_with_callback(
                     1, 
                     iADC_config.trigger_on_falling_edge ? GPIO_IRQ_EDGE_FALL : GPIO_IRQ_EDGE_RISE, //GPIO_IRQ_EDGE_FALL ,
                     true, 
                     &iADC_trigger_IRQ);
        iADC_start_or_schedule_after_trigger(); 
    };
}


struct __attribute__((packed)) {
    uint8_t report_code;
    uint32_t aborted_blocks_to_send;
} adc_stop_report;

void adc_stop() {
    /* Manually sets the analog-to-digital conversion not to start another sampling ADC block after the active block is 
     * finished. Also an ADC block won't be started if it is waiting for a trigger event to start. 
     *
     * Still, one or more ADC report(s) may still arrive after adc_stop() being issued; these were either actively 
     * sampled at the moment, or were stored in the USB transmit queue. 
     *
     * Adc_stop() is most useful when adc(infinite=True) was previously called. Stopping ADC is also necessary to re-run 
     * it with different configuration. But if you want to sample exactly X blocks it may be easier to specify their number
     * by calling adc(blocks_to_send=X) instead.
     *
     * If ADC is not running, this takes no action. 
     * 
     * __This command will result in one immediate report.  __
     */ 
	struct __attribute__((packed)) {
        uint8_t finish_last_adc_packet;    // min=1 max=1 default=1  Hard stopping of ADC in the middle of a block not implemented yet.
	} * command = (void*)(command_buffer+1);

    adc_stop_report.aborted_blocks_to_send = iADC_config.blocks_to_send;
    iADC_config.infinite = 0;
    iADC_config.blocks_to_send = 0; 
	iADC_config.waits_for_usb = 0;	
	iADC_config.waits_for_trigger = 0;	
    prepare_report(&adc_stop_report, sizeof(adc_stop_report), 0,0,0);
}



// =============================================================================
// Double-buffer management and auxiliary functions 

typedef struct  { 
    uint8_t data[1024*16]; 
    uint8_t write_lock; 
    uint8_t dummy0;  // This is necessary for proper byte alignment, why? 
} iADC_buffer;

#define iADC_BUF_COUNT 4 // multi-buffering ensures continuous acquisition without USB delays
iADC_buffer iADC_buffers[iADC_BUF_COUNT];

volatile uint8_t iADC_active_buffer = 0;

dma_channel_config iADC_DMA_cfg;
int iADC_DMA_chan;


void iADC_DMA_init() { 
    adc_init();
	for (uint8_t ch=0; ch<4; ch++) {
        if (iADC_config.channel_mask & (1<<ch)) 
            adc_gpio_init(26+ch); 
			gpio_set_dir(26+ch, GPIO_IN); // not necessary here?
			gpio_disable_pulls(26+ch); // sets the input into high-z state again 
    }

    adc_set_temp_sensor_enabled(true);

    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ (and IRQ) asserted when at least 1 sample present
        false,   // disable ERR bit
        false    // don't trunc samples to 8-bit
    );
	
    // Set up the DMA to start transferring data as soon as it appears in FIFO
    iADC_DMA_chan = dma_claim_unused_channel(true);
    iADC_DMA_cfg = dma_channel_get_default_config(iADC_DMA_chan);
    channel_config_set_transfer_data_size(&iADC_DMA_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&iADC_DMA_cfg, false); // from ADC
    channel_config_set_write_increment(&iADC_DMA_cfg, true); // into buffer
    channel_config_set_dreq(&iADC_DMA_cfg, DREQ_ADC);

    // Raise interrupt when DMA finishes a block
    dma_channel_set_irq0_enabled(iADC_DMA_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, iADC_DMA_IRQ_handler);
    irq_set_enabled(DMA_IRQ_0, true);
	adc_run(true);
}

void iADC_DMA_start() {
    iADC_config.block_delayed_by_usb = iADC_config.waits_for_usb;
    iADC_config.waits_for_usb = 0;
    //iADC_config.block_delayed_by_trigger = iADC_config.waits_for_trigger; TODO 
	
	// Pause and drain ADC before DMA setup (doing otherwise breaks ADC input order)
	adc_run(false);				
	adc_fifo_drain(); 
	adc_set_round_robin(iADC_config.channel_mask);
	adc_set_clkdiv(iADC_config.clkdiv); // user-set

	// Prepare a new non-blocking ADC acquisition using DMA in background
    //iADC_buffers[iADC_active_buffer].start_time_us = time_us_64();
    iADC_config.start_time_us = time_us_64();

	iADC_buffers[iADC_active_buffer].write_lock = 1; // will be released upon transmit
	dma_channel_configure(iADC_DMA_chan, &iADC_DMA_cfg,
		iADC_buffers[iADC_active_buffer].data,    // destination
		&adc_hw->fifo,  // src
		iADC_config.blocksize,  // transfer count
		true            // start immediately  (?)
	);

	// Forced ADC channel round-robin reset to the first enabled bit in adc_mask 
	uint8_t ADCch;
	for (ADCch=0; (ADCch <= 4) && !(1<<ADCch & iADC_config.channel_mask); ADCch++) {};
	adc_select_input(ADCch);  // force 1st enabled ADC input channel
	dma_channel_start(iADC_DMA_chan);
	adc_run(true);				
}


void compress_2x12b_to_24b_inplace(uint8_t* buf, uint32_t data_count) {
    // Squashes a pair of 0..4095 short integers from 4B into 3B, saves 25% of USB bandwidth
    // e.g. from two decimal values 2748 3567, originally stored little-endian as four bytes 
    // 0xBC 0x0A 0xEF 0x0D,  this makes 0xBC 0xAE 0xFD to be later expanded back in computer
    for (uint16_t i; i<(data_count+1)/2; i+=1) { 
		// It takes 320 us for 4000 pairs (i.e. 8kB->6kB); one i-cycle is thus 40 CPU cycles.
        uint8_t a = buf[i*4];
        uint8_t b = buf[i*4+1]*16 + buf[i*4+2]/16;
        uint8_t c = buf[i*4+2]*16 + buf[i*4+3];

        buf[i*3] = a; buf[i*3+1] = b; buf[i*3+2] = c;
    }
}



void iADC_start_or_schedule_after_trigger() { // ... or for user-defined timeout TODO
    if ((iADC_config.infinite) || (iADC_config.blocks_to_send > 0)) { 
		if (iADC_config.trigger_gpio < 0) {
			iADC_start_or_schedule_after_usb();
		} else {
		gpio_put(PICO_DEFAULT_LED_PIN, 1); // XXX
			iADC_config.waits_for_trigger = 1;
		}
    } 
}

void iADC_trigger_IRQ(uint gpio, uint32_t events) {
	
	if (iADC_config.waits_for_trigger) {
		gpio_put(PICO_DEFAULT_LED_PIN, 0); // XXX
		iADC_start_or_schedule_after_usb();
	};
}

void iADC_start_or_schedule_after_usb() {
	iADC_config.waits_for_trigger = 0;

	if (iADC_buffers[iADC_active_buffer].write_lock) {
		iADC_config.waits_for_usb = 1; // (or arm it to be started ASAP, should never occur)
	} else { 
		iADC_DMA_start(); 
	};
}

uint8_t iADC_on_buffer_transmitted() { // note this is called from main USB communication loop in rp2daq.c
	if (iADC_config.waits_for_usb && !(iADC_buffers[iADC_active_buffer].write_lock)) {
		iADC_DMA_start();
	}
}

void iADC_DMA_IRQ_handler() {
    dma_hw->ints0 = 1u << iADC_DMA_chan;  // clear the interrupt request to avoid re-trigger
	adc_run(false);

	// Quickly swap buffers & start a new ADC acquisition (if appropriate)
    uint8_t iADC_buffer_prev = iADC_active_buffer;
    iADC_active_buffer = (iADC_active_buffer + 1) % iADC_BUF_COUNT;

	// First remember ADC config that will get overwritten 
    adc_report.end_time_us = time_us_64(); 
    adc_report.start_time_us = iADC_config.start_time_us;

    // If possible start the next ADC acquisition block non-delayed (i.e. within <4 us)
	if (iADC_config.blocks_to_send) iADC_config.blocks_to_send--;

    //if ((iADC_config.infinite) || (iADC_config.blocks_to_send > 0)) { 
		//iADC_DMA_start();
	//}
	iADC_start_or_schedule_after_trigger();

	// Schedule the just finished buffer to be transmitted
    adc_report._data_count = iADC_config.blocksize; // should not change
    adc_report._data_bitwidth = 12;
    adc_report.channel_mask = iADC_config.channel_mask;
    adc_report.blocks_to_send = iADC_config.blocks_to_send;
    adc_report.block_delayed_by_usb = iADC_config.block_delayed_by_usb;

	if (adc_report._data_bitwidth == 12) {
		compress_2x12b_to_24b_inplace(iADC_buffers[iADC_buffer_prev].data, adc_report._data_count); }

	prepare_report_wrl(&adc_report, 
            sizeof(adc_report), 
			iADC_buffers[iADC_buffer_prev].data, 
            (adc_report._data_count * adc_report._data_bitwidth + (8-1))/8, //rounding up
            0, // don't make buffer copy
			&iADC_buffers[iADC_buffer_prev].write_lock); // will be auto-cleared upon transmit (see rp2daq.c)
	// (small todo: transmit CRC (already computed in SNIFF_DATA reg, chap. 2.5.5.2) )

}




/* XXX
struct __attribute__((packed)) {
    uint8_t report_code;
} adc_set_trigger_report;
void adc_set_trigger() 
{
	struct __attribute__((packed)) {
		int8_t trigger_gpio;		// default=0		min=-1		max=24 Masks 0x01, 0x02, 0x04 are GPIO26, 27, 28; mask 0x08 internal reference, 0x10 temperature sensor
	} * command = (void*)(command_buffer+1);

	//gpio_set_irq_enabled_with_callback(args->trigger_gpio, edge_mask, true, &iADC_trigger_IRQ);
}
 *
 *
 *

    //if (command->infinite || command->blocks_to_send) {
    //} 
    //else 
    //{ // will stop ADC after this block is finished
        //iADC_config.infinite = 0;
        //iADC_config.blocks_to_send = 0; 
    //}


	// TODO may need to reimplement GPIO IRQ globally like
	//void irqhandler1() {
		//from https://forums.raspberrypi.com/viewtopic.php?t=339696
		//if (gpio_get_irq_event_mask(1) & GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL) {
			//gpio_acknowledge_irq(1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);
			//gpio_xor_mask(0b100);
		//}
	//}
	//gpio_set_irq_enabled(1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true); //monitor pin 1 connected to pin 0
    //gpio_add_raw_irq_handler(1,irqhandler1);
    //irq_set_enabled(IO_IRQ_BANK0, true);
 *
 *
 *
 *
// TODO rp.adc(blocks_to_send=1, blocksize=10, trigger_gpio=-1) ## freezes ? 
//rp.adc(blocks_to_send=3, trigger_gpio=2) ## IS OK? 
*/
