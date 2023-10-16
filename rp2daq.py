#!/usr/bin/python3  
#-*- coding: utf-8 -*-
"""
rp2daq.py  (c) Filip Dominec 2020-2022, MIT licensed

This module uses c_code_parser.py to auto-generate the binary message interface. 
Then it connects to Raspberry Pi Pico to control various hardware. 

The methods provided here aim to make the hardware control as convenient as possible.  

More information and examples on https://github.com/FilipDominec/rp2daq or in README.md
"""


MIN_FW_VER = 231005

import atexit
from collections import deque
import logging
import multiprocessing
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



class Rp2daq():
    def __init__(self, required_device_id="", verbose=False):

        logging.basicConfig(level=logging.DEBUG if verbose else logging.INFO, 
                format='%(asctime)s (%(threadName)-9s) %(message)s',) # filename='rp2.log',

        # Most of the technicalities are delegated to the following class. The Rp2daq's namespace 
        # will be dynamically populated with useful commands
        self._i = Rp2daq_internals(externals=self, required_device_id=required_device_id, verbose=verbose)

        atexit.register(self.quit)


    def quit(self):
        """Clean termination of tx/rx threads, and explicit releasing of serial ports (for win32) """ 
        # TODO: add device reset option
        self._i.run_event.clear()
        #self._i.port.close()



#def usb_backend(report_pipe, port_name): 


