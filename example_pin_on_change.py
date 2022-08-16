#!/usr/bin/python3  
#-*- coding: utf-8 -*-

wait_sec = 1.


import time

import rp2daq
rp = rp2daq.Rp2daq()

# Optional: Let's prepare PWM for some artificial signal on pin 0
# Note that 10kHz clock, i.e. 100k reports per second is still possible
rp.pwm_configure_pair(0, 
        clkdiv=250, # clock at 1000.0 kHz
        wrap_value=200) # one rising edge, one falling edge at 5.000 kHz  
rp.pwm_set_value(0, 100) 

print("Waiting a moment for PWM channel to configure") # unclear why, but 100 ms is safe
time.sleep(.08)

#rp.pwm_set_value(0, 100) 


## Define a report handler and start asynchronous reporting on each pin change
count = [0]
def handler(**kwargs): 
    count[0] += 1
rp.pin_on_change(0, on_rising_edge=1, on_falling_edge=0, _callback=handler)



## Acquire signal over given timespan to collect some reports

time.sleep(wait_sec) # FIXME as if signal for ca. 67 ms were missing...
print(f'Received edge reports after {wait_sec} seconds:', count)


## Note one or few more reports may be on their way yet, so:
## 1. stop the PWM
print("Switching off PWM signal")
def dummy_cb(**kwargs): pass
rp.pwm_set_value(0, 0, _callback=dummy_cb) # stop PWM signal (immediate)
#rp.pwm_set_value(0, 0) # c.f. stop PWM signal (waits for all previous reports)

    ## 2. stop the device from issuing new reports, but wait for pending ones to arrive
    #rp.pin_on_change(pin=0, on_rising_edge=1, on_falling_edge=0, _callback=handler)  

## Receive pending reports (if any)
after_wait_sec = 0.1
time.sleep(after_wait_sec)
print(f'Total received edge reports after extra {after_wait_sec} seconds:', count)
time.sleep(after_wait_sec)
print(f'Total received edge reports after another extra {after_wait_sec} seconds:', count)
