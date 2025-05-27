#!/usr/bin/python3  
#-*- coding: utf-8 -*-
"""
rp2daq.py  (c) Filip Dominec 2020-2024, MIT licensed

This module uses c_code_parser.py to auto-generate the binary message interface. 
Then it connects to Raspberry Pi Pico with rp2daq firmware to control various hardware. 
All rp2daq capabilities are described in the C code of this project.

More information and examples on https://github.com/FilipDominec/rp2daq or in README.md
"""



import atexit
from collections import deque, namedtuple
import logging
import multiprocessing
import os
import pathlib
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



class Rp2daq():
    def __init__(self, required_device_id="", verbose=False):

        logging.basicConfig(level=logging.DEBUG if verbose else logging.INFO, 
                format='%(asctime)s (%(threadName)-9s) %(message)s',) # filename='rp2.log',

        # Most of the technicalities are delegated to the following class. Rp2daq's namespace, 
        # exposed to the user, will be kept clean and dynamically populated with useful commands.
        self._i = Rp2daq_internals(externals=self, required_device_id=required_device_id, verbose=verbose)

        atexit.register(self.quit) # (fixme?) does not work well with Spyder console


    def quit(self):
        """Clean termination of tx/rx threads, and explicit releasing of serial ports (for win32) """ 
        time.sleep(0.01) # is this necessary?
        if self._i.run_event.is_set():
            self._i.run_event.clear()
            self._i.terminate_queue.put(b'1')   # let the subprocess release the port on its own
            self._i.terminate_queue.get(block=True) # wait for confirmation it succeeded



