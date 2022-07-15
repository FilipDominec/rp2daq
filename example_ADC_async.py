#!/usr/bin/python3  
#-*- coding: utf-8 -*-
"""
This slightly fancier example receives multiple data blocks from the built in ADC 
(analog-to-digital converter) Raspberry Pi Pico.

It demonstrates how virtually unlimited amount of data can be acquired, with
the main thread staying responsive. Asynchronous, callback-based operation is 
generally more flexible and recommended. 

Finally, the ADC record is separated into channels and plotted using numpy and
matplotlib's interactive plot.

        Filip Dominec 2022, public domain
"""

import rp2daq 
import threading
import time

## Connect to the device
rp = rp2daq.Rp2daq()

## User settings
channel_names = {0:"pin 26", 1:"pin 27", 2:"pin 28", 3:"ref V", 4:"int thermo"}
channels = [0, 3, 4]     # 0,1,2 are pins 26-28;  3 is V_ref and 4 is internal thermometer
kSPS_per_ch = 10 * len(channels)    # note there is only one multiplexed ADC

## Run-time objects and variables
all_ADC_done = threading.Event() # a thread-safe semaphore
all_data = []
t0 = time.time()

## Initialize the ADC into asynchronous operation...
def append_ADC_data(**kwargs): #   called from other thread whenever data come from RP2
    all_data.extend(kwargs['data'])

    print(f"at {time.time()-t0:.3f} s, received {len(kwargs['data'])} new " +
            f"ADC values ({kwargs['blocks_to_send']} blocks to go)")

    if not kwargs['blocks_to_send']: # i.e. if all blocks were received
        all_ADC_done.set()   # releases wait() in the main tread

rp.internal_adc(channel_mask=sum(2**ch for ch in channels), 
        blocksize=1000*len(channels), 
        blocks_to_send=20, 
        clkdiv=48000//kSPS_per_ch, 
        _callback=append_ADC_data)

## ... OK, here we *want* to wait until all data are received (but do not have to)
all_ADC_done.wait()

rp.quit() # relase the device (or the app will wait indefinitely)

print(f"{len(all_data)} samples in {time.time()-t0}")
print(f"i.e. {len(all_data)/(time.time()-t0):.2f} samples per second on average" )



## Optional plotting of all channels
import matplotlib.pyplot as plt
import numpy as np

y = np.array(all_data)
fig, ax = plt.subplots(nrows=1, ncols=1, figsize=(12, 10))

for ofs,ch in enumerate(channels):
    ax.plot(y[ofs::len(channels)] * 3.3 / 2**12, 
            label=channel_names[ch],
            lw=.3, marker=',', c='rgbycm'[ch])
ax.set_xlabel("ADC count per channel")
ax.set_ylabel("(inaccurate) voltage measured")
ax.legend()
plt.show()

