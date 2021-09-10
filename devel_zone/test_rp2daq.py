#!/usr/bin/python3
#-*- coding: utf-8 -*-

"""

Module dependencies: tkinter, serial, struct

"""

# TODO keyboard shortcuts; most
# TODO saving recent few 
# big optional TODO PS/2 keyboard sniffer

# TODO TEST NEW FEATURES 2021-01-08


import sys, os, time
sys.path.append('..')  # DEVEL this is a trick to import a local module from updir
import rp2daq 
import tkinter as tk


#rp2daq.init_error_msgbox()
#quit()

settings = rp2daq.init_settings()


hw = rp2daq.Rp2daq(required_device_tag = None, required_firmware_version=0, verbose=True)  #  'e6:60:58:38:83:48:89:2d'

while True:
    hw.set_pin(2, 0)
    hw.set_pin(0, 0, 0)
    hw.set_pin(4, 0)
    time.sleep(.1)
    hw.set_pin(2, 1)
    hw.set_pin(0, 1, 0 )
    hw.set_pin(4, 1)
    time.sleep(.1)
quit()

## Testing ADC input & plotting a graph
#import matplotlib.pyplot as plt, numpy as np
#fig, ax = plt.subplots(nrows=1, ncols=1, figsize=(7, 7))
#x,y = [], []
#t0 = time.time()
#for n in range(20):
    #y.append(hw.get_ADC(26, oversampling_count=100))
    #x.append(time.time() - t0)
    #time.sleep(.1)
#ax.plot(y)
#fig.savefig("output2.png", bbox_inches='tight')

hw.init_stepper(motor_id=0, dir_pin=12, step_pin=13, endswitch_pin=19, disable_pin=0, motor_inertia=256*2)
hw.init_stepper(motor_id=1, dir_pin=10, step_pin=11, endswitch_pin=18, disable_pin=0, motor_inertia=256*2)
#hw.init_stepper(motor_id=2, dir_pin=14, step_pin=15, endswitch_pin=17, disable_pin=0, motor_inertia=128)
#hw.init_stepper(motor_id=3, dir_pin=21, step_pin=20, endswitch_pin=16, disable_pin=0, motor_inertia=128)
hw.calibrate_stepper_positions(motor_ids=(0,1), minimum_micropos=-10000, nanospeed=(256,128), bailout_micropos=1000 )

#c = 0
#while True:
    #res  = hw.identify()
    #if (not c%1000) or (res != b'{rp2daq\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'):
        #print(c, ":", res)
    #print(c, ":", hw.get_stepper_status(motor_id=1))
    #print(c, ":", hw.get_stm_status())
    #c += 1
    #time.sleep(.01)

#hw.stepper_go(motor_id=0, target_micropos=10000, nanospeed=64*2, wait=True, endstop_override=1)

#hw.wait_stepper_idle(motor_ids=(0,1))

#hw.stepper_go(motor_id=0, target_micropos=3000, nanospeed=32, wait=True, endstop_override=1)

#time.sleep(.5)

quit()

    #hw.stepper_go(motor_id=1, target_micropos=300, nanospeed=32, wait=True, endstop_override=False)
    #hw.stepper_go(motor_id=0, target_micropos=2000, nanospeed=64)
    #hw.stepper_go(motor_id=1, target_micropos=3000, nanospeed=32, wait=True, endstop_override=False)
#hw.stepper_go(motor_id=0, target_micropos=100, nanospeed=12, endstop_override=True)
#hw.stepper_go(motor_id=1, target_micropos=100, nanospeed=4, wait=True, endstop_override=True)
quit() ################################################################################

MONO_MOTOR_ID = 0
VERT_MOTOR_ID = 1

## == Hardware-communication routines =={{{
def vmotor(choice):

    ## send the message to the hardware
    if choice == 'reset': 
        target_micropos = float('-inf')
    else:
        target_micropos = int(settings['vpos_'+choice])
    #values = struct.pack(r'<BBii', CMD_MOVE_SYMBOL, VERT_MOTOR_ID, target_micropos*NANOSTEP_PER_MICROSTEP, 64)
    #port.write(values)
    print('VMOTOR', target_micropos)
    hw.stepper_go(motor_id=VERT_MOTOR_ID, target_micropos=target_micropos, nanospeed=16)

    ## receive response from the hardware
    time.sleep(0.01)
    #total = 0
    #while total < 10:
        #try:
            #print(ord(hw.port.read(1)))
        #except TypeError: 
            #break
        #total=total+1

    ## visual GUI feedback (TODO indicate that the movement was really finished)
    for k,v in button_dict.items(): v.config(relief=tk.SUNKEN if k == choice else tk.RAISED)
    for k,v in frame_dict.items():  v.config(bg='green2' if k == choice else BGCOLOR)
    lbl_wavelength.config(bg='green2' if choice=='monochro' else BGCOLOR)
    ent_wavelength.config(state=tk.NORMAL if choice=='monochro' else tk.DISABLED)
    btn_wavelength.config(state=tk.NORMAL if choice=='monochro' else tk.DISABLED)

