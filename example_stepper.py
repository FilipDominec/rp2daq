#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import time
import threading

import rp2daq
rp = rp2daq.Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'

## 1.  Define tuples of coordinates the steppers will go to
##     (note we [mis]use mutable data types to act as global variables)
coords_to_go = [(3,3), (2,2), (1,1), ]*100


## 2.  Initialize steppers, remember the numeric position they are assigned. Your pin assignments may vary.
zero_pos = {}
zero_pos[0] = rp.stepper_init(0, dir_pin=12, step_pin=13, endswitch_pin=19, inertia=90)["initial_nanopos"]
zero_pos[1] = rp.stepper_init(1, dir_pin=10, step_pin=11, endswitch_pin=18, inertia=90)["initial_nanopos"]
#print('\n'*3); time.sleep(.05)
#zero_pos[2] = rp.stepper_init(2, dir_pin=14, step_pin=15, endswitch_pin=17, inertia=30)["initial_nanopos"]
#zero_pos[3] = rp.stepper_init(3, dir_pin=21, step_pin=20, endswitch_pin=16, inertia=30)["initial_nanopos"]


## 3. Define callback that feeds both steppers with new coords - but only if *all* have finished a move!
def stepper_cb(**kwargs):
    ## i. End switches are useful for position calibration (and to prevent dangerous crashes later)
    if kwargs['endswitch_triggered']:
        if kwargs['endswitch_expected']:
            zero_pos[kwargs["stepper_number"]] = kwargs["nanopos"]  # recalibrate position against endstop
        else:
            print("Error: Unexpected endswitch trigger on motor #{kwargs['stepper_number']}!") 
            coords_to_go[:] = []   # for safety, we flush further movements, so this script ends

    ## ii. Check if whole stepper group is done
    print('-- here we are to poll status --')
    print(kwargs)
    print(rp.stepper_status(0))
    print()
    status = rp.stepper_status(0)
    if status["steppers_moving_bitmask"]:
        return # some stepper is still busy - wait for its callback

    ## iii. Feed whole stepper group with new coordinates to go
    try:
        new_coords = coords_to_go.pop(0)
        for st in (0,1):
            print(zero_pos[st],new_coords)
            rp.stepper_move(st, 
                    to=zero_pos[st]+new_coords[st] *400000, 
                    speed=128*(2+5*st), 
                    _callback=stepper_cb)
    except IndexError: # i.e. if the last move finished
        #coords_to_go[:] = ["last job done"]
        all_steppers_done.set()
    #print("CB", time.time(), kwargs) 


## 3.  Launch the first move for each stepper here (further moves will be launched within the callback)
##      Note the position units: typically 200 steps per revolution, 16x microstepping x 256 "nanosteps" =
##      one revolution in 819200 nanosteps
##      Speed of rotation is given in nanosteps per 100 us cycle; i.e. 82 is 1 revolution / s.
for st in (0,1):
    rp.stepper_move(st, to=zero_pos[0]-10000000, speed=128*2, _callback=stepper_cb) # moving towards 0 = seeking end switch
    print('\n'*3); time.sleep(.05)

print(dir(rp))

## 4.  Wait here until the last movement finishes 
all_steppers_done = threading.Event() # a thread-safe semaphore
all_steppers_done.wait()
time.sleep(.1)



