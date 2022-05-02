#!/usr/bin/python3  
#-*- coding: utf-8 -*-
"""
rp2daq.py  (c) Filip Dominec 2021, MIT licensed

This is a thick wrapper around the binary message interface that makes Raspberry Pi Pico 
an universal laboratory interface.  

The methods provided here aim to make the hardware control as convenient as possible.  

More information and examples on https://github.com/FilipDominec/rp2daq
"""


MIN_FW_VER = 210400

NANOPOS_AT_ENDSTOP = 2**31 # half the range of uint32
NANOSTEP_PER_MICROSTEP = 256 # stepper resolution finer than microstep allows smooth speed control 
MINIMUM_POS = -2**22

CMD_IDENTIFY = 0  # blink LED, return "rp2daq" + datecode (6B) + unique 24B identifier
CMD_STEPPER_GO = 1
CMD_GET_STEPPER_STATUS =  3
CMD_INIT_STEPPER = 5 
CMD_GET_PIN = 6
CMD_SET_PIN = 7 
CMD_SET_PWM = 20
CMD_INIT_PWM = 21 
# TODO SYMBOLS HERE

CMD_GET_ADC = 22

#CMD_APPROACH =  102 # disused
#CMD_GET_STM_STATUS =  104 # disused
#CMD_SET_PIEZO =  109 # disused
#CMD_LINESCAN = 1010 # disused

from collections import deque
import os
import serial
import struct
import sys
import threading
import time
import tkinter
from serial.tools import list_ports 

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

communication_code = """
def identify(self, x,y,):

        self.port.write(struct.pack('BBBB', 4, 0, 
                x,
            y,))
self.identify2 = identify
"""


class Rp2daq(threading.Thread):
    def __init__(self, required_device_id=None, verbose=True):

        self.verbose = verbose
        self.log_text = "" 
        self.start_time = time.time()

        self.sleep_tune = 0.01
        
        exec(communication_code)

        self.port = self._initialize_port(required_device_id=None, required_firmware_version=MIN_FW_VER)
        self.serial_port = self.port # tmp. compat


        # INSPIRED BY TMX
        self.shutdown_flag = False # xx
        threading.Thread.__init__(self)
        self.the_reporter_thread = threading.Thread(target=self._reporter)
        self.the_reporter_thread.daemon = True
        self.the_data_receive_thread = threading.Thread(target=self._serial_receiver)

        self.run_event = threading.Event()

        self.the_reporter_thread.start()
        self.the_data_receive_thread.start()

        self.the_deque = deque()

        # allow the threads to run
        self._run_threads()

        self.identify()




    def _run_threads(self):
        self.run_event.set()

    def _is_running(self):
        return self.run_event.is_set()

    def _stop_threads(self):
        self.run_event.clear()

    def _reporter(self):
        """
        This is the reporter thread. It continuously pulls data from
        the deque. When a full message is detected, that message is
        processed.
        """
        self.run_event.wait()

        while self._is_running() and not self.shutdown_flag:
            if len(self.the_deque):
                # response_data will be populated with the received data for the report
                response_data = []
                packet_length = 29 if self.the_deque.popleft() else 0

                if packet_length:
                    print("packet_length",packet_length)
                    # get all the data for the report and place it into response_data
                    for i in range(packet_length):
                        while not len(self.the_deque):
                            time.sleep(self.sleep_tune)
                        data = self.the_deque.popleft()
                        response_data.append(data)
                    print(' -- ', bytes(response_data).decode('utf-8'))

                    # get the report type and look up its dispatch method
                    # here we pop the report type off of response_data
                    report_type = response_data.pop(0)

                    # retrieve the report handler from the dispatch table
                    #dispatch_entry = self.report_dispatch.get(report_type)

                    # if there is additional data for the report,
                    # it will be contained in response_data
                    # noinspection PyArgumentList
                    #dispatch_entry(response_data)
                    print(response_data)
                    continue

                else:
                    if self.shutdown_on_exception:
                        self.shutdown()
                    raise RuntimeError(
                        'A report with a packet length of zero was received.')
            else:
                time.sleep(self.sleep_tune)

    def _serial_receiver(self):
        """
        Thread to continuously check for incoming data.
        When a byte comes in, place it onto the deque.
        """
        self.run_event.wait()

        while self._is_running() and not self.shutdown_flag:
            # we can get an OSError: [Errno9] Bad file descriptor when shutting down
            # just ignore it
            try:
                if self.serial_port.inWaiting():
                    c = self.serial_port.read()
                    self.the_deque.append(ord(c))
                else:
                    time.sleep(self.sleep_tune)
                    # continue
            except OSError:
                pass

    # TODO implement async inspired by tmx
    #def _report_receiver_thread(): # polls the port for incoming messages
        #while True:
            #time.sleep(.1)
            #print(time.time())


    # TODO autogenerate e.g. the "self.internal_adc(..., cb=self._blocking_CB)" command as a new method of this class
    # TODO test it with _blocking_CB() - a loop polling a semaphore must (?) delay this method from returning
    # note: it seems OK that multiple reports (i.e. blocks) returned from device would not be all serviced by 
    #   the _blocking_CB method, as it just waits for the first report arriving and returns it
    #   user has to implement their cb themselves if multiple/infinite reports expected


    def _blocking_CB(): # 
        """
        Default blocking callback (useful for immediate responses from device, )
        """

    def _log(self, message, end="\n"):# {{{
        """
        Like the print() command, but all debugging info is also stored in self.log_text to remain 
        accessible in graphical user interfaces.
        """
        message = message.replace('\n','\n\t\t') # visual align indent with timestamps
        if self.log_text[-1:] in ('', '\n'):
            message = f"[{time.time()-self.start_time:13.6f}] {message}"
        self.log_text += f"{message}{end}"
        if self.verbose: 
            print(f"{message}{end}", end="")