def mmotor(target_micropos):
    #port.write(struct.pack(r'<BBii', CMD_MOVE_SYMBOL, MONO_MOTOR_ID, target_micropos*NANOSTEP_PER_MICROSTEP, 1*256))
    hw.stepper_go(motor_id=MONO_MOTOR_ID, target_micropos=target_micropos, nanospeed=2*32)

def mmotor_by_wl(target_wavelength_nm):
    ## (re)load the monochromator calib
    nm1, nm2  = float(settings['calib1_nm']), float(settings['calib2_nm']), 
    s1, s2    = float(settings['calib1_stepper']), float(settings['calib2_stepper'])
    target_micropos = int((target_wavelength_nm-nm1)/(nm2-nm1)*(s2-s1)+s1)
    print("DEBUG: target_micropos = ", target_micropos)
    mmotor(target_micropos)

def mmotor_by_entry(*args):
    try: 
        print("DEBUG: ent_wavelength = ", ent_wavelength.get())
        target_wavelength_nm = float(ent_wavelength.get())
        print("DEBUG: target_wavelength_nm = ", target_wavelength_nm)
        ent_wavelength.config(bg='#ffffff')
    except:
        ent_wavelength.config(bg='#ff8800')
    print("DEBUG: target_wavelength_nm = ", target_wavelength_nm)
    print( settings['min_limit_nm']  + settings['max_limit_nm'])
    target_wavelength_nm = min(max(target_wavelength_nm, float(settings['min_limit_nm'])), float(settings['max_limit_nm']))
    txt_wavelength.set('{:6.2f}'.format(target_wavelength_nm))
    mmotor_by_wl(target_wavelength_nm)
# }}}

## == GUI init == 
root = tk.Tk()
root.geometry("300x330+300+300")
root.wm_title("CL control")
#root.pack_propagate(0) # don't shrink

btnkwarg = {'padx':100, 'pady':20}
frm_panchro = tk.Frame(root, height=32, width=32); frm_panchro.pack(padx=5, pady=5, side=tk.TOP)
btn_panchro = tk.Button(frm_panchro, text='Panchromatic imaging', command=lambda:vmotor('panchro'), **btnkwarg) # fill=tk.X, ipady=10, 
btn_panchro.pack(padx=5, pady=5, side=tk.TOP)
BGCOLOR = btn_panchro.cget('bg')

frm_spectro = tk.Frame(root, height=32, width=32); frm_spectro.pack(padx=5, pady=5, side=tk.TOP)
btn_spectro = tk.Button(frm_spectro, text='Spectrometer', command=lambda:vmotor('spectro'),  **btnkwarg)
btn_spectro.pack(padx=5, pady=5, side=tk.TOP)

frm_monochro = tk.Frame(root, height=32, width=32); frm_monochro.pack(padx=5, pady=5, side=tk.TOP)
btn_monochro = tk.Button(frm_monochro, text='Monochromator imaging', command=lambda:vmotor('monochro'), **btnkwarg)
btn_monochro.pack(padx=5, pady=5, side=tk.TOP)
lbl_wavelength = tk.Label(text='Wavelength (nm)', master=frm_monochro ); lbl_wavelength.pack(padx=5, pady=5, side=tk.LEFT)
txt_wavelength = tk.StringVar()
ent_wavelength = tk.Entry(frm_monochro, text='nm', textvariable=txt_wavelength, width=10); 
ent_wavelength.pack(padx=5, pady=5, side=tk.LEFT)
ent_wavelength.bind('<Return>', mmotor_by_entry)
btn_wavelength = tk.Button(frm_monochro, text='set!', command=mmotor_by_entry); btn_wavelength.pack(padx=5, pady=5, side=tk.LEFT)


frm_endstop = tk.Frame(root, height=22); frm_endstop.pack(padx=5, pady=5, side=tk.TOP)
btn_mgotovend = tk.Button(frm_endstop, text='Vertical zero', command=lambda:[vmotor('reset')], bg='#eebbbb', padx=10, pady=2) # fill=tk.X, ipady=10, 
btn_mgotovend.pack(padx=5, pady=5, side=tk.LEFT)
btn_mgotomend = tk.Button(frm_endstop, text='Monochromator zero', command=lambda:[mmotor('reset')], bg='#eebbbb', padx=10, pady=2) # fill=tk.X, ipady=10, 
btn_mgotomend.pack(padx=5, pady=5, side=tk.LEFT)

frame_dict = {'panchro':frm_panchro, 'spectro':frm_spectro, 'monochro':frm_monochro}
button_dict = {'panchro':btn_panchro, 'spectro':btn_spectro, 'monochro':btn_monochro}



## == responsive GUI loop and termination == 

tk.mainloop()
#port.close()

