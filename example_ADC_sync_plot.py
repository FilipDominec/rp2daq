#!/usr/bin/python3  
#-*- coding: utf-8 -*-

## User options
width, height = 1000, 1000
channels = [0, 1, 2,] #,  3, 4]     # 0,1,2 are GPIO 26-28;  3 is V_ref and 4 is internal thermometer
kSPS_per_ch = 100 * len(channels)  # one-second run



import rp2daq
import sys
import tkinter

rp = rp2daq.Rp2daq()

class StupidPlot(tkinter.Frame): 
    def __init__(self):
        super().__init__()
        self.pack(fill=tkinter.BOTH, expand=1)
        self.canvas = tkinter.Canvas(self, bg="#111")
        self.canvas.pack(fill=tkinter.BOTH, expand=1)
        self.drawnlines = []

    def plot(self, channel_data): # That's it. 4 lines of code to plot data as coloured lines.
        for values, color in zip(channel_data, ("red2", "green2", "cyan", "yellow2", "violet")):
            values = [(height-v//4) for v in values]
            self.drawnlines.append(self.canvas.create_line(*enumerate(values), fill=color))
        #Label4 = canvas.create_text(30, 46, text='aoeu', fill="green") #del(Label4)

root = tkinter.Tk()
root.geometry(f"{width}x{height}+0+200")

stupidplot = StupidPlot()
#for x in range(5):
ADC_data = rp.adc(
        channel_mask=sum(2**ch for ch in channels),
        blocksize=width*len(channels),  # one ADC sample per ?????, per channel
        clkdiv=48000//kSPS_per_ch)
print(ADC_data)
channel_data = [ADC_data[ofs::len(channels)] for ofs,name in enumerate(channels)]
stupidplot.plot(channel_data)

root.mainloop()
