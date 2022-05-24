#!/usr/bin/python3  
import sys, serial, time, struct
port = serial.Serial('/dev/ttyACM0', timeout=.2) 
c = 0
#l = 1000*2  * 1
max01 =0
accum = b''
num_ch = 4

#bs, bc, cd = 4000,   2,   96*1 # OK, fills up both bufs  always OK (up to buffer size)
#bs, bc, cd = 4000,   3,   96*1 # wrong, but understandable: data rate trouble -> overwrites 1st buf
#bs, bc, cd = 4000,   40,  112  # just at the edge, USB is just too slow
bs, bc, cd = 4000,   40,  120  # still OK: 400ksps*16bit is approx the USB througput with default packet size 


TAG = "1002" 

raw = struct.pack(r'<BBBBHHH', 
        10,  # msg length - 1
        1,    # msg code XXX
        2+4, # +2+4+8,  # ch mask
        0,  # infi
        bs,  # bs 
        bc,  # bc
        cd,  # clkdiv
        )
port.write(raw)

time.sleep(.02)

while True: 
    t0=time.time()

    bytesToRead = port.inWaiting() #port.read(bytesToRead) # test this?
    if not bytesToRead and c>250: break
    raw = port.read(bytesToRead)
    print(f"to read: {bytesToRead}")

    #raw = port.read(l)
    #accum += raw[:int(len(raw)/num_ch+1e-9) *num_ch] aligning channels??
    accum += raw
    if c==0: tst=time.time()
    t1= time.time()
    tooktime = t1-t0
    #max01 = max(max01, raw[1]*256+raw[0])
    time.sleep(0.002) # note: shall poll fast until whole pre-advertised msglen is received

    #print(f"LEN = {len(raw)} ", raw)
    #print(f"CUT = ", raw[4080:4160:4])
    #print(f"CUT = ", raw[4081:4160:4])
    #print(f"CUT = ", raw[4082:4160:4])
    #print(f"CUT = ", raw[4083:4160:4])
    #if raw[1]*256+raw[0] > 1700: print('\n', c, 'Mismatch!', raw[1]*256+raw[0], raw[:10])

    #print(f'{c:5d} receiving {l/1000} kB now took {tooktime:.4f} s, i.e. {len(raw)/tooktime/1000:8.2f} kB/s,',
    #print(f'{c:5d} {raw[1]*256+raw[0]:05d} {raw[3]*256+raw[2]:05d} {raw[5]*256+raw[4]:05d} {raw[7]*256+raw[6]:05d}', raw[:10], raw[-4:], max01, ' '*9, ((c-1)*l)/(t1-tst), end='\n')
    #if c<1: 
       #pass
       #for x in range(int(len(raw)/4)): print(raw[x*4:x*4+4])
       #print(raw[1]*256+raw[0])
       #V = ((raw[1]*256+raw[0])/2**12)*3.3
       #print(V)
       #print(27 - (V - 0.706)/0.001721)
       #quit()
    c+=1

import matplotlib, sys, os, time, collections
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np

print(len(raw))
y = np.frombuffer(accum, dtype=np.uint16)
fig, ax = plt.subplots(nrows=1, ncols=1, figsize=(12, 10))

for ch in range(num_ch):
    print('ch', ch, ':', y[ch:30:num_ch], 'len', len(y[ch::num_ch]), '...')
    ax.plot(y[ch::num_ch], lw=.3, marker=',', c='rgbycm'[ch], label=f"channel {ch}")
ax.legend()

plt.show()
#fig.savefig(f"output_{TAG}.png", bbox_inches='tight')