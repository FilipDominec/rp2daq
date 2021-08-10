#!/usr/bin/python3  
#-*- coding: utf-8 -*-

NANOPOS_AT_ENDSTOP = 2**31 # half the range of uint32
NANOSTEP_PER_MICROSTEP = 256 # stepper resolution finer than microstep allows smooth speed control 
MINIMUM_POS = -2**22

CMD_IDENTIFY = 123
CMD_MOVE_SYMBOL = 1
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


        self.port = None
        if required_device_tag:
            if isinstance(required_device_tag, str): # conv
                required_device_tag = bytes.fromhex(required_device_tag.replace(":", ""))
            print(f"Trying to connect to an espdaq device with specified ID {required_device_tag.hex(':')}")
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
                       continue

                #if required_device_tags: 
                    #for required_device_tag in required_device_tags:
                if required_device_tag:
                    if raw[6:14] != required_device_tag:
                        print(f"Skipping the rp2daq device on port {serial_port_name}, with ID {raw[6:14].hex(':')}")
                        continue

                print(f"Connecting to rp2daq device manufacturer ID = {raw[6:14].hex(':')} on port {serial_port_name}")
                return # succesful init of the port with desired device

            except IOError:
                pass        # termios is not available on Windows, but probably also not needed
        # if select_device_tag=None
        if self.port == None:
            print(f"Error: could not open any relevant USB port")

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
        another whether it is on end switch and an integer of the motor's current microstep count. """
        self.port.write(struct.pack(r'<BB', CMD_GET_STEPPER_STATUS, motor_id))
        raw = self.port.read(6)
        #print('STEPPER RAW', raw)
        vals = list(struct.unpack(r'<BBI', raw))
        vals[2] = (vals[2] - NANOPOS_AT_ENDSTOP) / NANOSTEP_PER_MICROSTEP
        #print('    --> STEPPER STATUS', vals)
        try:
            return dict(zip(['active', 'endswitch', 'nanopos'], vals))
        except:
            return dict(zip(['active', 'endswitch', 'nanopos'], [0,0,0]))

    def reset_stepper_position(self, position=NANOPOS_AT_ENDSTOP):
        pass # TODO 
        #raise NotImplementedError


    def stepper_move(self, motor_id, target_micropos, nanospeed=256, endstop_override=False, wait=False): 
        """ Universal low-level stepper motor control: sets the new target position of the selected
        stepper motor, along with the maximum speed to be used. """
        # FIXME in firmware: nanospeed should allow (a bit) more than 256
        if target_micropos < MINIMUM_POS: 
            target_micropos = MINIMUM_POS
        self.port.write(struct.pack(r'<BBIIB', CMD_MOVE_SYMBOL, motor_id, 
                target_micropos*NANOSTEP_PER_MICROSTEP + NANOPOS_AT_ENDSTOP, nanospeed, 
                1 if endstop_override else 0))
        print(motor_id, target_micropos*NANOSTEP_PER_MICROSTEP)
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


# https://www.aranacorp.com/en/using-the-eeprom-with-the-rp2daq/
