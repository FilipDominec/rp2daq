#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import rp2daq, time
rp = rp2daq.Rp2daq()

# optimum settings to control a small servo (190 Hz) 
# typically the timing range is 0.8 .. 2.2 ms pulse width, but YMMV
pin = 8
rp.pwm_configure_pair(pin=pin, wrap_value=65535, clkdiv=20, clkdiv_int_frac=0)
rp.pwm_set_value(pin=pin, value=10000) # minimum position
#time.sleep(1)
rp.pwm_set_value(pin=pin, value=27500) # maximum position


