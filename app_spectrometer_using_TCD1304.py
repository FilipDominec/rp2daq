#!/usr/bin/python3  
#-*- coding: utf-8 -*-

""" Linear CCD arrays, like Toshiba TCD1304, contain a row of several thousands
photodiodes which integrate charge proportional to their illumination. After a
user-defined "shutter" time, this charge can be transferred to a charge-coupled
device (CCD) array and read out as voltage drop at the analog output pin. The
operation of a CCD detector thus requires driving several control pins, and
simultaneously quite fast digitization of its output voltage. 

Raspberry Pi Pico with rp2daq appears ideal for this task. This program is
split into two classes: 

 * `TCD1304_Backend` which is specific for accessing the CCD hardware, and can be 
 reused when this Python file is imported as a module; and 
 * `GraphicalSpectrometerApp` which shows the recorded intensity profiles in a
 window and provides basic operations one would use with a handheld
 spectrometer - a typical use of this high-resolution linear CCD array.

The optical & mechanical design of the spectrometer optics can vary; there are
good resources online for this. But you can also easily rewrite this class for
your own purposes, as there are other uses of linear CCD like barcode and
flatbed scanners, accurate position sensors etc.
"""

from matplotlib.figure import Figure 
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
import numpy as np
import threading
import time
import tkinter as tk
  
import rp2daq


