#!/usr/bin/python3  
#-*- coding: utf-8 -*-
"""
This slightly fancier example receives multiple data blocks from the built in ADC 
(analog-to-digital converter) Raspberry Pi Pico.

It demonstrates how a virtually unlimited amount of data can be acquired, with
the main thread staying responsive. Such asynchronous, callback-based operation is 
generally more flexible and efficient. 

Rp2daq implements direct-memory access (DMA), triple-buffering and bit-compression 
in the firmware, along with multi-threading and -processing in the python module, to 
achieve reliable uninterrupted data stream at 500k × 12bit samples per second.

Finally, the ADC record is separated into channels and plotted using numpy and
matplotlib's interactive plot.

        Filip Dominec 2023, public domain
"""

## User options
ADC_channel_names = {0:"GPIO 26", 1:"GPIO 27", 2:"GPIO 28", 3:"ref V", 4:"builtin thermo"}

channels = [0,1]     # 0,1,2 are GPIOs 26-28;  3 is V_ref and 4 is internal thermometer

kSPS_total = 500    # with short (1000sample) packets it causes USB data loss (leading sometimes to USB comm freezing) at my notebook
#kSPS_total = 480     # ?
#kSPS_total = 400     # seems safe (even at CPU_STRESS=2, but the printout is jerky)

CPU_STRESS = 0 # use 0 for thread wait (good practice), 1 for a loop with short time.sleep(), 2 for a busy loop


import rp2daq 
import threading
import time

## Connect to the device
rp = rp2daq.Rp2daq()

## Generate some realistic signal on GPIO (e.g. connect it through an RC filter to GPIO 26)
rp.pwm_configure_pair(gpio=17, wrap_value=65530, clkdiv=25, clkdiv_int_frac=0)
rp.pwm_set_value(gpio=17, value=12500) # minimum position
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
            -rv.start_time_us+rv.end_time_us,
            -prev_etime_us+rv.start_time_us,
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
        blocksize=4000*len(channels), 
        blocks_to_send=10, 
        trigger_gpio=17,
        trigger_on_falling_edge=1,
        clkdiv=int(48000//kSPS_total), 
        _callback=ADC_callback)
## Unless all data are received, the program can continue (or wait) here. A dedicated separate process
## ensures that raw data are quickly offloaded from USB into a queue, so that no message is corrupted. 
## High CPU load in this user script can however lead to delays in the callbacks being issued.
## In particular, tight busy loops in main thread (option 4 below) will cause callback delays, so 
## maximum sustained data rate may be roughly halved on ordinary modern computer (<300 kBps).



if CPU_STRESS == 0:
    all_ADC_done.wait() ## Waiting option 1: the right and efficient waiting (data rate OK, no loss)
elif CPU_STRESS == 1:
    while not all_ADC_done.is_set(): # Waiting option 2: moderate CPU load is (also OK)
        time.sleep(.000005)
elif CPU_STRESS == 2:
    tt = time.time()
    def busy_wait(t): # Waiting option 3: stress test with busy loops (still OK)
        t0 = time.time()
        while time.time() < t0+t: pass
    while not all_ADC_done.is_set(): #rp.gpio_out(25,1)
        busy_wait(.05)
        #if time.time() > tt+.31:    # optionally, stop the stream
            #print(rp.adc_stop()) #rp.gpio_out(25,0)
        busy_wait(.05)

# FIXME! 
# stopping ADC batch in the middle results in:
# ... 
#Packet received 452000 2000 4044 3 0 
#Packet received 454000 2000 4044 4 0 
#Packet received 456000 2000 4044 1469 0 
#2024-07-15 19:46:20,603 (Thread-2 ) Warning: Got callback that was not asked for
	#Debug info: {'report_code': 0, '_data_count': 30, '_data_bitwidth': 8, 'data': b'rp2daq_240715_E66118604B52522A'}


#rp.gpio_out(25,1) # Waiting option 4: stress test with single busy loop
#while not all_ADC_done.is_set(): pass
#rp.gpio_out(25,0) # note this only happens after all data is received on computer side

print(f"Received total {len(all_data)} samples in {time.time()-t0}")
print(f"Average processed data rate was {len(all_data)/(time.time()-t0):.2f} samples per second")

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

