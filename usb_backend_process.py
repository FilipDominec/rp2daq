#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import multiprocessing as mp

class PatchedProcess(mp.Process):
    def start(self, *args):
        import sys
        bkup_main = sys.modules['__main__']
        sys.modules['__main__'] =  __import__('usb_backend_process')
        super().start(*args)
        sys.modules['__main__'] = bkup_main


import logging
import os
import serial
import threading
import time

def usb_backend(report_queue, command_queue, port_name): 
    """
    Default Python interpreter has a Global Interpreter Lock, due to which a high CPU load 
    in the user script can halt USB data reception, leading to USB buffer overflow and 
    corrupted reports. 

    Relegating the raw data handling to this separate process resolves the problem with GIL. 
    To keep the communication fluent without a tight busy loop in this process, USB input and 
    output are further separated into two threads here. 
    """

    def _raw_byte_output_thread():
        while True:
            out_bytes = command_queue.get(block=True)
            port.write(out_bytes)

    rx_delay = 0.002 if os.name == 'posix' else 0

    try: 
        port = serial.Serial(port=port_name.device, timeout=None)
        raw_byte_output_thread = threading.Thread(target=_raw_byte_output_thread, daemon=True)
        raw_byte_output_thread.start()

        while True:
            in_bytes = port.read(max(1, port.in_waiting))
            report_queue.put(in_bytes)
            if rx_delay:
                # Slight delay experimentally determined to *improve* performance on Linux: 
                # the rx queue does not fill with unduly short byte chunks (todo: even better 
                # would be parsing whole reports here) 
                time.sleep(rx_delay) 

    except OSError: 
        logging.error("Device disconnected, check your cabling or possible short-circuits")
        # (todo) Should somehow invoke self._e.quit() here ? 
        # (todo) Should port.close() ? 

