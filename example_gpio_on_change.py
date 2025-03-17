#!/usr/bin/python3  
#-*- coding: utf-8 -*-

# This example first sets up an independent square wave generator using the PWM command. 
# Then it hooks up the gpio_on_change callback on every change of this PWM signal, and 
# counts the incoming messages. Aside of showing how gpio_on_change asynchronously reports 
# leading/falling edges on a given pin, it is a test of how fast the reports can be sent.
# No hardware preparation on the board is necessary.

# 50 000 messages per second seems at the edge of what rp2daq handles; higher frequency results 
# in reports being silently dropped. 

wait_sec = 1.0
clkdiv = 250       # Since system clock is 250 MHz, so 250 corresponds to 1 MHz
wrap_value = 20-1  # Dividing 1 MHz by 20 results in 50 000 reports/s
pwm_frequency = 250000/clkdiv/(wrap_value+1) 



import time

import rp2daq
rp = rp2daq.Rp2daq()

print(f'Configuring PWM channel to make artificial {pwm_frequency} kHz square wave')
rp.pwm_configure_pair(gpio=0, 
        clkdiv=clkdiv,
        wrap_value=wrap_value)

time.sleep(.08) # unclear why, but a margin of ~100 ms is better for PWM to start
rp.pwm_set_value(gpio=0, value=1)  # short 1us spikes



print(f'Registering rising edge events for {wait_sec} seconds...')

count = [0]
def event_counter(rv): 
    ## Define a report handler 
    count[0] += 1

rp.gpio_on_change(gpio=0, 
        on_rising_edge=True, 
        on_falling_edge=False, 
        _callback=event_counter) # Start asynchronous reporting on each GPIO change

time.sleep(wait_sec)



print('Switching off the PWM signal')

rp.pwm_set_value(0, 0) # stop PWM signal (nearly immediate - waits for all previous reports)



print(f'Number of received edge reports after {wait_sec} seconds = ', count[0])


count = [0] # reset the counter, but let's check if event_counter will be called
time.sleep(wait_sec)
print(f'Extra edge reports received from queue after another {wait_sec} s = ', count[0])
