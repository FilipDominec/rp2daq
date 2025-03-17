
#define FIRMWARE_VERSION {"rp2daq_250317_"}


#define TUD_OPT_HIGH_SPEED (1)
//#define CFG_TUD_CDC_EP_BUFSIZE 256 // legacy; needs to go into tusb_config.h that is being used

// 240221-241205 - we fixed troubles running Waveshare's RP2040-Zero board, which randomly failed to be 
// detected upon USB re-connection. See also https://github.com/raspberrypi/pico-sdk/pull/1421 etc.
#define XOSC_STARTUP_DELAY_MULTIPLIER  64
#undef PICO_FLASH_SPI_CLKDIV 
#define PICO_FLASH_SPI_CLKDIV 4



#define DATA_BY_REF 0
#define DATA_BY_COPY 1

#define DEBUG_PIN 4
#define DEBUG2_PIN 5
#define BLINK_LED_US(duration) gpio_put(PICO_DEFAULT_LED_PIN, 1); busy_wait_us_32(duration); gpio_put(PICO_DEFAULT_LED_PIN, 0); 

#define ARRAY_LEN(arr)   (sizeof(arr)/sizeof((arr)[0]))

#define max(a,b)  ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a > _b ? _a : _b; })
#define min(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a < _b ? _a : _b; })


// Simple buffer for incoming commands
// (small fixme: python module should check no command is longer than RXBUF_LEN bytes
#define RXBUF_LEN 1024    
uint8_t command_buffer[RXBUF_LEN];

// Cyclic buffer for staging reports to be sent (fixed-length structures only)
#define TXBUF_LEN 256    // (longer data than this can be transmitted as reference to memory)
#define TXBUF_COUNT 8    // up to 8 reports can be immediately scheduled, even if USB is busy
uint8_t  txbuf[(TXBUF_LEN*TXBUF_COUNT)];
uint16_t txbuf_struct_len[TXBUF_COUNT];
void*    txbuf_data_ptr[TXBUF_COUNT];
uint32_t txbuf_data_len[TXBUF_COUNT];
uint8_t* txbuf_data_write_lock_ptr[TXBUF_COUNT]; // optional write lock; released upon transmit

volatile uint8_t txbuf_tofill, txbuf_tosend;  // start & end of a cyclic buffer
volatile uint8_t txbuf_lock;

void prepare_report(void* headerptr, 
		uint16_t headersize, 
		void* dataptr, 
		uint16_t datasize, 
		uint8_t make_copy_of_data);
void prepare_report_wrl(void* headerptr, 
		uint16_t headersize, 
		void* dataptr, 
		uint16_t datasize, 
		uint8_t make_copy_of_data, 
        uint8_t* data_write_lock_ptr);