# }}}
    def _initialize_port(self, required_device_id, required_firmware_version):# {{{
        """
        Seeks for a compatible rp2daq device on USB, checking for its firmware version and, if 
        specified, for its particular unique vendor name.
        """

        #serial_prefix = '/dev/ttyACM' if os.name=='posix' else 'COM' # TODO test if "COM" port assigned to rp2 on windows
        #serial_port_names = [serial_prefix + str(port_number) for port_number in range(5)]

        # TODO new elegant approach to port autodetection
        PID, VID = 10, 11914
        port_list = list_ports.comports() # DEBUG
        #print(f"{port_list=}")
        #for port in port_list: self._log(f"{port=} PID={port.pid} VID={port.vid}")

        for port_name in port_list:
            self._log(f"trying port {port_name.name}", end="")
            #'apply_usb_info', 'description', 'device', 'device_path', 'hwid', 'interface', 'location', 'manufacturer', 'name', 'pid', 'product', 'read_line', 'serial_number', 'subsystem', 'usb_description', 'usb_device_path', 'usb_info', 'usb_interface_path', 'vid'  {port_name.description=} {port_name.hwid=} {port_name.manufacturer=} {port_name.serial_number=} 

            port = serial.Serial(port=port_name.device, timeout=0.1)


            try:
                #raw = self.identify()
                port.write(struct.pack(r'<BB', 1, 0)) # hard-coded implementation of "identify" command
                time.sleep(.05) # 20ms round-trip time is enough
                bytesToRead = port.inWaiting() 
                if bytesToRead == 30: raw = port.read(bytesToRead)
            except:
                raw = b''

            print(f"{raw=}")
            if not raw[:6] == b'rp2daq': # needed: implement timeout!
                self._log(f"port open, but device does not identify itself as rp2daq")
                #del(self.port)
                continue


            version_signature = raw[7:13]
            if not version_signature.isdigit() or int(version_signature) < required_firmware_version:
                self._log(f"rp2daq device firmware has version {version_signature.decode('utf-8')},\n" +\
                        f"older than this script's {MIN_FW_VER}.\nPlease upgrade firmware " +\
                        "or override this error using 'required_firmware_version=0'.")
                #del(self.port)
                continue

            if isinstance(required_device_id, str): # optional conversion
                required_device_id = bytes.fromhex(required_device_id.replace(":", ""))
            if required_device_id and raw[12:20] != required_device_id:
                self._log(f"found an rp2daq device, but its ID {raw[12:20].hex(':')} does not match " + 
                        f"required {required_device_id.hex(':')}")
                #del(self.port)
                continue

            self._log(f"connected to rp2daq device with manufacturer ID = {raw[12:20].hex(':')}")
            return port

        else:
            msg = "Error: could not find any matching rp2daq device"
            self._log(msg)
            raise RuntimeError(msg)
