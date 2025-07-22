#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import numpy as np
import threading
import time
import tkinter as tk
from matplotlib.figure import Figure 
from matplotlib.backends.backend_tkagg import (FigureCanvasTkAgg,  NavigationToolbar2Tk) 
  
import example_TCD1304_readout
import rp2daq


class MainApplication(tk.Frame):
    def __init__(self, parent, *args, **kwargs):
        tk.Frame.__init__(self, parent, *args, **kwargs)
        self.parent = parent
        row = 0

        root.title('RAW CCD readout ') 
        #root.geometry("1000x800") 
        self.plot_button = tk.Button(master=root,  command=self.update_plot, text="Plot") 
        self.plot_button.pack() 
        # [ single ]

        self.fig = Figure(figsize=(15, 10), dpi=90, layout="constrained") #
        self.plot1 = self.fig.add_subplot(111) 
        self.plot1.grid()
        self.lines = [] # prepare empty data lines
        for color in 'rygcbm': self.lines.extend(self.plot1.plot([0,0], color))

        #self.plot1.set_xscale('log')
        #self.plot1.set_yscale('log')
        self.plot1.set_xlim(xmin=0, xmax=7700)
        self.plot1.set_ylim(ymin=1e0, ymax=1e5)

        #lines.extend(plot1.plot(y)) 
        self.canvas = FigureCanvasTkAgg(self.fig, master=root)   
        self.canvas.draw() 
        self.canvas.get_tk_widget().pack(fill="both", expand=True) 
        self.toolbar = NavigationToolbar2Tk(self.canvas, root) 
        self.toolbar.update() 
        self.canvas.get_tk_widget().pack(fill="x", expand=False)


        self.rp = rp2daq.Rp2daq()       # tip: you can use required_device_id='42:42:42:42:42:42:42:42'
        example_TCD1304_readout.init_CCD(self.rp)

        self.CCD_acquisition_finished = threading.Event()
        self.acquisition_running = False

        self.ADC_oversample = 1
        self.int_time = .5
        self.t0 = time.time()


          
        #self.lbl_Progress = tk.Label(self, text='Set config & click Start')
        #self.lbl_Progress.grid(column=1, row=row, columnspan=2); row+=1
        #self.btn_Process = tk.Button(self, text='Process!', command=self.btn_Start_click)
        #self.btn_Process.grid(column=1, row=row, columnspan=2); row+=1

    #def btn_Start_click(self):
        #print('start')

    def ADC_callback(self, rv): 
        print(len(rv.data))
        deadtime=0.005
        if self.ADC_oversample == 2:
            self.new_spectrum = [(y1+y2)/2/(self.int_time-deadtime) for y1,y2 in zip(rv.data[:-1:2], rv.data[1::2])]  
        else:
            self.new_spectrum = [y/(self.int_time-deadtime) for y in rv.data]
        y_ref = sum(self.new_spectrum[:5])/5
        self.new_spectrum = [y_ref-y for y in self.new_spectrum]

        self.CCD_acquisition_finished.set()

    def update_plot(self): 
        if self.acquisition_running: return

        self.t0 = time.time()
        print(time.time() - self.t0, 'start upd' )


        self.acquisition_running = True

        #for n,t in enumerate([ .02, ]): #, 1, 3]):
        #for n,t in enumerate([0.012, .02, .05, .1, ]): #, 1, 3]):
        #for n,t in enumerate([.02, .05,   .1, .2, .5, ]): #, 1, 3]):
        for n,t in enumerate([           .1, .2, .5, 1., 2.]): #, 1, 2 ]): #, 1, 3]):
        #for n,t in enumerate([                  .5, 1., 2,  5, 10,]): #, 1, 3]):
            self.int_time = t
            time.sleep(.01)

            self.CCD_acquisition_finished.clear()
            example_TCD1304_readout.start_CCD_acquisition(self.rp, 
                    self.ADC_callback, int(self.int_time*1e6), oversample=self.ADC_oversample) # takes some 120ms on my notebook
            self.CCD_acquisition_finished.wait()
            self.lines[n].set_data(range(len(self.new_spectrum)), np.array(self.new_spectrum)-t*0) 
            self.lines[n].set_label(str(t))

        self.canvas.draw()
        self.acquisition_running = False


        print(time.time() - self.t0, 'unlock' )
        root.after(20, self.update_plot)
    

if __name__ == "__main__":
    root = tk.Tk()
    MainApplication(root).pack(side="top", fill="both", expand=True)
    root.mainloop()

## TODO
# Electrical de-noise (ADC VCC cap, 3.3 V stabilized source for CCD)



