#!/usr/bin/python3  
#-*- coding: utf-8 -*-

wait_sec = 1.


import time

import rp2daq
rp = rp2daq.Rp2daq()

# Optional: Let's prepare PWM for some artificial signal on GPIO 0
# Note that even 10kHz clock works, i.e. 100k reports per second can be received
print("Configuring PWM channel to make artificial 5kHz square wave (and waiting for it to settle)")
rp.pwm_configure_pair(gpio=0, 
        clkdiv=250, # clock at 1.000 MHz
        wrap_value=200-1) # one rising edge, one falling edge at 5.000 kHz  

time.sleep(.08) # unclear why, but 100 ms is safe

rp.pwm_set_value(0, 100) 


## Define a report handler and start asynchronous reporting on each GPIO change
print("Registering rising/falling edge events...")

count = [0]
def handler(**kwargs): 
    #print(kwargs)
    count[0] += 1
rp.gpio_on_change(0, on_rising_edge=1, on_falling_edge=1, _callback=handler)



time.sleep(wait_sec)

print(f'Number of received edge reports after {wait_sec} seconds:', count)


## Note one or few more reports may be on their way yet, so:
## 1. stop the PWM
print("Switching off PWM signal")
def dummy_cb(**kwargs): pass
rp.pwm_set_value(0, 0, _callback=dummy_cb) # stop PWM signal (immediately)
#rp.pwm_set_value(0, 0) # c.f. stop PWM signal (waits for all previous reports)

    ## 2. stop the device from issuing new reports, but wait for pending ones to arrive
    #rp.pin_on_change(pin=0, on_rising_edge=1, on_falling_edge=0, _callback=handler)  

## Receive pending reports (if any)
after_wait_sec = 0.1
time.sleep(after_wait_sec)
print(f'Total received edge reports after extra {after_wait_sec} seconds:', count)
time.sleep(after_wait_sec)
print(f'Total received edge reports after another extra {after_wait_sec} seconds:', count)
