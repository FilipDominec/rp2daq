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

## User options
channel_names = {0:"pin 26", 1:"pin 27", 2:"pin 28", 3:"ref V", 4:"int thermo"}
#channels = [0, 3, 4]     # 0,1,2 are pins 26-28;  3 is V_ref and 4 is internal thermometer
#kSPS_per_ch = 50 * len(channels)    # note there is only one multiplexed ADC
channels = [0,1]     # 0,1,2 are pins 26-28;  3 is V_ref and 4 is internal thermometer
kSPS_per_ch = 250 * len(channels)    # note there is only one multiplexed ADC


# TODO: FIXME with total sample rate approx > 100k kSPS, order of bytes appear as temporarily swapped
#           persists with 1 channel
#           not sure if existed before
#           must check if wforms look nice
###80, 0, 76, 0, 80, 0, 76, 76, 5, 0, 76, 5, 32, 76, 5, 0, 76, 5, 16
rp.pwm_configure_pair(pin=0, wrap_value=6553, clkdiv=25, clkdiv_int_frac=0)
rp.pwm_set_value(pin=0, value=3000) # minimum position


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
        blocksize=2000*len(channels), 
        blocks_to_send=100, 
        clkdiv=48000//kSPS_per_ch, 
        _callback=append_ADC_data)

## ... OK, here we *want* to wait until all data are received (but do not have to)
all_ADC_done.wait()
#rp.quit()
#while not all_ADC_done.is_set():
    #pass
    #rp.pin_set(28,1)
    #time.sleep(.05)
    #rp.pin_set(28,0)
    #time.sleep(.05)

#rp.quit() # relase the device (or the app will wait indefinitely)

#print(f"{len(all_data)} samples in {time.time()-t0}")
#print(f"i.e. {len(all_data)/(time.time()-t0):.2f} samples per second on average" )

t0 = time.time()
import numpy




#all_data=[8,3,8,2,3,9,2,8,4,1,9,8,3,4,7,1,2,0,9,8,4,3,1,0,2,9,3,4,7,1,0,2,9,3,7,4,1,5,1,]*10
## Optional plotting of all channels
import matplotlib.pyplot as plt
import numpy as np

print( time.time() - t0)

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

