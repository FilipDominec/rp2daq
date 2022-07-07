#define DEBUG_PIN 4
#define DEBUG2_PIN 5


struct { 
	uint8_t channel_mask;	
	uint8_t infinite;		
	uint16_t blocksize;		
	uint16_t blockcount;	
	uint32_t clkdiv;		
} internal_adc_config;



//volatile uint16_t internal_adc_configblocksize=1000; // TODO issues with channel swapping (bytes lost?) when 300+kSPS and depth~200
//volatile uint8_t ADC_MASK=(2+4+8+16); // bits 1,2,4 are GPIO26,27,28; bit 8 internal reference, 16 temperature sensor
uint16_t iADC_buffer0[1024*16];
uint16_t iADC_buffer1[1024*16];
volatile uint8_t iADC_buffer_choice = 0;

volatile uint8_t iADC_DMA_IRQ_triggered;
dma_channel_config iADC_DMA_cfg;
int iADC_DMA_chan;


void iADC_DMA_start() {
	// Pause and drain ADC before DMA setup (doing otherwise breaks ADC input order)
	gpio_put(DEBUG_PIN, 1); 
	adc_run(false);				
	adc_fifo_drain(); // ??

	adc_set_round_robin(internal_adc_config.channel_mask);
	adc_set_clkdiv(internal_adc_config.clkdiv); // user-set
	//adc_set_clkdiv(96); // 96 -> full ADC speed at 500 kSPS
	//adc_set_clkdiv(120); // 400kSPS 
	//adc_set_clkdiv(125); // 384kSPS OK?
	//adc_set_clkdiv(130); // 366kSPS OK
	//adc_set_clkdiv(144); // 333kSPS ?
	//adc_set_clkdiv(160); // 300kSPS 
	//adc_set_clkdiv(182); // 250kSPS 
	//adc_set_clkdiv(96*4); // 125kSPS seems long-term safe against channel swapping
	//adc_set_clkdiv(96*100); // 5kSPS for debug

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
	adc_select_input(ADCch);  // force ADC input channel
	dma_channel_start(iADC_DMA_chan);

	adc_run(true);				
}


void iADC_DMA_IRQ_handler() {
    gpio_put(DEBUG_PIN, 0);
    dma_hw->ints0 = 1u << iADC_DMA_chan;  // clear the interrupt request to avoid re-trigger
    iADC_buffer_choice = iADC_buffer_choice ^ 0x01; // swap buffers

    // main loop on core0 will transmit data later
	//iADC_DMA_IRQ_triggered = 1;			  
			//fwrite(iADC_buffer_choice ? iADC_buffer0 : iADC_buffer1, internal_adc_config.blocksize, 2, stdout);
            //
    internal_adc_report._data_count = internal_adc_config.blocksize*2; // XXX
    internal_adc_report._data_bitwidth = 8;
    //internal_adc_report._data_count = internal_adc_config.blocksize; // todo test
    //internal_adc_report._data_bitwidth = 8*2;

	tx_header_and_data(&internal_adc_report, 
            sizeof(internal_adc_report), 
			iADC_buffer_choice ? &iADC_buffer0 : &iADC_buffer1, 
            internal_adc_report._data_count * internal_adc_report._data_bitwidth/8,
			1);

	adc_run(false);
    if (internal_adc_config.infinite || internal_adc_config.blockcount--)
	    iADC_DMA_start();					  // start new acquisition
}


void iADC_DMA_setup() { 

    /// todo mv to DMA start
	for (uint8_t ch=0; ch<4; ch++) {
        if (internal_adc_config.channel_mask & (1<<ch)) 
            adc_gpio_init(26+ch); 
    }
	if (internal_adc_config.channel_mask & (1<<4)) adc_set_temp_sensor_enabled(true);
    adc_init();

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
