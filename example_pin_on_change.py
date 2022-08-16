#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import time

import rp2daq
rp = rp2daq.Rp2daq()

## Define a report handler and start asynchronous reporting on each pin change
count = [0]
def handler(**kwargs): 
    count[0] += 1
    #print("HANDLER", count, kwargs)
rp.pin_on_change(0, on_rising_edge=0, on_falling_edge=1, _callback=handler)
time.sleep(.5)


# Optional: Let's make some artificial signal on pin 0
rp.pwm_configure_pair(0, 
        clkdiv=250, # clock at 1.000 MHz
        wrap_value=200) # one rising edge, one falling edge at 5000.0 Hz  
rp.pwm_set_value(0, 100) 
#time.sleep(.5)


## Wait one second to collect some reports
t0 = time.time()
time.sleep(1.0) # FIXME as if signal for ca. 67 ms were missing...
print(count)
print(time.time()-t0)

## Note one or few more reports may be on their way yet, so:
## stop the device from issuing new reports, but wait for pending ones to arrive
def dummy_cb(**kwargs):
    pass

rp.pin_on_change(pin=0, on_rising_edge=1, on_falling_edge=0, _callback=handler)  
#rp.pwm_set_value(0, 0, _callback=dummy_cb) # and optionally stop signal (immediate)
#rp.pwm_set_value(0, 0) # and optionally stop signal (waits for all previous reports)
print(count)

print(time.time()-t0)
time.sleep(.1)
print(time.time()-t0)
print(count)
print(time.time()-t0)
time.sleep(1)
print(count)
