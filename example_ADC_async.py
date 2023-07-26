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

        Filip Dominec 2023, public domain
"""

import rp2daq 
import threading
import time

## User options
ADC_channel_names = {0:"GPIO 26", 1:"GPIO 27", 2:"GPIO 28", 3:"ref V", 4:"builtin thermo"}

channels = [1,4]     # 0,1,2 are GPIOs 26-28;  3 is V_ref and 4 is internal thermometer
kSPS_per_ch = 250 * len(channels)    # note there is only one multiplexed ADC



## Connect to the device
rp = rp2daq.Rp2daq()

## Generate some realistic signal on GPIO 2 (connect it with a jumper to GPIO 26)
rp.pwm_configure_pair(gpio=2, wrap_value=6553, clkdiv=250, clkdiv_int_frac=0)
rp.pwm_set_value(gpio=2, value=3500) # minimum position
time.sleep(.1)

## Run-time objects and variables
all_ADC_done = threading.Event() # a thread-safe semaphore
all_data = []
received_time = []
delayed = []

# Called from other thread whenever data come from RP2
def ADC_callback(**kwargs): 
    global t0

    all_data.extend(kwargs['data'])
    print("LEN all, kwargs: ", len(all_data), len(kwargs['data']))
    received_time.extend([time.time()]*(len(kwargs['data'])//2))
    delayed.extend([kwargs['block_delayed_by_usb']]*(len(kwargs['data'])//2))

    if not kwargs['blocks_to_send']: # i.e. if all blocks were received
        all_ADC_done.set()   # releases wait() in the main tread

    if not t0:
        t0 = time.time()

    print(f"at {time.time()-t0:.3f} s, received {len(kwargs['data'])} new " +
            f"ADC values ({kwargs['blocks_to_send']} blocks to go)" + 
            (" DELAYED" if kwargs['block_delayed_by_usb'] else "") )


## Initialize the ADC into asynchronous operation...
t0 = None
rp.adc(channel_mask=sum(2**ch for ch in channels), 
        blocksize=2000*len(channels), 
        blocks_to_send=10, 
        clkdiv=int(48000//kSPS_per_ch), 
        _callback=ADC_callback)


## Unless all data are received, the program can continue (or wait) here

    ## Waiting option 1: the right and efficient waiting (and causes almost no delayed blocks)
#all_ADC_done.wait()  #rp.quit()

    ## Waiting option 2: moderate CPU load is also OK
#while not all_ADC_done.is_set():
    #time.sleep(.000005)

    ## Waiting option 3: stress test with busy loops (still responsive, but <1% random USB delayed blocks)
def busy_wait(t):
    t0 = time.time()
    while time.time() < t0+t: pass
while not all_ADC_done.is_set():
    rp.gpio_set(25,1)
    busy_wait(.01)
    rp.gpio_set(25,0)
    busy_wait(.01)

    # Waiting option 4: stress test with single busy loop (observed to cause random USB delayed blocks)
#while not all_ADC_done.is_set():
    #pass

print(f"Received total {len(all_data)} samples in {time.time()-t0}")
print(f"Average incoming data rate was {len(all_data)/(time.time()-t0):.2f} samples per second")


## Optional plotting of all channels
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

