#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import tkinter      
window = tkinter.Tk()   # initialize the graphical interface
window.title("RP2DAQ test app")

label = tkinter.Label(window)
label.grid(column=0, row=0)

try:
    import rp2daq
    rp = rp2daq.Rp2daq()

    id_string = "".join(chr(b) for b in rp.identify()["data"])
    label.config(text = "Successfully connected to " + id_string)

    def set_LED(state):
        rp.pin_set(25, state) # onboard LED assigned to pin 25 on R Pi Pico

    btn_on = tkinter.Button(window, text='LED on', bg='green3', command=lambda:set_LED(1))
    btn_on.grid(column=0, row=1)

    btn_off = tkinter.Button(window, text='LED off', bg='red3', command=lambda:set_LED(0))
    btn_off.grid(column=0, row=2)

except Exception as e: 
    label.config(text=str(e))

window.mainloop()
