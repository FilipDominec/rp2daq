
typedef struct { 
	uint8_t clkdiv;				// default=96		min=96		max=1000000
	uint8_t channel_mask;		// default=1		min=0		max=31
	uint16_t blocksize;			// default=1000		min=1		max=2048
	uint16_t blockcount;		// default=1		min=0		max=2048
	uint8_t continued;			// default=0		min=0		max=1
} internal_adc_config_t;

internal_adc_config_t internal_adc_config;

volatile uint16_t CAPTURE_DEPTH=1000; // TODO issues with channel swapping (bytes lost?) when 300+kSPS and depth~200
volatile uint8_t ADC_MASK=(2+4+8+16); // 1,2,4 are GPIO26,27,28; 8 internal reference, 16 temperature sensor
//volatile uint8_t ADC_MASK=(2); // 1,2,4 are GPIO26,27,28; 8 internal reference, 16 temperature sensor
uint16_t iADC_buffer0[1024*8] = {0,0,9000,9000,9000,5000}; 
uint16_t iADC_buffer1[1024*8] = {0,0,10000,8000,10000,10000}; 
volatile uint8_t iADC_buffer_choice = 0;

volatile uint8_t iADC_DMA_IRQ_triggered;
dma_channel_config iADC_DMA_cfg;
int iADC_DMA_chan;


void iADC_DMA_start() {
	// Pause and drain ADC before DMA setup (doing otherwise breaks ADC input order)
	adc_run(false);				
	adc_fifo_drain();


	// Initiate non-blocking ADC run, instead of calling dma_channel_wait_for_finish_blocking()
	dma_channel_configure(iADC_DMA_chan, &iADC_DMA_cfg,
		iADC_buffer_choice ? iADC_buffer1 : iADC_buffer0,    // dst
		&adc_hw->fifo,  // src
		CAPTURE_DEPTH,  // transfer count
		true            // start immediately  (?)
	);

	// Forced ADC channel round-robin reset to the first enabled bit in ADC_MASK 
	uint8_t ADCch;
	for (ADCch=0; (ADCch <= 4) && !(1<<ADCch & ADC_MASK); ADCch++) {};
	adc_select_input(ADCch);  // force ADC input channel
	dma_channel_start(iADC_DMA_chan);

	adc_run(true);				
}


void iADC_DMA_IRQ_handler() {
    dma_hw->ints0 = 1u << iADC_DMA_chan;  // clear the interrupt request to avoid re-trigger
    iADC_buffer_choice = iADC_buffer_choice ^ 0x01; // swap buffers
	iADC_DMA_IRQ_triggered = 1;			  // main loop on core0 will transmit data later
	adc_run(false);
	iADC_DMA_start();					  // start new acquisition
}


void iADC_DMA_setup() { 
	
	for (uint8_t ch=0; ch<4; ch++) { if (ADC_MASK & (1<<ch)) adc_gpio_init(26+ch); }
	if (ADC_MASK & (1<<4)) adc_set_temp_sensor_enabled(true);
    adc_init();
	adc_set_round_robin(ADC_MASK);
	adc_set_clkdiv(96); // 96 -> full ADC speed at 500 kSPS
	//adc_set_clkdiv(96); // 96 -> full ADC speed at 500 kSPS
	//adc_set_clkdiv(120); // 400kSPS 
	//adc_set_clkdiv(125); // 384kSPS OK?
	//adc_set_clkdiv(130); // 366kSPS OK
	//adc_set_clkdiv(144); // 333kSPS ?
	//adc_set_clkdiv(160); // 300kSPS 
	//adc_set_clkdiv(182); // 250kSPS 
	//adc_set_clkdiv(96*4); // 125kSPS seems long-term safe against channel swapping
	//adc_set_clkdiv(96*100); // 5kSPS for debug
    //sleep_ms(2000); // TODO rm?

    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ (and IRQ) asserted when at least 1 sample present
        false,   // disable ERR bit
        false    // keep each sample 12-bit
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
