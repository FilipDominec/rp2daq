#!/usr/bin/python3  
#-*- coding: utf-8 -*-
"""
This example initiates the built-in ADC (analog-digital converter) once, and then receives 
multiple reports containing sampled ADC.

It demonstrates how a virtually unlimited amount of data can be acquired, with
the main thread staying responsive. Such asynchronous, callback-based operation is 
generally more flexible and efficient. 

Rp2daq implements direct-memory access (DMA), multiple buffering and bit compression 
in the firmware, along with multi-threading and -processing in the python module, to 
achieve reliable uninterrupted data stream at 500k × 12bit samples per second.

Finally, in this script the ADC record is separated into channels and plotted using numpy and
matplotlib's interactive plot.

        Filip Dominec 2023-25, public domain
"""

## User options

# List of ADC input channels that will be interleaved in the sampled data. 
ADC_channel_names = {0:"GPIO 26", 1:"GPIO 27", 2:"GPIO 28", 3:"ref V", 4:"builtin thermometer"}

channels = [0,1]     

# ADC speed; 500 ksps is maximum given by hardware. Rp2daq firmware packs the 12bit values into 
# 750 kBps and should be able to stream it continuously to the computer.
kSPS_total = 500

# CPU stress test. While ADC is running, the script on your computer must be able to do other jobs.   
# Use 0 for idle thread waiting, 1 for a loop containing a short time.sleep(), and 2 for a tight busy loop.
# Expected results: With normal 2GHz+ CPU, the options 0 and 1 should store the incoming data on-the-fly. 
# With option 2, the rp2daq receiving routines may not keep up with the stream, leading to the ADC reports 
# heaping in the internal report queue (c.f. rp._i.report_queue.qsize() to monitor this).  
# Option 3 is similar, but between the busy loops rp2daq sends commands (synchronously) to blink the LED.

# But in any case of CPU load and parallel communication, no data should be lost.

CPU_STRESS = 3  

# Set this to True to see all acquired values
PLOTTING_ENABLED = True


import rp2daq 
import threading
import time

## Connect to the device
rp = rp2daq.Rp2daq()

## Generate some realistic signal on GPIO (e.g. connect it through an RC filter to GPIO 26)
rp.pwm_configure_pair(gpio=17, wrap_value=2000-1, clkdiv=25, clkdiv_int_frac=0)
rp.pwm_set_value(gpio=17, value=1000)
#rp.gpio_out(17,1)
time.sleep(.1)

## Run-time objects and variables
all_ADC_done = threading.Event() # a thread-safe semaphore
all_data = []
received_time = []
delayed = []



# Called from other thread whenever data come from RP2
prev_etime_us = 0
def ADC_callback(rv): 
    global t0, prev_etime_us

    all_data.extend(rv.data)
    delayed.extend([rv.block_delayed_by_usb]*(len(rv.data)//len(channels)))

    print("Packet received", len(all_data), len(rv.data), rv.blocks_to_send,
            rv.end_time_us-rv.start_time_us,
            rv.start_time_us-prev_etime_us,
            rp._i.report_queue.qsize(), 
            (" DELAYED" if rv.block_delayed_by_usb else "") )

    prev_etime_us = rv.end_time_us
    received_time.extend([time.time()]*(len(rv.data)//len(channels)))

    if not rv.blocks_to_send: # i.e. if all blocks were received
        all_ADC_done.set()   # releases wait() in the main tread

    if not t0:
        t0 = time.time()

    #print(f"at {time.time()-t0:.3f} s, received {len(kwargs.data)} new " +
            #f"ADC values ({kwargs.blocks_to_send} blocks to go)" + 
            #(" DELAYED" if kwargs.block_delayed_by_usb else "") )


## Initialize the ADC into asynchronous operation...
t0 = None
rp.adc(channel_mask=sum(2**ch for ch in channels), 
        blocksize=500*len(channels), 
        blocks_to_send=1000, 
        #trigger_gpio=17,
        trigger_on_falling_edge=1,
        clkdiv=int(48000//kSPS_total), 
        _callback=ADC_callback)



## Unless all data are received, the program can continue (or wait) here. A dedicated separate process
## ensures that raw data are quickly offloaded from USB into a queue, so that no message is corrupted. 
## High CPU load in this user script can however lead to delays in the callbacks being issued.
## In particular, tight busy loops in main thread (option 4 below) will cause callback delays, so 
## maximum sustained data rate may be roughly halved on ordinary modern computer (<300 kBps).

def busy_wait(t): 
    t0 = time.time()
    while time.time() < t0+t: pass
start_time = time.time()

if CPU_STRESS == 0:
    all_ADC_done.wait() ## Waiting option 0: the right approach to efficient waiting
elif CPU_STRESS == 1:
    while not all_ADC_done.is_set(): # Waiting option 1: moderate CPU load
        time.sleep(.000005)
elif CPU_STRESS == 2:        # Waiting option 2: the Python process is 100% busy (still OK)
    while not all_ADC_done.is_set(): 
        busy_wait(.1)
        if time.time() > start_time+.5:    # optionally, stop the stream after half second
            print(rp.adc_stop()) 
elif CPU_STRESS == 3:   # Waiting option 3: the Python process is 100% busy and issues many sync commands
    while not all_ADC_done.is_set(): 
        rp.gpio_out(25,1)
        busy_wait(.01)
        if time.time() > start_time+.5:    # optionally, stop the stream
            print(rp.adc_stop()) 
        rp.gpio_out(25,0)
        busy_wait(.01)

print(f"Received total {len(all_data)} samples in {time.time()-t0}")
print(f"The actual rate of data arriving in the computer: {len(all_data)/(time.time()-t0):.2f} samples per second")

## Optional plotting of all channels
if PLOTTING_ENABLED:
    t0 = time.time()
    import matplotlib.pyplot as plt
    import numpy as np

    def adc_to_temperature(adc, Vref=3.30): 
        return 27 - (adc*Vref/4095 - 0.716)/0.001721

    print(f"Plotting took {time.time() - t0:.3f} s")
    y = np.array(all_data)
    fig, ax = plt.subplots(nrows=1, ncols=1, figsize=(12, 10))
    for ofs,ch in enumerate(channels):
        ax.plot(y[ofs::len(channels)] * 3.3 / 2**12, 
                label=ADC_channel_names[ch],
                lw=.3, c='rgbycm'[ch]) # marker=',', 
        if ch==4: print(f"Average temperature at channel no.4 = {adc_to_temperature(np.mean(y[ofs::len(channels)])):.3f} °C")

    #ax.plot((-min(received_time)+np.array(received_time)), lw=.3, c='k', label='message received time')  # timing
    ax.plot(np.array(delayed), lw=.8, c='k', label='(message delayed warning)') 

    ax.set_xlabel("ADC count per channel")
    ax.set_ylabel("ADC readout (V)")
    ax.legend()
    plt.show()

