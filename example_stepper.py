#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import time
import threading

import rp2daq
rp = rp2daq.Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'

zero_pos = {}
zero_pos[0] = rp.stepper_init(0, dir_gpio=14, step_gpio=15, endswitch_gpio=0, inertia=9000)["initial_nanopos"] # blue board
#zero_pos[0] = rp.stepper_init(0, dir_gpio=12, step_gpio=13, endswitch_gpio=19, inertia=9000)["initial_nanopos"] # green board
result = rp.stepper_move(0, 
        to=zero_pos[0] - 200 * 3330, 
        speed=6*(1), 
        endswitch_ignore=0
        ) 

print("The stepper has finished its jobs, quitting with following return values:\n", result)
time.sleep(.1)