# }}}

    def identify(self): 
        self.port.write(struct.pack(r'<B', CMD_IDENTIFY))
        raw = self.port.read(30)
        #devicename = raw[0:6]
        #unique_id = (raw[6:14]).hex(':')
        return raw

    def init_stepper(self, motor_id, dir_pin, step_pin, endswitch_pin, disable_pin, motor_inertia=128):
        self.port.write(struct.pack(r'<BBBBBBI', 
            CMD_INIT_STEPPER, 
            motor_id, 
            dir_pin, 
            step_pin, 
            endswitch_pin, 
            disable_pin, 
            motor_inertia))

    def get_stepper_status(self, motor_id):       
        """ Universal low-level stepper motor control: returns a dict indicating whether the motor is running, 
        whether it has touched an end switch and an integer of the motor's current microstep count. """
        self.port.write(struct.pack(r'<BB', CMD_GET_STEPPER_STATUS, motor_id))
        time.sleep(.05)
        raw = self.port.read(6)
        print('STEPPER RAW', raw)
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
        raw = struct.pack(r'<BBIIBB', 
                CMD_STEPPER_GO, 
                motor_id, 
                max(MINIMUM_POS, target_micropos)*NANOSTEP_PER_MICROSTEP + NANOPOS_AT_ENDSTOP, 
                nanospeed, 
                1 if endstop_override else 0,
                1 if reset_zero_pos else 0)
        self.port.write(raw)

        if wait: 
            self.wait_stepper_idle(motor_id)

    def get_pin(self, pin): # TODO TEST
        assert pin <= 28
        self.port.write(struct.pack(r'<BB', CMD_GET_PIN, pin))	
        raw = self.port.read(1)
        return raw

    def set_pin(self, pin, value, output_mode=True): # TODO TEST
        assert pin <= 28
        self.port.write(struct.pack(r'<BBBB', CMD_SET_PIN, pin, 1 if value else 0, 1 if output_mode else 0))	


    def get_ADC(self, adc_pin=26, oversampling_count=16):
        assert adc_pin in (26,27,28)
        self.port.write(struct.pack(r'<BBB', CMD_GET_ADC, adc_pin, oversampling_count))	
        raw = self.port.read(4)
        print(raw)
        return struct.unpack(r'<I', raw)

    
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

    def wait_stepper_idle(self, motor_ids, poll_delay=0.05):
        if not hasattr(motor_ids, "__getitem__"): motor_ids = [motor_ids]
        while any([self.get_stepper_status(m)['active'] for m in motor_ids]): 
            time.sleep(poll_delay)   


    def calibrate_stepper_positions(self, motor_ids, minimum_micropos=-1000000, 
            nanospeed=256, bailout_micropos=1000):

        ## Auto-convert single-valued target_micropos and/or nanospeed to lists, if multiple motor_ids are supplied
        if not hasattr(motor_ids, "__getitem__"): motor_ids = [motor_ids]
        if not hasattr(minimum_micropos, "__getitem__"): minimum_micropos = [minimum_micropos]*len(motor_ids)
        if not hasattr(nanospeed, "__getitem__"): nanospeed = [nanospeed]*len(motor_ids)
        if not hasattr(bailout_micropos, "__getitem__"): bailout_micropos = [bailout_micropos]*len(motor_ids)

        ## Initialize the movement towards endstop(s)
        for n,motor_id in enumerate(motor_ids):
            self.stepper_go(motor_id=motor_id, 
                target_micropos=minimum_micropos[n], nanospeed=nanospeed[n], wait=False, endstop_override=0)

        ## Wait until all motors are done, and optionally wait until they bail out
        some_motor_still_busy = True
        unfinished_motor_ids = list(motor_ids)
        while some_motor_still_busy:
            time.sleep(.1)
            some_motor_still_busy = False
            for n, motor_id in enumerate(motor_ids):
                status = self.get_stepper_status(motor_id=motor_id)
                print(n, status)
                if not status['active']:
                    if status['endswitch']:
                        self.stepper_go(motor_id=motor_id, target_micropos=bailout_micropos[n], 
                                nanospeed=nanospeed[n], wait=False, endstop_override=1, reset_zero_pos=1)
                        unfinished_motor_ids.remove(motor_id)
                else:
                    some_motor_still_busy = True

        if unfinished_motor_ids:
            print(f"Warning: motor(s) {unfinished_motor_ids} finished its calibration move but did not reach any " +
                    "endstop. Are they connected? Is their current sufficient? Didn't they crash meanwhile?")
        return unfinished_motor_ids

if __name__ == "__main__":
    print("Note: Running this module as a standalone script will only try to connect to a RP2 device.")
    print("\tSee the 'examples' directory for further uses.")
    rp2 = Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'

    time.sleep(1)
    rp2.port.write(struct.pack(r'<B', CMD_IDENTIFY))
    #rp2.identify()
    time.sleep(1)
    rp2.port.write(struct.pack(r'<B', CMD_IDENTIFY))
    rp2.port.write(struct.pack(r'<B', CMD_IDENTIFY))
    #rp2.port.write(struct.pack(r'<B', CMD_IDENTIFY))
    time.sleep(.1)
    rp2.shutdown_flag = True
#


            #port = serial.Serial(port="/dev/ttyACM0", timeout=0.1)
            #try: # old-rm
                #self.port = serial.serial(port=serial_port_name, timeout=0.1)
            #except serial.serialutil.serialexception:
                #self._log("not available")
                #continue 
            #todo: if port.pid != 10 or port.vid != 11914

