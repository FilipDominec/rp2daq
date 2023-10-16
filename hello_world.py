#!/usr/bin/python3  
#-*- coding: utf-8 -*-


from tkinter import Tk, ttk 

window = Tk()   # Initialize the graphical interface first
label = ttk.Label(window)
label.grid(row=0)

try:
    ## Following seven lines are essential for the hardware control:
    import rp2daq
    rp = rp2daq.Rp2daq()
    label.config(text=f' Successfully connected to {rp.identify()["data"].decode()} ')

    def set_LED(value): 
        rp.gpio_out(gpio=25, value=value) # reused for both buttons
    btn_on  = ttk.Button(window, text='LED on',  command=lambda:set_LED(1), style='g.TButton').grid(row=1)
    btn_off = ttk.Button(window, text='LED off', command=lambda:set_LED(0), style='r.TButton').grid(row=2)


    ## Following lines are useful tweaks for the graphical user interface:
    window.title('RP2DAQ test app')
    window.bind('1', lambda x: set_LED(1)) # hit 1 to turn LED on
    window.bind('0', lambda x: set_LED(0)) # hit 0 to turn LED off
    window.bind('<Escape>', lambda x: exit()) # hit esc to close the app
    style = ttk.Style()
    style.theme_use('clam') 
    style.configure('g.TButton', background='lime')
    style.configure('r.TButton', background='red')
    import ctypes
    if 'windll' in dir(ctypes): ctypes.windll.shcore.SetProcessDpiAwareness(1) # sharp fonts on Win

except Exception as e: # this allows printing runtime errors in the graphical window
    label.config(text=str(e))

window.mainloop()