class Rp2daq_internals(threading.Thread):
    def __init__(self, externals, required_device_id="", verbose=False):
        threading.Thread.__init__(self) 

        self._e = externals

        self._register_commands()

        time.sleep(.05)
        self.port_name = self._find_device(required_device_id, required_firmware_version=MIN_FW_VER)

        ## Asynchronous communication using threads
        self.sleep_tune = 0.001

        self.report_queue = multiprocessing.Queue()  
        self.command_queue = multiprocessing.Queue()  

        # Experimental: the "pipe" variant
        #self.report_pipe_out, report_pipe_in = multiprocessing.Pipe(duplex=True)
                #args=(report_pipe_in, self.port_name))


        # Experimental: the "guardless" variant
        import usb_backend_process as ubp
        self.usb_backend_process = ubp.PatchedProcess(
                target=ubp.usb_backend, 
                args=(self.report_queue, self.command_queue, self.port_name))
        self.usb_backend_process.daemon = True
        self.usb_backend_process.start()

        # The default variant
        #self.usb_backend_process = multiprocessing.Process(
                #target=ubp.usb_backend, 
                #args=(self.report_queue, self.command_queue, self.port_name))
        #self.usb_backend_process.daemon = True  # enables terminating along with main process
        #self.usb_backend_process.start()

        self.report_processing_thread = threading.Thread(target=self._report_processor, daemon=True)
        self.callback_dispatching_thread = threading.Thread(target=self._callback_dispatcher, daemon=True)

        self.rx_bytes = deque()
        self.rx_bytes_total_len = 0

        self.run_event = threading.Event()
        self.report_processing_thread.start()
        self.callback_dispatching_thread.start()
        self.run_event.set()


    def _register_commands(self):
        # TODO 0: search C code for version, check it matches that one returned by Raspberry Pi at runtime
        # #define FIRMWARE_VERSION {"rp2daq_220720_"}
        # self.expected_firmware_v = 

        names_codes, markdown_docs  = c_code_parser.generate_command_binary_interface()
        for cmd_name, cmd_code in names_codes.items():
            exec(cmd_code)
            setattr(self._e, cmd_name, types.MethodType(locals()[cmd_name], self))

        # Search C code for report structs & generate automatically:
        self.sync_report_cb_queues = {}
        self.async_report_cb_queue = queue.Queue()
        self.report_header_lenghts, self.report_header_formats, self.report_header_varnames = \
                c_code_parser.generate_report_binary_interface()

        # Register callbacks (to dispatch reports as they arrive)
        self.report_callbacks = {} 


    def _report_processor(self):
        """
        Thread to continuously check for incoming data.
        When a byte comes in, place it onto the deque.
        """

        def queue_recv_bytes(length): # note: should re-implement with io.BytesIO() ring buffer?
            while len(self.rx_bytes) < length:
                #c = self.report_pipe_out.recv_bytes()
                c = self.report_queue.get()

                self.rx_bytes.extend(c) # superfluous bytes are kept in deque for later use
                self.rx_bytes_total_len += len(c)
            return bytes([self.rx_bytes.popleft() for _ in range(length)])

        def unpack_data_payload(data_bytes, count, bitwidth):
            if bitwidth == 8:
                return data_bytes
            elif bitwidth == 12:      # decompress 3B  into pairs of 12b values & flatten
                odd = [a + ((b&0xF0)<<4)  for a,b
                        in zip(data_bytes[::3], data_bytes[1::3])]
                even = [(c&0xF0)//16+(b&0x0F)*16+(c&0x0F)*256  for b,c
                        in zip(                   data_bytes[1:-1:3], data_bytes[2::3])]
                return [x for l in zip(odd,even) for x in l] + ([odd[-1]] if len(odd)>len(even) else [])

                # maybe more efficient variant, fixme: shouldn't miss last odd byte, if any
                #return [x 
                        #for a,b,c in zip(data_bytes[::3], data_bytes[1::3], data_bytes[2::3]) 
                        #for x in (a + ((b&0xF0)<<4), (c&0xF0)//16+(b&0x0F)*16+(c&0x0F)*256)] 
            elif bitwidth == 16:      # using little endian byte order everywhere
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
                    cb_kwargs = dict(zip(self.report_header_varnames[report_type], report_args))
                    logging.debug(f"received packet header {report_type=} {bytes(report_header_bytes)=}")

                    # 3rd: Get the data payload (if present), and convert it into a list of ints
                    if cb_kwargs.get("_data_count") and cb_kwargs["_data_count"]:
                        #print("Get the data payload",cb_kwargs.get("_data_count") )
                        count, bitwidth = cb_kwargs["_data_count"], cb_kwargs["_data_bitwidth"]
                        payload_length = -((-count*bitwidth)//8)  # int div like floor(); this makes it ceil()
                        payload_raw = queue_recv_bytes(payload_length)
                        cb_kwargs["data"] = unpack_data_payload(payload_raw, count, bitwidth)

                    # 4th: Register callback (if async), or wait (if sync)
                    cb = self.report_callbacks.get(report_type, False) # false for unexpected reports
                    if cb:
                        self.async_report_cb_queue.put((cb, cb_kwargs))
                    elif cb is None: # expected report from blocking command
                        self.sync_report_cb_queues[report_type].put(cb_kwargs) # unblock default callback (& send it data)
                    elif cb is False: # unexpected report, from command that was not yet called in this script instance
                        logging.warning(f"Warning: Got callback that was not asked for\n\tDebug info: {cb_kwargs}")
                        pass 
                #else:
                    #time.sleep(self.sleep_tune)
                #logging.debug("CALLING CB {cb_kwargs}")
                #print(f"CALLING CB {cb} {cb_kwargs}")
                ## TODO: enqueue to be called by yet another thread (so that sync cmds work within callbacks,too)
                ## TODO: check if sync cmd works correctly after async cmd (of the same type)
                #cb(**cb_kwargs)

            except EOFError:
                logging.debug("Got EOF from the receiver process, quitting")
                self._e.quit()

    def _callback_dispatcher(self):
        """
        """
        self.run_event.wait()

        while self.run_event.is_set():
            (cb, cb_kwargs) = self.async_report_cb_queue.get()
            cb(**cb_kwargs)

    def default_blocking_callback(self, command_code): # 
        """
        Any command called without explicit `_callback` argument is blocking - i.e. the thread
        that called the command waits here until a corresponding report arrives. This is good 
        practice only if quick response from device is expected, or your script uses 
        multithreading. Otherwise your program flow would be stalled for a while here.

        This function is called from *autogenerated* code for each command, iff no _callback
        is specified.
        """
        #print("DBC WAITING", command_code)
        kwargs = self.sync_report_cb_queues[command_code].get() # waits until default callback unblocked
        return kwargs

    def _find_device(self, required_device_id, required_firmware_version=0):
        """
        Seeks for a compatible rp2daq device on USB, checking for its firmware version and, if 
        specified, for its particular unique vendor name.
        """

        VID, PID = 0x2e8a, 0x000a 
        port_list = list_ports.comports()

        for port_name in port_list:
            # filter out ports, without disturbing previously connected devices 
            if not port_name.hwid.startswith("USB VID:PID=2E8A:000A SER="+required_device_id.upper()):
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

            if not id_data[:6] == b'rp2daq': 
                logging.info(f"\tport open, but device does not identify itself as rp2daq: {id_data}" )
                continue

            version_signature = id_data[7:13]
            if not version_signature.isdigit() or int(version_signature) < required_firmware_version:
                logging.warning(f"rp2daq device firmware has version {version_signature.decode('utf-8')},\n" +\
                        f"older than this script's {MIN_FW_VER}.\nPlease upgrade firmware " +\
                        "or override this error using 'required_firmware_version=0'.")
                continue

            if isinstance(required_device_id, str): # optional conversion
                required_device_id = required_device_id.replace(":", "")
            found_device_id = id_data[14:]
            if required_device_id and found_device_id != required_device_id:
                logging.info(f"found an rp2daq device, but its ID {found_device_id} does not match " + 
                        f"required {required_device_id}")
                continue

            logging.info(f"connected to rp2daq device with manufacturer ID = {found_device_id.decode()}")
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
    t0 = time.time()

    #rp.pin_set(11, 1, high_z=1, pull_up=1)
    #print(rp.pin_get(11))
    #rp.pwm_configure_pair(0, clkdiv=255, wrap_value=500)
    #rp.pwm_set_value(0, 200)
    #rp.pwm_configure_pair(0, clkdiv=255, wrap_value=500)
    #rp.pwm_set_value(1, 100)
    #rp.pwm_configure_pair(2, clkdiv=255, wrap_value=1000)
    #rp.pwm_set_value(2, 300)




            #try_port = serial.Serial(port=port_name.device, timeout=1)
            # TODO: get along without the serial module
            #try_port = io.open(port_name.device) # fixme: no response from device?
            #try_port = io.open(list_ports.comports()[0].device) # crude approach
