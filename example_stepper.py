#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import rp2daq
rp = rp2daq.Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'

zero_pos = {}
zero_pos[0] = rp.stepper_init(0, dir_gpio=14, step_gpio=15, endswitch_gpio=0, inertia=450)["initial_nanopos"] 
result = rp.stepper_move(0, 
        to=zero_pos[0] + 14000_000, 
        speed=160, 
        endswitch_ignore=0
        ) 
print("The stepper has finished its movement, giving following return values:\n", result)

print("Its status is:\n", rp.stepper_status(0))

