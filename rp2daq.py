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
MINIMUM_POS = -2**22 ## FIXME should be +2**22 ? 

CMD_IDENTIFY = 0  # blink LED, return "rp2daq" + datecode (6B) + unique 24B identifier

from collections import deque
import os
import serial
from serial.tools import list_ports 
import struct
import sys
import threading
import time
import traceback
import tkinter
import types

import c_code_parser

def init_error_msgbox():  # error handling with a graphical message box
    def myerr(exc_type, exc_value, tb): 
        message = '\r'.join(traceback.format_exception(exc_type, exc_value, tb))
        print(message)
        from tkinter import messagebox
        messagebox.showerror(title=exc_value, message=message)
    sys.excepthook = myerr


class Rp2daq(threading.Thread):
    def __init__(self, required_device_id=None, verbose=True):

        self.verbose = verbose
        self.log_text = "" 
        self.start_time = time.time()

        self.sleep_tune = 0.001

        self.port = self._find_device(required_device_id=None, required_firmware_version=MIN_FW_VER)
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


        self._register_commands()


    def _register_commands(self):
        # TODO 0: search C code for version, check it matches that one returned by Raspberry Pi at runtime

        C_code = open('rp2daq.c').read()

        names_codes = c_code_parser.generate_command_binary_interface(C_code)
        for cmd_name, cmd_code in names_codes.items():
            exec(cmd_code)
            setattr(self, cmd_name, types.MethodType(locals()[cmd_name], self))

        # Search C code for report structs & generate automatically:
        self.report_cb_lock = {}
        self.report_header_lenghts, self.report_header_formats, self.report_header_varnames = \
                c_code_parser.generate_report_binary_interface(C_code)

        # Register callbacks (to dispatch reports as they arrive)
        self.report_callbacks = {} 

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
                #print(f"nonzero {len(self.the_deque)=}")
                # response_data will be populated with the received data for the report
                response_data = []
                report_id = self.the_deque.popleft()
                packet_length = self.report_header_lenghts[report_id] - 1

                if packet_length:
                    #print("processing packet_length",packet_length)
                    # get all the data for the report and place it into response_data
                    for i in range(packet_length):
                        while not len(self.the_deque):
                            time.sleep(self.sleep_tune)
                        data = self.the_deque.popleft()
                        response_data.append(data)
                    print(' received packet ', response_data, bytes(response_data) ) # .decode('utf-8'),
                    report_args = struct.unpack(self.report_header_formats[report_id], bytes([report_id]+response_data))
                    cb_kwargs = dict(zip(self.report_header_varnames[report_id], report_args))

                    if (dc := cb_kwargs.get("_data_count",0)) and (dbw := cb_kwargs.get("_data_bitwidth",0)):
                        print(f"------------ {dc=} {dbw=} WOULD RECEIVE {int(dc*dbw)+.999999} EXTRA BYTES " )
                    

                    if cb := self.report_callbacks[report_id]:
                        cb(**cb_kwargs)
                    else:
                        self.report_cb_lock[report_id] = 0
                    

                    # get the report type and look up its dispatch method
                    # here we pop the report type off of response_data
                    report_type = response_data.pop(0)

                    # retrieve the report handler from the dispatch table
                    #dispatch_entry = self.report_dispatch.get(report_type)

                    # if there is additional data for the report,
                    # it will be contained in response_data
                    #dispatch_entry(response_data)
                    #print("RespType", report_type, "RespData", response_data)
                    continue

                else:
                    #if self.shutdown_on_exception: self.shutdown()
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
                    #print('byte ', c)
                    self.the_deque.append(ord(c)) # TODO this is inefficient -> rewrite
                else:
                    time.sleep(self.sleep_tune)
                    # continue
            except OSError:
                pass

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
    def _find_device(self, required_device_id, required_firmware_version):# {{{
        """
        Seeks for a compatible rp2daq device on USB, checking for its firmware version and, if 
        specified, for its particular unique vendor name.
        """

        PID, VID = 10, 11914 # TODO use this info to filter out ports 
        port_list = list_ports.comports()
        #print(f"{port_list=}")
        #for port in port_list: self._log(f"{port=} PID={port.pid} VID={port.vid}")

        for port_name in port_list:
            self._log(f"trying port {port_name.name}", end="")

            port = serial.Serial(port=port_name.device, timeout=0.1)


            try:
                #raw = self.identify()
                #if port.inWaiting(): port.read(port.inWaiting()) # todo: test port flush
                port.write(struct.pack(r'<BB', 1, 0)) # hard-coded "identify" command
                time.sleep(.05) # 20ms round-trip time is enough
                bytesToRead = port.inWaiting() 
                if bytesToRead == 30:    
                    raw = port.read(bytesToRead) # assuming 30B
                    #print(f"{raw=}")
                else:
                    print(f"ERROR identf report, {bytesToRead=}")
            except:
                raw = b''

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


#mylock = 0



def test_callback(**kwargs):
    print("**** test cb **** ", kwargs, time.time()-t0)
    #mylock = 0
    check.acquire(); check.notify(); check.release()

if __name__ == "__main__":
    print("Note: Running this module as a standalone script will only try to connect to a RP2 device.")
    print("\tSee the 'examples' directory for further uses.")
    rp = Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'

    #rp.test(4, ord("J"))
    #rp.test(2, ord("Q"))
    # TODO test receiving reports split in halves - should trigger callback only when full report is received 

    t0=time.time()
    for x in range(3):
        print()

        t0=time.time()
        #rp.pin_out(25, 1); print("synchronous", time.time()-t0)
        #mylock = 1
        rp.pin_out(25, 1, _callback=test_callback); print(f"{x}a asynchronous - just sent cmd", time.time()-t0)

        #while mylock: pass
        #check.acquire(); check.wait()

        print(" .. unlock @ ", time.time()-t0)
        time.sleep(.500)


        print()
        t0=time.time()
        rp.pin_out(25, 0, _callback=test_callback); print(f"{x}b asynchronous - just sent cmd", time.time()-t0)
        time.sleep(.500)


    print(f"end timer {time.time()-t0}")
    time.sleep(.1)
    rp.shutdown_flag = True