class Rp2daq_internals(threading.Thread):
    def __init__(self, externals, required_device_id="", verbose=False):
        threading.Thread.__init__(self) 

        self._e = externals

        self._register_commands()

        # auto-checking binary compatibility of device's firmware against available C code
        rp2daq_h_ver = c_code_parser.get_C_code_version()
        self.port_name = self._find_device(required_device_id, required_firmware_version=rp2daq_h_ver)

        ## Asynchronous communication using threads
        self.sleep_tune = 0.001

        self.report_queue = multiprocessing.Queue()  
        self.command_queue = multiprocessing.Queue()  
        self.terminate_queue = multiprocessing.Queue()  

        # Establish reliable USB connection using a child process, patching the multiprocessing.Process
        # class so that user scripts are no more required to contain the __name__=='__main__' guard clause.
        import usb_backend_process as ubp
        self.usb_backend_process = ubp.PatchedProcess(
                target=ubp.usb_backend, 
                args=(self.report_queue, self.command_queue, self.terminate_queue, self.port_name))
        self.usb_backend_process.daemon = True
        self.usb_backend_process.start()

        # Additionally, run two separate threads in the main process te deal with incoming reports.  
        self.report_processing_thread = threading.Thread(target=self._report_processor, daemon=True)
        self.callback_dispatching_thread = threading.Thread(target=self._callback_dispatcher, daemon=True)
        self.rx_bytes = deque()
        #self.rx_bytes_total_len = 0

        # Launch the bidirectional communication now
        self.run_event = threading.Event()
        self.report_processing_thread.start()
        self.callback_dispatching_thread.start()
        self.run_event.set()


    def _register_commands(self):
        # TODO 0: search C code for version, check it matches that one returned by Raspberry Pi at runtime
        # #define FIRMWARE_VERSION {"rp2daq_220720_"}
        # self.expected_firmware_v = 

        self.report_names, self.report_header_lenghts, self.report_header_formats, self.report_header_varnames, \
                names_codes, markdown_docs = c_code_parser.analyze_c_firmware()

        for cmd_name, cmd_code in names_codes.items():
            exec(cmd_code)
            setattr(self._e, cmd_name, types.MethodType(locals()[cmd_name], self))

        # Search C code for report structs & generate automatically:
        self.sync_report_cb_queues = {}
        self.async_report_cb_queue = queue.Queue()

        # Stores callbacks (to dispatch reports as they arrive); generate corresponding named tuples for each
        self.report_callbacks = {} 
        self.report_namedtuple_classes = {} 
        for report_type, varnames in self.report_header_varnames.items():
            self.report_namedtuple_classes[report_type] = namedtuple(
                    self.report_names[report_type] + '_report_values', 
                    varnames + (['data'] if 'data_bitwidth' in varnames else []))


    def _report_processor(self):
        """
        A thread to continuously check for incoming data. When a byte comes in, place it onto the deque.
        When the bytes in the deque are enough for the expected length of a report, process them into one.
        """

        def queue_recv_bytes(length): # note: should re-implement with io.BytesIO() ring buffer?
            while len(self.rx_bytes) < length:
                #c = self.report_pipe_out.recv_bytes()
                c = self.report_queue.get()

                self.rx_bytes.extend(c) # superfluous bytes are kept in deque for later use
                #self.rx_bytes_total_len += len(c)
            return bytes([self.rx_bytes.popleft() for _ in range(length)])

        def unpack_data_payload(data_bytes, count, bitwidth):
            if bitwidth == 8:
                return list(data_bytes)  # for any bitwidth return a list of ints, not the bytes object
            elif bitwidth == 12:      # quick compress byte triplet into 12b integer pairs
                odd = [a + ((b&0xF0)<<4)  for a,b
                        in zip(data_bytes[::3], data_bytes[1::3])]
                even = [(c&0xF0)//16+(b&0x0F)*16+(c&0x0F)*256  for b,c
                        in zip(data_bytes[1:-1:3], data_bytes[2::3])]
                return [x for l in zip(odd,even) for x in l] + ([odd[-1]] if len(odd)>len(even) else [])
            elif bitwidth == 16:      # compress byte pairs into 16b integers (note: LE byte order)
                return [a+(b<<8) for a,b in zip(data_bytes[:-1:2], data_bytes[1::2])]
            else:
                print(bitwidth, count, len(data_bytes))
                raise NotImplementedError 

        self.run_event.wait()

        while self.run_event.is_set():
            try:
                #if self.port.in_waiting:
                    # 1st: Get 1st byte to tell the report type
                    report_type_b = queue_recv_bytes(1); 
                    report_type = ord(report_type_b)
                    packet_length = self.report_header_lenghts[report_type] - 1

                    # 2nd: Get the corresponding header
                    report_header_bytes = queue_recv_bytes(packet_length)
                    report_args = struct.unpack(self.report_header_formats[report_type], 
                            report_type_b+report_header_bytes)
                    logging.debug(f"received packet header {report_type} {bytes(report_header_bytes)}")

                    # 3rd: Get the data payload (if present), and convert it into a list of ints
                    if "data_count" in self.report_header_varnames[report_type]:
                        cb_kwargs = dict(zip(self.report_header_varnames[report_type], report_args))
                        count, bitwidth = cb_kwargs["data_count"], cb_kwargs["data_bitwidth"]
                        payload_length = -((-count*bitwidth)//8)  # int div like floor(); this makes it ceil()
                        payload_raw = queue_recv_bytes(payload_length)

                        # Use pre-cached classes for each report type
                        return_values = self.report_namedtuple_classes[report_type](
                                *report_args, unpack_data_payload(payload_raw, count, bitwidth))
                    else:
                        return_values = self.report_namedtuple_classes[report_type](
                                *report_args)

                    # 4th: Register callback (if async), or wait (if sync)
                    cb = self.report_callbacks.get(report_type, False) # false for unexpected reports
                    if cb:
                        self.async_report_cb_queue.put((cb, return_values))
                    elif cb is None: # expected report from blocking command
                        self.sync_report_cb_queues[report_type].put(return_values) # unblock default callback (& send it data)
                    elif cb is False: # unexpected report, from command that was not yet called in this script instance
                        logging.warning(f"Warning: Unexpected report type; you may want to reset the device. \n\tDebug info: {return_values}", 
                                report_namedtuple_classes[return_values])
                        pass 
                ## TODO: enqueue to be called by yet another thread (so that sync cmds work within callbacks,too)
                ## TODO: check if sync cmd works correctly after async cmd (of the same type)

            except EOFError:
                logging.warning("Got EOF from the receiver process, quitting")
                self._e.quit()

    def _callback_dispatcher(self):
        """
        A separate thread of the main process to call all callbacks.
        """
        self.run_event.wait()

        while self.run_event.is_set():
            (cb, return_values) = self.async_report_cb_queue.get()
            cb(return_values)

    def default_blocking_callback(self, command_code): # 
        """
        Any command called without explicit `_callback` argument is blocking - i.e. the thread
        that called the command waits here until a corresponding report arrives. This is good 
        practice only if quick response from device is expected, or your script uses 
        multithreading. Otherwise your program flow would be stalled for a while here.

        This function is called from *autogenerated* code for each command, iff no _callback
        is specified.
        """
        kwargs = self.sync_report_cb_queues[command_code].get() # waits until default callback unblocked
        return kwargs

    def _find_device(self, required_device_id, required_firmware_version=0):
        """
        Seeks for a compatible rp2daq device on USB, checking for its firmware version and, if 
        specified, for its particular unique vendor name.
        """

        port_list = list_ports.comports()

        for port_name in port_list:
            # filter out ports, without disturbing previously connected devices 
            #VID=0x2e8a;  PID = 0x000a for RP2040, but 0x0009 for RP2350 
            #print(port_name.hwid)
            if not (port_name.hwid.startswith("USB VID:PID=2E8A:000A SER="+required_device_id.upper()) or
                port_name.hwid.startswith("USB VID:PID=2E8A:0009 SER="+required_device_id.upper()) ): 
                continue
            #print(f"port_name.hwid={port_name.hwid}")
            try_port = serial.Serial(port=port_name.device, timeout=1)

            try:
                #try_port.reset_input_buffer(); try_port.flush()
                #time.sleep(.05) # 50ms round-trip time is enough

                # the "identify" command is hard-coded here, as the receiving threads are not ready yet
                try_port.write(struct.pack(r'<BBB', 1, 0, 1)) 
                time.sleep(.15) # 50ms round-trip time is enough
                assert try_port.in_waiting == 1+2+1+30
                id_data = try_port.read(try_port.in_waiting)[4:] 
            except:
                id_data = b''
            ## TODO close the port, remember its port_name (thus do not keep open many ports & enable pickling for multiproc on win)

            if id_data[:6] != b'rp2daq': 
                logging.info(f"A Raspberry Pi Pico device is present but its firmware doesn't identify as rp2daq: {id_data}" )
                continue

            version_signature = id_data[7:13]
            if not version_signature.isdigit() or int(version_signature) != required_firmware_version:
                logging.warning(f"rp2daq device firmware has version {version_signature.decode('utf-8')},\n" +\
                        f"older than this script requires: {required_firmware_version}.\nPlease upgrade firmware " +\
                        "or override this error using 'required_firmware_version=0'.")
                continue

            if isinstance(required_device_id, str): # optional conversion
                required_device_id = required_device_id.replace(":", "")
            found_device_id = id_data[14:]
            if required_device_id and found_device_id != required_device_id:
                logging.info(f"Found an rp2daq device, but its ID {found_device_id} does not match " + 
                        f"required {required_device_id}")
                continue

            logging.info(f"Connected to rp2daq device with unique ID = {found_device_id.decode()} and correct FW version = {required_firmware_version}")
            #return try_port
            try_port.close()
            return port_name

        else:
            msg = "Error: could not find any matching rp2daq device"
            logging.critical(msg)
            raise RuntimeError(msg)



if __name__ == "__main__":
    print("Note: Running this module as a standalone script will only try to connect to a RP2 device.")
    print("\tSee the 'examples' directory for further uses.")
    rp = Rp2daq()       # tip you can use e.g. required_device_id='01020304050607'
    t0 = time.sleep(3)
    rp.quit()

