#!/usr/bin/python3  
#-*- coding: utf-8 -*-
"""
rp2daq.py  (c) Filip Dominec 2021, MIT licensed

This is a thick wrapper around the binary message interface that makes Raspberry Pi Pico 
an universal laboratory interface.  More information on 

The methods provided here aim to make the hardware control as convenient as possible.  

More information and examples on https://github.com/FilipDominec/rp2daq
"""


NANOPOS_AT_ENDSTOP = 2**31 # half the range of uint32
NANOSTEP_PER_MICROSTEP = 256 # stepper resolution finer than microstep allows smooth speed control 
MINIMUM_POS = -2**22

CMD_IDENTIFY = 123
CMD_STEPPER_GO = 1
CMD_GET_STEPPER_STATUS =  3
CMD_INIT_STEPPER = 5 
CMD_SET_PWM = 20
CMD_INIT_PWM = 21 
# TODO SYMBOLS HERE

CMD_APPROACH =  2
CMD_GET_STM_STATUS =  4
CMD_SET_PIEZO =  9
CMD_LINESCAN = 10

import sys
import time
import tkinter
import os
import serial
import struct


def init_error_msgbox():  # error handling with a graphical message box
    import traceback, sys
    def myerr(exc_type, exc_value, tb): 
        message = '\r'.join(traceback.format_exception(exc_type, exc_value, tb))
        print(message)
        from tkinter import messagebox
        messagebox.showerror(title=exc_value, message=message)
    sys.excepthook = myerr

def init_settings(infile='settings.txt'):
    #infile = os.path.realpath(__file__)
    #print("DEBUG: infile = ", infile)
    settings = {}
    with open(infile) as f:
        for l in f.readlines():
            l = l.split('#')[0] # ignore comments
            k,v = [s.strip() for s in l.split('=', 1)]  # split at first '=' sign
            settings[k] = v
    return settings

