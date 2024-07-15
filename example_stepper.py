#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import time
import threading

import rp2daq
rp = rp2daq.Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'

zero_pos = {}
zero_pos[0] = rp.stepper_init(0, dir_gpio=14, step_gpio=15, endswitch_gpio=0, inertia=500)["initial_nanopos"] 
result = rp.stepper_move(0, 
        to=zero_pos[0] - 500_000, 
        speed=1600, 
        endswitch_ignore=0
        ) 


print("The stepper has finished its movement, quitting with following return values:\n", result)

rp.quit()

print("Its status is:\n", rp.stepper_status(0))

time.sleep(.1)

