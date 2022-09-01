
void iADC_DMA_start();
void iADC_DMA_IRQ_handler();
#define DEBUG_PIN 4
#define DEBUG2_PIN 5

struct __attribute__((packed)) {
    uint8_t report_code;
    uint16_t _data_count; 
    uint8_t _data_bitwidth;
    uint8_t channel_mask;
    uint16_t blocks_to_send;
} internal_adc_report;

struct { 
	uint8_t channel_mask;	
	uint8_t infinite;		
	uint16_t blocksize;		
    uint32_t blocks_to_send;	
	uint16_t clkdiv;		
} internal_adc_config;

void internal_adc() {
    /* Initiates analog-to-digital conversion (ADC), using by the RP2040 built-in feature.
     * 
     * __This command can result in one, several or infinitely many report(s). They can be 
     * almost immediate or delayed, depending on block size and timing. __
     */ 
	struct __attribute__((packed)) {
		uint8_t channel_mask;		// default=1		min=0		max=31 Bits 1,2,4 are GPIO26,27,28; bit 8 internal reference, 16 temperature sensor
		uint8_t infinite;			// default=0		min=0		max=1  Disables blocks_to_send countdown (reports keep coming until explicitly stopped)
		uint16_t blocksize;			// default=1000		min=1		max=8192 Number of sample points until a report is sent
		uint16_t blocks_to_send;	// default=1		min=0		         Number of reports to be sent
		uint16_t clkdiv;			// default=96		min=96		max=65535 Sampling rate is 48MHz/clkdiv (e.g. 96 gives 500 ksps; 48000 gives 1000 sps etc.)
	} * command = (void*)(command_buffer+1);

	internal_adc_config.channel_mask = command->channel_mask; 
	internal_adc_config.infinite = command->infinite; 
	internal_adc_config.blocksize = command->blocksize; 
	internal_adc_config.clkdiv = command->clkdiv; 
	if (command->blocks_to_send) {
		internal_adc_config.blocks_to_send = command->blocks_to_send; 
		iADC_DMA_start(); 
	}
}




//volatile uint8_t ADC_MASK=(2+4+8+16); // 
uint16_t iADC_buffer0[1024*16];
uint16_t iADC_buffer1[1024*16];
volatile uint8_t iADC_buffer_choice = 0;

dma_channel_config iADC_DMA_cfg;
int iADC_DMA_chan;


void iADC_DMA_start() {
	// Pause and drain ADC before DMA setup (doing otherwise breaks ADC input order)
	adc_run(false);				
	adc_fifo_drain(); // ??

	adc_set_round_robin(internal_adc_config.channel_mask);
	adc_set_clkdiv(internal_adc_config.clkdiv); // user-set

	// Initiate non-blocking ADC run, instead of calling dma_channel_wait_for_finish_blocking()
	dma_channel_configure(iADC_DMA_chan, &iADC_DMA_cfg,
		iADC_buffer_choice ? iADC_buffer1 : iADC_buffer0,    // dst
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
        uint8_t a = buf[i*4];
        uint8_t b = buf[i*4+1]*16 + buf[i*4+2]/16;
        uint8_t c = buf[i*4+2]*16 + buf[i*4+3];

        buf[i*3] = a;
        buf[i*3+1] = b;
        buf[i*3+2] = c;
    }
}

void iADC_DMA_IRQ_handler() {
    // TODO check if 2nd DMA ch could swap buffers **entirely** without irq avoiding 1% dead-time;
	// DMA chain trigger should do this (chap. 2.5.6.2)
    // Now we achieve 494 ksps, but true 500 ksps would be possible with that 
    gpio_put(DEBUG_PIN, 1);
    dma_hw->ints0 = 1u << iADC_DMA_chan;  // clear the interrupt request to avoid re-trigger
    iADC_buffer_choice = iADC_buffer_choice ^ 0x01; // swap buffers
	adc_run(false);
    if (internal_adc_config.infinite || --internal_adc_config.blocks_to_send)
    { 
	    iADC_DMA_start();					  // start new acquisition
    }

    uint8_t* finished_adc_buf = (uint8_t*)(iADC_buffer_choice ? &iADC_buffer0 : &iADC_buffer1);

    internal_adc_report._data_count = internal_adc_config.blocksize; // should not change
    internal_adc_report.channel_mask = internal_adc_config.channel_mask;
    internal_adc_report.blocks_to_send = internal_adc_config.blocks_to_send;
	// TODO transmit CRC (already computed in SNIFF_DATA reg, chap. 2.5.5.2)
    
    //for (uint16_t i; i<(data_count+1)/2; i+=1) {  // DEBUG data
        //buf[i*3] = i/256; // XXX
        //buf[i*3+1] = i%256; // XXX
        //buf[i*3+2] = 0; // XXX
    //}
    compress_2x12b_to_24b_inplace(finished_adc_buf, internal_adc_report._data_count);
    internal_adc_report._data_bitwidth = 12;
    //internal_adc_report._data_bitwidth = 16; // failsafe option taking 133% USB bw
    
	tx_header_and_data(&internal_adc_report, 
            sizeof(internal_adc_report), 
			finished_adc_buf, 
            (internal_adc_report._data_count * internal_adc_report._data_bitwidth + (8-1))/8,
			0);


    gpio_put(DEBUG_PIN, 0);

}


    // when tx schedule before DMA_start:
    // timing check for 12bitwidth @500 ksps nom.: core0 ~16160us transmitting, 584us outside loop (i.e. near full load)
    // (note: compiled with the TUD_OPT_HIGH_SPEED trick)
    // 524280 out of expected 524280 kB received, 100% success, real 477345 ksps

    //internal_adc_report._data_bitwidth = 16; 
    // timing check for 16bitwidth @500 ksps nom.: core0 ~16000us transmitting, 0.9us outside loop (overload, data loss:)
    // 523960 out of expected 524280 kB received, 327 out of 65535 chunks lost (~0.06% loss)
    

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
