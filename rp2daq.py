#!/usr/bin/python3  
#-*- coding: utf-8 -*-
"""
rp2daq.py  (c) Filip Dominec 2020-2022, MIT licensed

This module uses c_code_parser.py to auto-generate the binary message interface. 
Then it connects to Raspberry Pi Pico to control various hardware. 

The methods provided here aim to make the hardware control as convenient as possible.  

More information and examples on https://github.com/FilipDominec/rp2daq or in README.md
"""


MIN_FW_VER = 210400

NANOPOS_AT_ENDSTOP = 2**31 # half the range of uint32
NANOSTEP_PER_MICROSTEP = 256 # stepper resolution finer than microstep allows smooth speed control 
MINIMUM_POS = -2**22 ## FIXME should be +2**22 ? 

CMD_IDENTIFY = 0  # blink LED, return "rp2daq" + datecode (6B) + unique 24B identifier

from collections import deque
import logging
import os
import queue
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
        logging.error(message)
        from tkinter import messagebox
        messagebox.showerror(title=exc_value, message=message)
    sys.excepthook = myerr


class Rp2daq(threading.Thread):
    def __init__(self, required_device_id=None, verbose=True):

        logging.basicConfig(level=logging.DEBUG if verbose else logging.INFO, 
                format='%(asctime)s (%(threadName)-9s) %(message)s',) # filename='rp2.log',

        self._register_commands()

        self.port = self._find_device(required_device_id=None, required_firmware_version=MIN_FW_VER)

        # INSPIRED BY TMX
        self.sleep_tune = 0.001
        threading.Thread.__init__(self)
        self.the_reporter_thread = threading.Thread(target=self._reporter)
        #self.the_reporter_thread.daemon = True    # is this necessary?
        self.the_data_receive_thread = threading.Thread(target=self._serial_receiver)

        self.run_event = threading.Event()

        self.the_reporter_thread.start()
        self.the_data_receive_thread.start()

        self.the_deque = deque()

        # allow the threads to run
        self.run_event.set()




    def _register_commands(self):
        # TODO 0: search C code for version, check it matches that one returned by Raspberry Pi at runtime

        C_code = open('rp2daq.c').read()

        names_codes = c_code_parser.generate_command_binary_interface(C_code)
        for cmd_name, cmd_code in names_codes.items():
            exec(cmd_code)
            setattr(self, cmd_name, types.MethodType(locals()[cmd_name], self))

        # Search C code for report structs & generate automatically:
        self.report_cb_queue = {}
        self.report_header_lenghts, self.report_header_formats, self.report_header_varnames = \
                c_code_parser.generate_report_binary_interface(C_code)

        # Register callbacks (to dispatch reports as they arrive)
        self.report_callbacks = {} 

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

        while self.run_event.is_set():
            if len(self.the_deque):
                #print(f"nonzero {len(self.the_deque)=}")
                # report_header_bytes will be populated with the received data for the report
                report_type = self.the_deque.popleft()
                packet_length = self.report_header_lenghts[report_type] - 1

                if packet_length:
                    #print("processing packet_length",packet_length)
                    # get all the data for the report and place it into report_header_bytes

                    #for i in range(packet_length):
                        #while not len(self.the_deque):
                            #time.sleep(self.sleep_tune)
                        #report_header_bytes.append(self.the_deque.popleft()) # fixme: inefficient

                    while len(self.the_deque) < packet_length:
                        time.sleep(self.sleep_tune)
                    report_header_bytes = [self.the_deque.popleft() for _ in range(packet_length)]

                    #logging.debug(f"received packet header {report_type=} {report_header_bytes=} {bytes(report_header_bytes)=}")

                    report_args = struct.unpack(self.report_header_formats[report_type], 
                            bytes([report_type]+report_header_bytes))
                    cb_kwargs = dict(zip(self.report_header_varnames[report_type], report_args))

                    data_bytes = []
                    if (dc := cb_kwargs.get("_data_count",0)) and (dbw := cb_kwargs.get("_data_bitwidth",0)):
                        payload_length = -((-dc*dbw)//8)    # implicit floor() can be inverted to ceil()
                        #logging.debug(f"------------ {dc=} {dbw=} WOULD RECEIVE {payload_length} EXTRA RAW BYTES")

                        while len(self.the_deque) < payload_length:
                            time.sleep(self.sleep_tune)
                        data_bytes = [self.the_deque.popleft() for _ in range(payload_length)]

                        #print()
                        #print([hex(b) for b in data_bytes] )
                        if dbw == 8:
                            cb_kwargs["data"] = data_bytes
                        elif dbw == 12:      # decompress 3B  into pairs of 12b values & flatten
                            odd = [a + ((b&0xF0)<<4)  for a,b
                                    in zip(data_bytes[::3], data_bytes[1::3])]
                            even = [(c&0xF0)//16+(b&0x0F)*16+(c&0x0F)*256  for b,c
                                    in zip(                   data_bytes[1:-1:3], data_bytes[2::3])]
                            cb_kwargs["data"] = [x for l in zip(odd,even) for x in l]
                            if len(odd)>len(even): cb_kwargs["data"].append(odd[-1])

                        elif dbw == 16:      # using little endian byte order everywhere
                            cb_kwargs["data"] = [a+(b<<8) for a,b in zip(data_bytes[:-1:2], data_bytes[1::2])]
                    

                    if cb := self.report_callbacks[report_type]:
                        #logging.debug("CALLING CB {cb_kwargs}")
                        cb(**cb_kwargs)
                    else:
                        self.report_cb_queue[report_type].put(cb_kwargs) # unblock default callback (by data)
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

        while self.run_event.is_set():
            # we can get an OSError: [Errno9] Bad file descriptor when shutting down
            # just ignore it
            try:
                #if self.port.inWaiting():
                    #c = self.port.read()
                    #self.the_deque.append(c) # this may be slow

                if w := self.port.inWaiting():
                    c = self.port.read(w)
                    self.the_deque.extend(c)

                else:
                    time.sleep(self.sleep_tune)
            except OSError:
                pass

    def default_blocking_callback(self, command_code): # 
        """
        Any command called without explicit `_callback` argument is blocking - i.e. the thread
        that called the command waits here until a corresponding report arrives. This is good 
        practice only if quick response from device is expected, or your script uses 
        multithreading. Otherwise your program flow would be stalled for a while here.
        """
        if not self.report_cb_queue.get(command_code):
            self.report_cb_queue[command_code] = queue.Queue()
        kwargs = self.report_cb_queue[command_code].get()
        #print('SYNC CMD RETURNS:', kwargs)
        return kwargs

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
            logging.info(f"checking port {port_name.name}")

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
                logging.info(f"\tport open, but device does not identify itself as rp2daq")
                #del(self.port)
                continue

            version_signature = raw[7:13]
            if not version_signature.isdigit() or int(version_signature) < required_firmware_version:
                logging.critical(f"rp2daq device firmware has version {version_signature.decode('utf-8')},\n" +\
                        f"older than this script's {MIN_FW_VER}.\nPlease upgrade firmware " +\
                        "or override this error using 'required_firmware_version=0'.")
                #del(self.port)
                continue

            if isinstance(required_device_id, str): # optional conversion
                required_device_id = bytes.fromhex(required_device_id.replace(":", ""))
            if required_device_id and raw[12:20] != required_device_id:
                logging.critical(f"found an rp2daq device, but its ID {raw[12:20].hex(':')} does not match " + 
                        f"required {required_device_id.hex(':')}")
                #del(self.port)
                continue

            logging.info(f"connected to rp2daq device with manufacturer ID = {raw[12:20].hex(':')}")
            return port

        else:
            msg = "Error: could not find any matching rp2daq device"
            logging.critical(msg)
            raise RuntimeError(msg)
# }}}
    def identify(self): # {{{
        self.port.write(struct.pack(r'<B', CMD_IDENTIFY))
        raw = self.port.read(30)
        #devicename = raw[0:6]
        #unique_id = (raw[6:14]).hex(':')
        return raw
# }}}


total_b = 0
def test_callback(**kwargs):
    #print('datalen ', len(kwargs['data']))
    global total_b
    global can_end
    total_b += len(kwargs['data'] )
    print("callback delayed from first command by ", time.time()-t0, 'bts', kwargs['blocks_to_send'])# " with kwargs =", kwargs, "\n\n")
    if not kwargs['blocks_to_send']: 
        print(total_b, "in", time.time()-t0, " means ", total_b/(time.time()-t0) , "sample rate" )
        time.sleep(.1)
        rp._stop_threads()

if __name__ == "__main__":
    print("Note: Running this module as a standalone script will only try to connect to a RP2 device.")
    print("\tSee the 'examples' directory for further uses.")
    rp = Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'
    t0 = time.time()

    rp.internal_adc(channel_mask=16, blocksize=8000, blocks_to_send=1000, clkdiv=48000//500, _callback=test_callback) # 480ksps OK
     # always 1st cb missing?



    # Temperature logger
    #def adc_to_temperature(adc, Vref=3.30): 
        #return 27 - (adc*Vref/4095 - 0.706)/0.001721
    #for x in range(10000):
        #T = rp.internal_adc(channel_mask=16, infinite=0, blocksize=2000, blocks_to_send=1, clkdiv=96)['data'] #, _callback=test_callback
        #Tavg = sum(T) / len(T)
        #print('.', )
        #print(time.time()-t0, adc_to_temperature(Tavg),adc_to_temperature(min(T)),adc_to_temperature(max(T)))
        #time.sleep(1)

    #for x in range(min(T), max(T)+1): print(x, adc_to_temperature(x), T.count(x))
    #for x in "Note: Running this module as a standalone script":
        #rp.test(4, ord(x))
        #time.sleep(.2)
        #print('\n'*2)

    #rp.test(2, ord("Q"))
    # TODO test receiving reports split in halves - should trigger callback only when full report is received 

    #time.sleep(.9)
    #rp._stop_threads()


