#!/usr/bin/python3  
#-*- coding: utf-8 -*-

if __name__ == "__main__": 
    import multiprocessing
    multiprocessing.set_start_method('spawn')
# avoid spawn bomb due to multiprocessing


import tkinter      
from tkinter import ttk 
try:
    from ctypes import windll
    windll.shcore.SetProcessDpiAwareness(1)
except: 
    pass

window = tkinter.Tk()   # initialize the graphical interface
window.title("RP2DAQ test app")

if __name__ == "__main__":
    pass

label = ttk.Label(window)
label.grid(column=0, row=0)
try:
    import rp2daq
    rp = rp2daq.Rp2daq()

    id_string = "".join(chr(b) for b in rp.identify()["data"])
    label.config(text = "Successfully connected to " + id_string)

    def set_LED(state):
        rp.gpio_out(25, state) # onboard LED assigned to gpio 25 on R Pi Pico

    style = ttk.Style()
    style.configure("lime.TButton", background="lime")
    btn_on = ttk.Button(window, text='LED on', command=lambda:set_LED(1), style="lime.TButton")
    btn_on.grid(column=0, row=1)

    style.configure("red.TButton", background="red")
    btn_off = ttk.Button(window, text='LED off', command=lambda:set_LED(0), style="red.TButton")
    btn_off.grid(column=0, row=2)

except Exception as e: 
    label.config(text=str(e))

window.mainloop()
