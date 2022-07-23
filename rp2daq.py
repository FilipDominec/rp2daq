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
    def __init__(self, required_device_id=None, verbose=False):

        logging.basicConfig(level=logging.DEBUG if verbose else logging.INFO, 
                format='%(asctime)s (%(threadName)-9s) %(message)s',) # filename='rp2.log',

        self._register_commands()

        self.port = self._find_device(required_device_id=None, required_firmware_version=MIN_FW_VER)

        # Asynchronous communication using threads
        self.sleep_tune = 0.001
        threading.Thread.__init__(self)
        self.data_receive_thread = threading.Thread(target=self._serial_receiver, daemon=True)
        self.reporter_thread = threading.Thread(target=self._reporter, daemon=True)

        self.rx_bytes = deque()
        self.run_event = threading.Event()
        self.data_receive_thread.start()
        self.reporter_thread.start()
        self.run_event.set()




    def _register_commands(self):
        # TODO 0: search C code for version, check it matches that one returned by Raspberry Pi at runtime
        # #define FIRMWARE_VERSION {"rp2daq_220720_"}
        # self.expected_firmware_v = 

        names_codes = c_code_parser.generate_command_binary_interface()
        for cmd_name, cmd_code in names_codes.items():
            exec(cmd_code)
            setattr(self, cmd_name, types.MethodType(locals()[cmd_name], self))

        # Search C code for report structs & generate automatically:
        self.report_cb_queue = {}
        self.report_header_lenghts, self.report_header_formats, self.report_header_varnames = \
                c_code_parser.generate_report_binary_interface()

        # Register callbacks (to dispatch reports as they arrive)
        self.report_callbacks = {} 

    def quit(self):
        self.run_event.clear()

    def _serial_receiver(self):
        """
        Thread to continuously check for incoming data.
        When a byte comes in, place it onto the deque.
        """
        self.run_event.wait()

        while self.run_event.is_set():
            try:
                if w := self.port.inWaiting():
                    c = self.port.read(w)
                    self.rx_bytes.extend(c)
                else:
                    time.sleep(self.sleep_tune)
            except OSError:
                logging.error("Device disconnected")
                self.quit()

    def _reporter(self):
        """
        This is the reporter thread. It continuously pulls data from
        the deque. When a full message is detected, that message is
        processed.
        """
        self.run_event.wait()

        while self.run_event.is_set():
            if len(self.rx_bytes):
                # report_header_bytes will be populated with the received data for the report
                report_type = self.rx_bytes.popleft()
                packet_length = self.report_header_lenghts[report_type] - 1

                while len(self.rx_bytes) < packet_length:
                    time.sleep(self.sleep_tune)
                report_header_bytes = [self.rx_bytes.popleft() for _ in range(packet_length)]

                #logging.debug(f"received packet header {report_type=} {report_header_bytes=} {bytes(report_header_bytes)=}")

                report_args = struct.unpack(self.report_header_formats[report_type], 
                        bytes([report_type]+report_header_bytes))
                cb_kwargs = dict(zip(self.report_header_varnames[report_type], report_args))

                data_bytes = []
                if (dc := cb_kwargs.get("_data_count",0)) and (dbw := cb_kwargs.get("_data_bitwidth",0)):
                    payload_length = -((-dc*dbw)//8)    # implicit floor() can be inverted to ceil()
                    #logging.debug(f"------------ {dc=} {dbw=} WOULD RECEIVE {payload_length} EXTRA RAW BYTES")

                    while len(self.rx_bytes) < payload_length:
                        time.sleep(self.sleep_tune)
                    data_bytes = [self.rx_bytes.popleft() for _ in range(payload_length)]

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
                time.sleep(self.sleep_tune)

    def default_blocking_callback(self, command_code): # 
        """
        Any command called without explicit `_callback` argument is blocking - i.e. the thread
        that called the command waits here until a corresponding report arrives. This is good 
        practice only if quick response from device is expected, or your script uses 
        multithreading. Otherwise your program flow would be stalled for a while here.
        """
        if not self.report_cb_queue.get(command_code):
            self.report_cb_queue[command_code] = queue.Queue()
        kwargs = self.report_cb_queue[command_code].get() # waits until report arrives
        #print('SYNC CMD RETURNS:', kwargs)
        return kwargs

    def _find_device(self, required_device_id, required_firmware_version):
        """
        Seeks for a compatible rp2daq device on USB, checking for its firmware version and, if 
        specified, for its particular unique vendor name.
        """

        PID, VID = 10, 11914 # TODO use this info to filter out ports 
        port_list = list_ports.comports()

        for port_name in port_list:

            try_port = serial.Serial(port=port_name.device, timeout=0.1)
            logging.info(f"checking port {port_name.name}")

            try:
                try_port.flush()

                # the probing "identify" command is hard-coded here, as the receiver thread are not 
                # ready yet
                try_port.write(struct.pack(r'<BB', 1, 0)) 
                time.sleep(.05) # 20ms round-trip time is enough
                bytesToRead = try_port.inWaiting() 
                assert bytesToRead == 1+2+1+30
                id_data = try_port.read(bytesToRead)[4:] 
            except:
                id_data = b''

            if not id_data[:6] == b'rp2daq': 
                logging.info(f"\tport open, but device does not identify itself as rp2daq")
                continue

            version_signature = id_data[7:13]
            if not version_signature.isdigit() or int(version_signature) < required_firmware_version:
                logging.critical(f"rp2daq device firmware has version {version_signature.decode('utf-8')},\n" +\
                        f"older than this script's {MIN_FW_VER}.\nPlease upgrade firmware " +\
                        "or override this error using 'required_firmware_version=0'.")
                continue

            if isinstance(required_device_id, str): # optional conversion
                required_device_id = required_device_id.replace(":", "")
            found_device_id = id_data[14:]
            if required_device_id and found_device_id != required_device_id:
                logging.critical(f"found an rp2daq device, but its ID {found_device_id} does not match " + 
                        f"required {required_device_id}")
                continue

            logging.info(f"connected to rp2daq device with manufacturer ID = {found_device_id.decode()}")
            return try_port
        else:
            msg = "Error: could not find any matching rp2daq device"
            logging.critical(msg)
            raise RuntimeError(msg)



if __name__ == "__main__":
    print("Note: Running this module as a standalone script will only try to connect to a RP2 device.")
    print("\tSee the 'examples' directory for further uses.")
    rp = Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'
    t0 = time.time()

    rp.pin_set(11, 1, high_z=1, pull_up=1)
    print(rp.pin_get(11))

    #rp.pin_set(25,1)

    rp.quit()

