#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import multiprocessing as mp

# This is a unique (?) trick to prevent the multiprocessing module from launching 
# a "spawn bomb", without one having to annoyingly put the "if __name__ == '__main__'" 
# guard into every script that imports this code.
# We just need to monkey-patch the start() method of mp.Process class so that it runs this
# file in the new process, instead of re-running the whole original program. The deadly 
# recursion is thus avoided and no functionality is apparently harmed.
# Maybe this feature should become available in the official multiprocessing module, too.
class PatchedProcess(mp.Process):
    def start(self, *args):
        import sys
        bkup_main = sys.modules['__main__']
        sys.modules['__main__'] =  __import__('usb_backend_process')
        super().start(*args)
        sys.modules['__main__'] = bkup_main


import logging
import os
import queue
import serial
import threading
import time

def usb_backend(report_queue, command_queue, terminate_queue, port_name): 
    """
    Default Python interpreter has a Global Interpreter Lock, due to which a high CPU load 
    in the user script can halt USB data reception, leading to USB buffer overflow and 
    corrupted reports. This was confirmed both on Linux and Windows, although they behave a bit 
    different. 

    Relegating the raw data handling to this separate process resolves the problem with GIL. 
    To keep the communication fluent without a tight busy loop in this process, USB input and 
    output are further separated into two threads here. 
    """

    def _raw_byte_output_thread():
        while port.is_open:
            out_bytes = command_queue.get(block=True)
            port.write(out_bytes)


    def _terminate_thread():
        terminate_queue.get(block=True)
        terminate_pending.set()
        port.close()   # other threads below are made to handle this situation and gracefully


    # Observation from stress-tests: on Linux, rp2daq.py handles more data with few-ms delay within 
    # receiver loop, while Windows10 seems better without it 
    # Warning: current implementation may silently lose ADC packets when they come too often >400/s 
    # (https://github.com/FilipDominec/rp2daq/issues/23)
    rx_delay = 0.002 if os.name == 'posix' else 0 

    terminate_pending = threading.Event()
    try: 
        port = serial.Serial(port=port_name.device, timeout=None)

        raw_byte_output_thread = threading.Thread(target=_raw_byte_output_thread, daemon=True)
        control_thread = threading.Thread(target=_terminate_thread, daemon=True)
        raw_byte_output_thread.start()
        control_thread.start()

        while True:
            in_bytes = port.read(max(1, port.in_waiting))
            report_queue.put(in_bytes)
            if rx_delay:
                # Slight delay experimentally determined to *improve* performance on Linux: 
                # the rx queue does not fill with unduly short byte chunks (todo: even better 
                # would be parsing whole reports here) 
                time.sleep(rx_delay) 
    except (OSError, TypeError, AttributeError) as e:  # diferent OSes seem to report different errors?
        # (todo) Should try reconnecting? 
        # (todo) Should somehow send termination message to the main process? 
        if terminate_pending.is_set():
            logging.info("Device successfully disconnected")
        else: 
            logging.error("Device unexpectedly disconnected! Check your cabling and restart the program.")
        del(port)
        terminate_queue.put(b'2')   # report back to main process we are done here

