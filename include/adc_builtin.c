
void iADC_DMA_setup();
void iADC_DMA_start(uint8_t is_delayed);
void iADC_DMA_IRQ_handler();

struct __attribute__((packed)) {
    uint8_t report_code;
    uint16_t _data_count; 
    uint8_t _data_bitwidth;
    uint8_t channel_mask;
    uint16_t blocks_to_send;
    uint8_t block_delayed_by_usb;
} adc_report;

struct { 
	uint8_t channel_mask;	
	uint8_t infinite;		
	uint16_t blocksize;		
    uint32_t blocks_to_send;	
	uint16_t clkdiv;		
} internal_adc_config;


void adc() {
    /* Initiates analog-to-digital conversion (ADC), using the RP2040 built-in feature.
     * 
     * __This command can result in one, several or infinitely many report(s). They can be 
     * almost immediate or delayed, depending on block size and timing. __
     */ 
	struct __attribute__((packed)) {
		uint8_t channel_mask;		// default=1		min=0		max=31 Masks 0x01, 0x02, 0x04 are GPIO26, 27, 28; mask 0x08 internal reference, 0x10 temperature sensor
		uint16_t blocksize;			// default=1000		min=1		max=8192 Number of sample points until a report is sent
		uint8_t infinite;			// default=0		min=0		max=1  Disables blocks_to_send countdown (reports keep coming until explicitly stopped)
		uint16_t blocks_to_send;	// default=1		min=0		         Number of reports to be sent (if not infinite)
		uint16_t clkdiv;			// default=96		min=96		max=65535 Sampling rate is 48MHz/clkdiv (e.g. 96 gives 500 ksps; 48000 gives 1000 sps etc.)
	} * command = (void*)(command_buffer+1);

	internal_adc_config.channel_mask = command->channel_mask; 
	internal_adc_config.infinite = command->infinite; 
	internal_adc_config.blocksize = command->blocksize; 
	internal_adc_config.clkdiv = command->clkdiv; 
	if (command->blocks_to_send) {
		internal_adc_config.blocks_to_send = command->blocks_to_send; 
		iADC_DMA_start(0); 
	}
}



// *** double-buffer management and auxiliary functions *** //

typedef struct { uint8_t data[1024*16]; uint8_t write_lock; uint8_t is_delayed; } iADC_buffer;
#define iADC_BUF_COUNT 4 // multi-buffering ensures continuous acquisition without USB delays
iADC_buffer iADC_buffers[iADC_BUF_COUNT];

volatile uint8_t iADC_buffer_choice = 0;
uint8_t iADC_DMA_start_pending;

dma_channel_config iADC_DMA_cfg;
int iADC_DMA_chan;


void iADC_DMA_setup() { 
	for (uint8_t ch=0; ch<4; ch++) {
        if (internal_adc_config.channel_mask & (1<<ch)) 
            adc_gpio_init(26+ch); 
    }
    adc_init();
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

void iADC_DMA_start(uint8_t is_delayed) {
	// Pause and drain ADC before DMA setup (doing otherwise breaks ADC input order)
    iADC_DMA_start_pending = 0;
	adc_run(false);				
	adc_fifo_drain(); 
	adc_set_round_robin(internal_adc_config.channel_mask);
	adc_set_clkdiv(internal_adc_config.clkdiv); // user-set

	// Prepare a new non-blocking ADC acquisition using DMA in background
	iADC_buffers[iADC_buffer_choice].is_delayed = is_delayed;
	iADC_buffers[iADC_buffer_choice].write_lock = 1;
	dma_channel_configure(iADC_DMA_chan, &iADC_DMA_cfg,
		iADC_buffers[iADC_buffer_choice].data,    // destination
		&adc_hw->fifo,  // src
		internal_adc_config.blocksize,  // transfer count
		true            // start immediately  (?)
	);

	// Forced ADC channel round-robin reset to the first enabled bit in adc_mask 
	uint8_t ADCch;
	for (ADCch=0; (ADCch <= 4) && !(1<<ADCch & internal_adc_config.channel_mask); ADCch++) {};
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



void iADC_DMA_IRQ_handler() {
    dma_hw->ints0 = 1u << iADC_DMA_chan;  // clear the interrupt request to avoid re-trigger
	adc_run(false);

	// Quickly swap buffers & start a new ADC acquisition (if appropriate)
    uint8_t iADC_buffer_prev = iADC_buffer_choice;
    iADC_buffer_choice = (iADC_buffer_choice + 1) % iADC_BUF_COUNT;

    if (internal_adc_config.infinite || --internal_adc_config.blocks_to_send) { 
        if (iADC_buffers[iADC_buffer_choice].write_lock) {
            iADC_DMA_start_pending = 1;
        } else { 
            iADC_DMA_start(0);
        };
    } 

	// Schedule finished buffer to be transmitted
    internal_adc_report._data_count = internal_adc_config.blocksize; // should not change
    internal_adc_report._data_bitwidth = 12;
    compress_2x12b_to_24b_inplace(iADC_buffers[iADC_buffer_prev].data, internal_adc_report._data_count);
    internal_adc_report.channel_mask = internal_adc_config.channel_mask;
    internal_adc_report.blocks_to_send = internal_adc_config.blocks_to_send;
    internal_adc_report.block_delayed_by_usb = iADC_buffers[iADC_buffer_prev].is_delayed;
	prepare_report_wrl(&internal_adc_report, 
            sizeof(internal_adc_report), 
			iADC_buffers[iADC_buffer_prev].data, 
            (internal_adc_report._data_count * internal_adc_report._data_bitwidth + (8-1))/8, //rounding up
            0, // don't make buffer copy
			&iADC_buffers[iADC_buffer_prev].write_lock); // will be auto-cleared upon transmit (see rp2daq.c)
	// (small todo: transmit CRC (already computed in SNIFF_DATA reg, chap. 2.5.5.2) )

}
