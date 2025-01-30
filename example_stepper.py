#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import rp2daq
rp = rp2daq.Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'

zero_pos = {}
zero_pos[0] = rp.stepper_init(0, dir_gpio=12, step_gpio=13, endswitch_gpio=19, inertia=190).initial_nanopos
zero_pos[1] = rp.stepper_init(1, dir_gpio=10, step_gpio=11, endswitch_gpio=18, inertia=190).initial_nanopos
#zero_pos[0] = rp.stepper_init(0, dir_gpio=14, step_gpio=15, endswitch_gpio=0, inertia=450).initial_nanopos

#result = rp.stepper_move(0, 
        #to=zero_pos[0] - 2000000, 
        #speed=160, 
        #endswitch_ignore=0
        #) 
result = rp.stepper_move(1, 
        to=zero_pos[1] - 31500000, 
        speed=260, 
        reset_nanopos_at_endswitch=1
        ) 
print("The stepper has finished its movement, giving following return values:\n", result)
import time
time.sleep(.3)
result = rp.stepper_move(1, 
        to=zero_pos[1] + 1000000, 
        speed=560, 
        ) 
print("The stepper has finished its movement, giving following return values:\n", result)

print("Its status is:\n", rp.stepper_status(0))

