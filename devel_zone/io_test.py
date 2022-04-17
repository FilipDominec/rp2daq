#!/usr/bin/python3  
#-*- coding: utf-8 -*-

import sys, serial, time, struct
port = serial.Serial('/dev/ttyACM0', timeout=1) 

time.sleep(.1)

b = 48
while 1:
    b = b+1
    if b>98: b=48
    raw = struct.pack(r'<BB', 
            1,  # msg length - 1
            0,    # msg code XXX
            #b, 
            #3, 
            )

    print('.',b,end='')
    port.write(raw)
    time.sleep(.20) # let 20ms round-trip time to reliably get answer (from non-delayed commands)
    bytesToRead = port.inWaiting(); 
    if bytesToRead:
        print(' ', bytesToRead, port.read(bytesToRead)) # test this?

    time.sleep(.003)

    quit() ##DEBUG XXX

    #raw = port.read(6)
    #print(raw)

    #if sys.platform.startswith('win'):
        #ports = ['COM%s' % (i + 1) for i in range(256)]
    #elif sys.platform.startswith('linux') or sys.platform.startswith('cygwin'):
        #ports = glob.glob('/dev/ttyUSB*') 
    #elif sys.platform.startswith('darwin'): 
        #ports = glob.glob('/dev/tty.SLAB_USBtoUART*')  # or glob.glob('/dev/tty.*') ?
    #else:
        #raise EnvironmentError('Unsupported platform')
