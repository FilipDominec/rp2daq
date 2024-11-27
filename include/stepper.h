
// Stepper support using Stepstick
#define NANOPOS_AT_ENDSWITCH  (uint32_t)(1<<31)   // so that motor can move symmetrically from origin
#define NANOSTEP_PER_MICROSTEP  256             // for fine-grained speed and position control
#define MAX_STEPPER_COUNT 16
#define STEPPER_IS_MOVING(m)	(stepper[m].max_nanospeed > 0)
#define ENDSWITCH_TEST(m) ((stepper[m].endswitch_sensitive) && (stepper[m].endswitch_gpio >= 0) && \
			 (!gpio_get(stepper[m].endswitch_gpio)))


typedef struct __attribute__((packed)) {
    uint8_t  initialized;      
    uint8_t  dir_gpio;
    uint8_t  step_gpio;
    uint8_t  endswitch_gpio;
    uint8_t  disable_gpio;
    uint8_t  endswitch_sensitive;
    uint8_t  reset_nanopos_at_endswitch;
    uint32_t nanopos;
    uint32_t target_nanopos;
    uint32_t max_nanospeed;
    uint32_t inertia_coef;
    uint32_t previous_nanopos;
    uint8_t  move_reached_endswitch;
    uint64_t start_time_us; 
} stepper_config_t;
volatile stepper_config_t stepper[MAX_STEPPER_COUNT];
