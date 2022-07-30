#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import time

import rp2daq
rp = rp2daq.Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'

print(rp.stepper_status(0))
print("Stepper 0 initialized:", )

#pos1 = [1,]
#print("STEPPER MOVE - async", )

def stepper_cb(**kwargs):
    print("...") 
    #time.sleep(1)
    if kwargs['endswitch_detected']:
        if kwargs['endswitch_expected']:
            zero_pos[st] = kwargs["nanopos"]  # recalibrate position against endstop
        else:
            print(" WARNING! UNEXPECTED ENDSWITCH EVENT!", time.time(), kwargs, pos0) 
            pos0[:] = []   # for safety, we stop commanding any further movements
            return

    try:
        if kwargs["stepper_number"] == 0:
            rp.stepper_move(0, to=zero_pos[0]+pos0.pop()*400000, speed=128*8, _callback=stepper_cb)
        else:
            rp.stepper_move(1, to=zero_pos[1]+pos1.pop()*400000, speed=128*3, _callback=stepper_cb)

        print("M.", time.time(), kwargs, pos0) 
    except IndexError:
        print("D.", time.time(), kwargs) 
        rp.quit()
    #print("CB", time.time(), kwargs) 

zero_pos = {}
pos0 = [2,4,1]

#print(rp.stepper_status(0))

for st in (0,):
    zero_pos[st] = rp.stepper_init(0, dir_pin=12, step_pin=13, endswitch_pin=19, disable_pin=25, inertia=60)["initial_nanopos"]
    rp.stepper_move(st, to=2**31-2000000, speed=128*2, _callback=stepper_cb)


while pos0: pass
time.sleep(.5)
#print(time.time())



#print("Stepper 1 initialized:")
#print(rp.stepper_init(1, dir_pin=10, step_pin=11, endswitch_pin=18, inertia=200))
#print(rp.stepper_status(1))
#print(rp.stepper_init(2, dir_pin=14, step_pin=15, endswitch_pin=17, inertia=30))
#print(rp.stepper_init(3, dir_pin=21, step_pin=20, endswitch_pin=16, inertia=30))

