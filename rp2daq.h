
#define TUD_OPT_HIGH_SPEED (1)
//#define CFG_TUD_CDC_EP_BUFSIZE 256 // legacy; needs to go into tusb_config.h that is being used

#define FIRMWARE_VERSION {"rp2daq_220720_"}

#define DATA_BY_REF 0
#define DATA_BY_COPY 1

#define DEBUG_PIN 4
#define DEBUG2_PIN 5
#define BLINK_LED_US(duration) gpio_put(PICO_DEFAULT_LED_PIN, 1); busy_wait_us_32(duration); gpio_put(PICO_DEFAULT_LED_PIN, 0); 

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

#define max(a,b)  ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a > _b ? _a : _b; })
#define min(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b);  _a < _b ? _a : _b; })


void tx_header_and_data(void* headerptr, 
		uint16_t headersize, 
		void* dataptr, 
		uint16_t datasize, 
		uint8_t make_copy_of_data);