class Rp2daq():
    def __init__(self, serial_port_names=None, required_device_tag=None):
        if not serial_port_names:
            serial_prefix = '/dev/ttyACM' if os.name=='posix' else 'COM' # TODO test if "COM" port assigned to rp2 on windows
            serial_port_names = [serial_prefix + str(port_number) for port_number in range(5)]

        if required_device_tag:
            if isinstance(required_device_tag, str): # conv
                required_device_tag = bytes.fromhex(required_device_tag.replace(":", ""))
            print(f"Trying to find an espdaq device with specified ID {required_device_tag.hex(':')}...")
        else:
            print(f"Trying to find any espdaq device...")
        for serial_port_name in serial_port_names:
            try:
                if os.name == 'posix':
                    import termios
                    with open(serial_port_name) as f: # 
                        attrs = termios.tcgetattr(f)
                        ## On Linux one needs first to disable the "hangup" signal, to prevent rp2daq randomly resetting. 
                        ## A command-line solution is: stty -F /dev/ttyUSB0 -hup
                        attrs[2] = attrs[2] & ~termios.HUPCL
                        termios.tcsetattr(f, termios.TCSAFLUSH, attrs)

                # Standard serial port settings, as hard-coded in the hardware
                # FIXME N/A for ACM class: baudrate=baudrate, bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE, 
                self.port = serial.Serial(port=serial_port_name, timeout= 0.1) # default is "8N1"

                # TODO port.write(struct.pack(r'<B', CMD_IDENTIFY)
                # time.sleep(.1): port.read(30)

                try:
                    raw = self.identify()
                except:
                    raw = b''

                if not raw[:6] == b'rp2daq': # needed: implement timeout!
                       print(f"Port {serial_port_name} exists, but does not report as rp2daq")
                       del(self.port)
                       continue

                #if required_device_tags: 
                    #for required_device_tag in required_device_tags:
                if required_device_tag:
                    if raw[6:14] != required_device_tag:
                        print(f"Skipping the rp2daq device on port {serial_port_name}, with ID {raw[6:14].hex(':')}")
                        del(self.port)
                        continue

                print(f"Connecting to rp2daq device with manufacturer ID = {raw[6:14].hex(':')} on port {serial_port_name}")
                return # succesful init of the port with desired device

            except IOError:
                pass        # termios is not available on Windows, but probably also not needed
        # if select_device_tag=None
        if not hasattr(self, "port"):
            raise RuntimeError("Error: rp2daq device initialization failed") 

        return
        #raise RuntimeError("Could not connect to the rp2daq device" + 
                #(f"with tag"+required_device_tag if required_device_tag else ""))
        #raise RuntimeError("Could not connect to the rp2daq device" + 
                #(f"with tag"+required_device_tag if required_device_tag else ""))


    def identify(self):
        self.port.write(struct.pack(r'<B', CMD_IDENTIFY))
        raw = self.port.read(30)
        devicename = raw[0:6]
        unique_id = (raw[6:14]).hex(':')
        return raw

    def init_stepper(self, motor_id, dir_pin, step_pin, endswitch_pin, disable_pin, motor_inertia=128, reset_nanopos=False):
        self.port.write(struct.pack(r'<BBBBBBIB', 
            CMD_INIT_STEPPER, 
            motor_id, 
            dir_pin, 
            step_pin, 
            endswitch_pin, 
            disable_pin, 
            motor_inertia, 
            1 if reset_nanopos else 0))


    def get_stepper_status(self, motor_id):       
        """ Universal low-level stepper motor control: returns a dict indicating whether the motor is running, 
        whether it has touched an end switch and an integer of the motor's current microstep count. """
        self.port.write(struct.pack(r'<BB', CMD_GET_STEPPER_STATUS, motor_id))
        raw = self.port.read(6)
        #print('STEPPER RAW', raw)
        vals = list(struct.unpack(r'<BBI', raw))
        vals[2] = (vals[2] - NANOPOS_AT_ENDSTOP) / NANOSTEP_PER_MICROSTEP
        #print('    --> STEPPER STATUS', vals)
        return dict(zip(['active', 'endswitch', 'micropos'], vals))
        #try:
        #except:
            #return dict(zip(['active', 'endswitch', 'micropos'], [0,0,0]))


    def stepper_go(self, motor_id, target_micropos, nanospeed=256, endstop_override=False, 
            reset_zero_pos=False, wait=False): 
        """ Universal low-level stepper motor control: sets the new target position of the selected
        stepper motor, along with the maximum speed to be used. """
        # FIXME in firmware: nanospeed should allow (a bit) more than 256
        if target_micropos < MINIMUM_POS: 
            target_micropos = MINIMUM_POS
        self.port.write(struct.pack(r'<BBIIBB', CMD_STEPPER_GO, motor_id, 
                target_micropos*NANOSTEP_PER_MICROSTEP + NANOPOS_AT_ENDSTOP, nanospeed, 
                1 if endstop_override else 0,
                1 if reset_zero_pos else 0))
        #print(motor_id, target_micropos*NANOSTEP_PER_MICROSTEP)
        if wait:
            while self.get_stepper_status(motor_id=motor_id)['active']: 
                time.sleep(.1)   

    
    def init_pwm(self, assign_channel=1, assign_pin=19, bit_resolution=16, freq_Hz=100, init_value=6654):
        self.port.write(struct.pack(r'<BBBBII', 
            CMD_INIT_PWM, 
            assign_channel, 
            assign_pin, 
            bit_resolution,
            freq_Hz,
            init_value))

    def set_pwm(self, val, channel=1):
        self.port.write(struct.pack(r'<BBI', 
            CMD_SET_PWM, 
            channel, 
            int(val)))


    def get_stm_status(self):
        self.port.write(struct.pack(r'<B', CMD_GET_STM_STATUS))
        raw = []
        for count in range(100):
            time.sleep(.10)
            raw = self.port.read(2000*2)
            if len(raw)==4000: break
            print('waiting extra time for serial data...')
            self.port.write(struct.pack(r'<B', CMD_GET_STM_STATUS))

        status = dict(zip(['tip_voltage'], [struct.unpack(r'<2000H', raw)]))
        #status['stm_data'] = struct.unpack(r'<{:d}h'.format(status['stm_data_len']//2), self.port.read(status['stm_data_len']))
        return status



    ## Additional useful functions 

    def calibrate_stepper_positions(self, motor_ids, minimum_micropos=-1000000, 
            nanospeed=256, bailout_micropos=1000):

        ## Accept single-valued target_micropos and/or nanospeed, even if a list of motor_ids is supplied
        if not hasattr(motor_ids, "__getitem__"): motor_ids = [motor_ids]
        if not hasattr(minimum_micropos, "__getitem__"): minimum_micropos = [minimum_micropos]*len(motor_ids)
        if not hasattr(nanospeed, "__getitem__"): nanospeed = [nanospeed]*len(motor_ids)
        if not hasattr(bailout_micropos, "__getitem__"): bailout_micropos = [bailout_micropos]*len(motor_ids)

        ## Initialize the movement towards endstop(s)
        for n,motor_id in enumerate(motor_ids):
            self.stepper_go(motor_id=motor_id, 
                target_micropos=minimum_micropos[n], nanospeed=nanospeed[n], wait=False, endstop_override=0)

        ## Wait until all motors are done, and optionally wait until they bail out to 
        some_motor_still_busy = True
        while some_motor_still_busy:
            time.sleep(.05)
            some_motor_still_busy = False
            for n, motor_id in enumerate(motor_ids):
                status = self.get_stepper_status(motor_id=motor_id)
                print(status)
                if not status['active']:
                    if status['endswitch']:
                        self.stepper_go(motor_id=motor_id, target_micropos=bailout_micropos[n], 
                                nanospeed=nanospeed[n], 
                                wait=False, endstop_override=1, reset_zero_pos=1)
                    else:
                        print(f"Warning: motor {motor_id} finished its move but did not reach endstop")
                else:
                    some_motor_still_busy = True



# https://www.aranacorp.com/en/using-the-eeprom-with-the-rp2daq/
