#!/usr/bin/python3  
#-*- coding: utf-8 -*-
"""
This slightly fancier example receives multiple data blocks from the built in ADC 
(analog-to-digital converter) Raspberry Pi Pico.

It demonstrates how a virtually unlimited amount of data can be acquired, with
the main thread staying responsive. Such asynchronous, callback-based operation is 
generally more flexible and efficient. 

Finally, the ADC record is separated into channels and plotted using numpy and
matplotlib's interactive plot.

        Filip Dominec 2022, public domain
"""




import rp2daq 
import threading
import time

## Connect to the device
rp = rp2daq.Rp2daq()

## User options
channel_names = {0:"pin 26", 1:"pin 27", 2:"pin 28", 3:"ref V", 4:"int thermo"}

# 300 kSPS seems safe over USB; 500 kSPS are almost sure not to pass through USB full-speed

channels = [1,4]     # 0,1,2 are pins 26-28;  3 is V_ref and 4 is internal thermometer
kSPS_per_ch = 150 * len(channels)    # note there is only one multiplexed ADC




# TODO: FIXME with total sample rate approx > 100k kSPS, order of bytes appear as temporarily swapped
#           persists with 1 channel
#           not sure if existed before
#           must check if wforms look nice
rp.pin_set(pin=2, value=0)
rp.pwm_configure_pair(pin=2, wrap_value=6553, clkdiv=250, clkdiv_int_frac=0)
rp.pwm_set_value(pin=2, value=3500) # minimum position

time.sleep(1)

## Run-time objects and variables
all_ADC_done = threading.Event() # a thread-safe semaphore
all_data = []
read_counter = []
delayed = []

## Initialize the ADC into asynchronous operation...
def append_ADC_data(**kwargs): #   called from other thread whenever data come from RP2
    global t0
    all_data.extend(kwargs['data'])
    read_counter.extend([time.time()]*(len(kwargs['data'])//2))
    delayed.extend([kwargs['block_delayed_by_usb']]*(len(kwargs['data'])//2))

    kwargs['data'] = None
    print(kwargs)

    #print(f"at {time.time()-t0:.3f} s, received {len(kwargs['data'])} new " +
            #f"ADC values ({kwargs['blocks_to_send']} blocks to go)")

    if not kwargs['blocks_to_send']: # i.e. if all blocks were received
        all_ADC_done.set()   # releases wait() in the main tread

    if not t0:
        print("Raw data example:", kwargs)
        t0 = time.time()


t0 = None
rp.internal_adc(channel_mask=sum(2**ch for ch in channels), 
        blocksize=2000*len(channels), 
        blocks_to_send=20, 
        clkdiv=48000//kSPS_per_ch, 
        _callback=append_ADC_data)

## Unless all data are received, the program can continue (or wait) here
all_ADC_done.wait()  #rp.quit()
#while not all_ADC_done.is_set():
    #pass
    #rp.pin_set(25,1)
    #time.sleep(.05)
    #rp.pin_set(25,0)
    #time.sleep(.05)

#rp.quit() # relase the device (or the app will wait indefinitely)

print(f"Received total {len(all_data)} samples in {time.time()-t0}")
print(f"Average incoming data rate was {len(all_data)/(time.time()-t0):.2f} samples per second")



## Optional plotting of all channels
t0 = time.time()
import matplotlib.pyplot as plt
import numpy as np

print(f"Plotting took {time.time() - t0:.3f} s")
#(0.8255-0.6188) / (2.52659-2.51372)
#
y = np.array(all_data)
fig, ax = plt.subplots(nrows=1, ncols=1, figsize=(12, 10))
for ofs,ch in enumerate(channels):
    ax.plot(y[ofs::len(channels)] * 3.3 / 2**12, 
            label=channel_names[ch],
            lw=.3, c='rgbycm'[ch]) # marker=',', 
    def adc_to_temperature(adc, Vref=3.30): 
            return 27 - (adc*Vref/4095 - 0.716)/0.001721
    if ch==4: print(f"Average temperature at channel no.4 = {adc_to_temperature(np.mean(y[ofs::len(channels)])):.3f} °C")

ax.plot((-min(read_counter)+np.array(read_counter)), lw=.3, c='k') 
ax.plot(np.array(delayed), lw=1.8, c='g') 

ax.set_xlabel("ADC count per channel")
ax.set_ylabel("(inaccurate) voltage measured")
ax.legend()
plt.show()