class TCD1304_Backend:

    def __init__(self, rp, verbose=False):
        """
        Configures the Pico so that CCD sensor is ready to start acquisition. 
        """

        # Hardware GPIO pins assignment (Note these are hardcoded; if you
        # change the numbers, you must rearrange the columns in the `gpio_seq`
        # command below!):

        self.S, self.V, self.I, self.P, self.A, self.M = 8, 9, 10, 11, 6, 7  
            # SH, VDD, ICG, PhiM GPIO numbers are directly connected to these pins on CCD (see datasheet)
            # A is ADC_lock for Pico to start sampling (without software jitter)
            # M is an optional sync marker to see the real integration time on oscilloscope

        # Timing of the signals is nontrivial and can't be efficiently done by
        # software control of GPIO. The PhiM clock is driven by Pico's hardware
        # PWM; other pins are driven by software as a predefined sequence. But
        # fast sampling is again accomplished by hardware ADC running exactly
        # at 1/4 frequency of PhiM as required by the datasheet.

        # TCD1304 needs 500-2000 kHz clock signal PhiM; when driven by pico's
        # PWM, PhiM frequency must be divisor of 250.000 MHz pico's system
        # clock and simultaneously of 48.000 MHz pico's ADC clock. Maximum ADC
        # rate is however 500ksps. The only three reasonable values are these:

        #self.CCD_freq_kHz = 2000    
             #oversample=1 -> 500ksps -> 7ms/spectrum  
             # (FIXME: single ADC channel couldn't run sample exact 500ksps, check this again)
             # (faster readout - shorter shutter time achievable - higher dynamic range of HDR)
        self.CCD_freq_kHz = 1000    # TCD1304 needs 500-2000 kHz  
            # oversample=1 -> 250ksps -> 14ms/spectrum 3600pts 
            # oversample=2 -> 500ksps -> 14ms/spectrum 7200pts
            # (FIXME: this works nice, everything in sync, but the mutual phase of PhiM and ADC taking sample is random?)
        #self.CCD_freq_kHz = 500    # TCD1304 needs 500-2000 kHz 
            # oversample=1 -> 125ksps -> 28ms/spectrum  3600pts 
            # oversample=2 -> 250ksps -> 28ms/spectrum  7200pts

        # Fixed configuration values from the Toshiba TCD1304 datasheet: 
        self.CCD_pixel_count = 16+13+3+3648+14  # 16 dummy + 13 lightshielded + 3 dummy + 3648 signal + 14 dummy
        self.px_per_clk = 4
        self.minimum_readout_us = self.CCD_pixel_count*self.px_per_clk//self.CCD_freq_kHz 

        # Start the PhiM clock signal 
        PhiMwrap = 250_000//self.CCD_freq_kHz - 1 
        self.rp = rp
        self.rp.pwm_configure_pair(gpio=self.P, wrap_value=PhiMwrap, clkdiv=1) # 500 kHz = full readout in 7.3 ms
        self.rp.pwm_set_value(gpio=self.P, value=PhiMwrap//2) # ~50% duty cycle


        # Initialize GPIO values (FIXME: they shouldn't be hardcoded in the sequence bits)
        rp.gpio_out(self.V, 1)  # FIXME: this should be also safely done by sequence
        rp.gpio_out(self.S, 0) 
        rp.gpio_out(self.I, 1) 
        rp.gpio_out(self.A, 1) 
        rp.gpio_out(self.M, 0) 
                          #         ba98_7654_3210   = bit numbering 
                          #         PIVS MA__ ____   = bit assignment towards CCD 
        rp.gpio_out_seq(          0b0111_1100_0000, # <--- bit mask
                                  0b0110_0100_0000, 1 )

        # todo: try setting GPIO23 to control the onboard SMPS power save enable pin, could reduce noise? 

        self.verbose=verbose

    def start_CCD_acquisition(self, rp, callback_on_ADC_data_ready, shutter_us, oversample=1):
        # Every falling edge of SH ends one integration period, and starts a new one
        # Every falling edge of ICG starts a readout period
        # Datasheet specifies some minimum timings, but command/report round-trip times are safely much longer (some 20us)

        assert shutter_us > self.minimum_readout_us

        ## Arm internal ADC of RP2 to start on first A-pin trigger falling edge.
        ## Note that timing of the ADC sequence is accurately in-sync with the driving signal thanks 
        ## to (1) the ADC using direct memory access independent on Pico's CPU load , and 
        ## (2) its being triggered directly by one pins in the readout sequence below.
        clkdiv = int(48000//(self.CCD_freq_kHz/self.px_per_clk*oversample) - 1)  
        # TODO This is weird. Find out why the above code made ADC in sync with CCD clk (i.e. 250ksps or slower) ...
        #clkdiv = int(48000//(self.CCD_freq_kHz/px_per_clk*oversample) - 0) 
        #  ... but this gets out-of-sync. However it allows for up to 500 ksps, and 2MHz PhiM clock.

        if self.verbose: 
            print('Arming ADC with  clkdiv=,', clkdiv, ', aiming for sampling rate ',  48e6/(clkdiv+1))
        rp.adc(channel_mask=0x01,
                blocksize=self.CCD_pixel_count*oversample,  
                blocks_to_send=1, 
                trigger_gpio=self.A,
                trigger_on_falling_edge=1, 
                clkdiv=clkdiv,  # 4 master clock = 1 CCD pixel shift
                _callback=callback_on_ADC_data_ready,
                )

        # CCD pre-flush - improves signal to noise ratio and recovery from deep oversaturation
        for x in range(10):
            rp.gpio_out_seq(   0b0111_1100_0000, # this is the mask of bits being changed
                               0b0011_0100_0000,    1, # SH strobe to flush the CCD
                               0b0010_0100_0000,    self.minimum_readout_us, 
                               0b0110_0100_0000,    1, # integ time
                               )

        # CCD driving sequence from datasheet, also starts ADC acquisition in sync with CCD signal.
        # Hex bit numbering: ba98_7654_3210   
        # Bit names on CCD:  PIVS MA__ ____   
        rp.gpio_out_seq(   0b0111_1100_0000, # this is the mask of bits being changed

                         # 0b0110_0100_0000,       # bit state from previous gpio_out_seq
                           0b0011_0100_0000,    1, # SH strobe to flush the CCD
                           0b0010_0100_0000,    1, 

                           0b0110_1100_0000,    shutter_us, # integ time from 20_000 up to 50_000_000 microseconds
                                                # TODO split the seq_command here, making its 1st half 
                                                # asynchronous, to avoid blocking the thread with long shutters
                           0b0011_0100_0000,    1, # SH strobe to get useful signal
                           0b0010_0100_0000,    1, # must wait before ICG goes up
                           0b0110_0000_0000,    1, # start ADC in sync with CCD output
                           0b0110_0100_0000,    1) 

        ## No return values here; the user-supplied `callback` will be called when ADC accumulation is finished



class MainApplication(tk.Frame):
    def __init__(self, parent, *args, **kwargs):
        tk.Frame.__init__(self, parent, *args, **kwargs)
        self.parent = parent

        root.title('RAW CCD readout ') 
        #root.geometry("1000x800") 
        self.plot_button = tk.Button(master=root,  command=self.update_plot, text="Start scanning") 
        self.plot_button.pack() 

        ## Connect to the Pico and CCD hardware
        self.rp = rp2daq.Rp2daq()   # initialize rp2daq device first, to keep independent access to its other functions     
        self.my_CCD = TCD1304_Backend(self.rp)  # then make it initialize CCD-specific pins & configuration

        ## Initialize the plotting canvas
        self.lines = [] # a list to store plotted lines, their values can be efficiently updated
        self.fig = Figure(figsize=(15, 10), dpi=90, layout="constrained") #
        self.ax1 = self.fig.add_subplot(111) 
        for color in 'rygcbm':
            self.lines.extend(self.ax1.plot([0,0], color, label='', alpha=.5))
        self.ax1.grid()
        self.ax1.legend(prop={'size':10}, loc='upper right')
        self.ax1.set_yscale('log')
        self.ax1.set_xlim(xmin=320, xmax=750) # TODO generic # self.my_CCD.CCD_pixel_count .. for raw pixel number
        self.ax1.set_ylim(ymin=1e0, ymax=1e6)

        self.canvas = FigureCanvasTkAgg(self.fig, master=root)   
        self.canvas.draw() 
        self.canvas.get_tk_widget().pack(fill="both", expand=True) 
        self.toolbar = NavigationToolbar2Tk(self.canvas, root) 
        self.toolbar.update() 
        self.canvas.get_tk_widget().pack(fill="x", expand=False)

        ## Getting ready to start acquisition
        self.CCD_acquisition_finished = threading.Event() # future TODO: rewrite to use 2nd thread
        self.acquisition_running = False

        self.ADC_oversample = 1
        self.int_time = .5
        self.t0 = time.time()

        #self.lbl_Progress = tk.Label(self, text='Set config & click Start')
        #self.lbl_Progress.grid(column=1, row=row, columnspan=2); row+=1
        #self.btn_Process = tk.Button(self, text='Process!', command=self.btn_Start_click)
        #self.btn_Process.grid(column=1, row=row, columnspan=2); row+=1

    def callback_on_ADC_data_ready(self, rv): # this will be called from rp2daq module, the `rv` contains ADC results
        deadtime=0.005
        if self.ADC_oversample == 2:
            self.new_spectrum = [(y1+y2)/2/(self.int_time-deadtime) for y1,y2 in zip(rv.data[:-1:2], rv.data[1::2])]  
        else:
            self.new_spectrum = [y/(self.int_time-deadtime) for y in rv.data]
        y_ref = sum(self.new_spectrum[:5])/ 4.999
        self.new_spectrum = [y_ref-y for y in self.new_spectrum]

        self.CCD_acquisition_finished.set()

    def update_plot(self): 
        if self.acquisition_running: return # is this necessary? 

        self.t0 = time.time(); #print(time.time() - self.t0, 'start upd' )

        self.acquisition_running = True

        #for n,shutter_time_s in enumerate([ .014, 0.14, 1.4, ]):  # SINGLE SCAN
        #for n,shutter_time_s in enumerate([0.012, .02, .05, .1, ]): #, 1, 3]): # HDR - HIGH ILLUM
        #for n,shutter_time_s in enumerate([.02, .05,   .1, .2, .5, ]): #, 1, 3]): # HDR - MEDIUM ILLUM
        #for n,shutter_time_s in enumerate([           .1, .2, .5, 1., 2.]):  # HDR - LOW ILLUM
        #for n,shutter_time_s in enumerate([                  .5, 1., 2,  5, 10,]):  # HDR - VERY LOW ILLUM
        #for n,shutter_time_s in enumerate([.02, .02,   .1, .5,  .1, .02]): #, 1, 3]): # TESTING REPEATABILITY
        #for n,shutter_time_s in enumerate([0.014, 0.02, 0.1, .5, 2,]):  # Ultra HDR
        for n,shutter_time_s in enumerate([0.012,   ]*5): #, 1, 3]): # fast oversample
            self.int_time = shutter_time_s
            time.sleep(.01) # FIXME is this necessary, why?

            self.CCD_acquisition_finished.clear()
            self.my_CCD.start_CCD_acquisition(self.rp, 
                    self.callback_on_ADC_data_ready, int(self.int_time*1e6), oversample=self.ADC_oversample) # takes some 120ms, why?

            # The callback_on_ADC_data_ready stores results into self.new_spectrum now, then 
            # clears the CCD_acquisition_finished semaphore, so we can plot it here.
            self.CCD_acquisition_finished.wait()
            #self.lines[n].set_data(range(len(self.new_spectrum)), np.array(self.new_spectrum)-shutter_time_s*0) 
            p1,l1,p2,l2 = 534, 404., 1805, 532.
            pps = np.arange(len(self.new_spectrum))
            self.lines[n].set_data(
                    l1 + (pps-p1) * (l2-l1)/(p2-p1),
                    np.array(self.new_spectrum)-shutter_time_s*0) 
            self.lines[n].set_label(str(shutter_time_s))

        self.canvas.draw()
        self.acquisition_running = False


        print(time.time() - self.t0, ' s for whole update routine' )
        root.after(20, self.update_plot) # periodic update
    

def crude_CCD_test(): # minimum code for testing
    rp = rp2daq.Rp2daq() 
    my_CCD = TCD1304_Backend(rp)
    def dummy_callback(args): 
        print(args)
    while True: # test CCD by just reading data
        my_CCD.start_CCD_acquisition(rp, dummy_callback, 30_000)


if __name__ == "__main__":
    root = tk.Tk()
    MainApplication(root).pack(side="top", fill="both", expand=True)
    root.mainloop()



