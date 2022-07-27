#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import time

import rp2daq
rp = rp2daq.Rp2daq()

# Let's make some artificial signal on pin 0
rp.pwm_configure_pair(0, clkdiv=255, wrap_value=3000) # can go to some 6000 reports/s
rp.pwm_set_value(0, 200) # thin spikes


## Define a report handler and ... 
count = [0]
def handler(**kwargs): 
    count[0] += 1
    print("HANDLER", count, kwargs)

## ... start asynchronous reporting on each pin change
rp.pin_on_change(0, _callback=handler, on_rising_edge=1, on_falling_edge=0)

## Wait one second to collect some reports
time.sleep(1)
print(count)

## Note one or few more reports may be on their way yet
#rp.pin_on_change(0, _callback=handler, )
#time.sleep(.1)
#print(count)
#rp2daq.quit() ## safe exit with pending busy printouts
#time.sleep(.1)
