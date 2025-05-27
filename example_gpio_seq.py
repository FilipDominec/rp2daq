#!/usr/bin/python3  
#-*- coding: utf-8 -*-

## This repeatedly sends an arbitrary bit pattern to the GPIO 0, 1 and 25 outputs. Any other
## combination of bits is possible, too - only make sure it is set in the bit mask.

## Some tips:
## * Note that maximum count of patterns in one sequence is 16.
## * GPIO 25 (i.e. on-board LED) is the maximum number to be set by the sequence. Further binary
##   digits are ignored
## * If the sequence behaves weird, make sure the bit mask has 1 on all pins being used.
##   Values set to GPIOs not set by the mask have no effect at all.
## * It may be more convenient to address GPIOs by bit-shifting, like 1<<25, instead of long binary 
##   numbers like here).
## * After a sequence finishes, the used GPIOs are left in the output state of its last stage.
## * Given by the USB messaging and Python code execution, the delay between two consecutive sequences 
##   is some 2-3 ms, with significant jitter. 
## * Re-defining active sequences asynchronously wasn't tested yet. Use at your own risk.

import rp2daq, time
rp = rp2daq.Rp2daq()

# The microsecond delay between bit pattern stages
delay_us = 1
# but e.g. one minute or even more between stages is also possible, use: 60*10**6

while True:
    print(
                       #         2. ...2 ...1 ...1 .1
                       #         5. ...0 ...6 ...2 .098_7654_3210   = GPIO numbering 
        rp.gpio_out_seq(0b0000_0010_0000_0000_0000_0000_0000_0011,    # the bit mask
                        0b0000_0010_0000_0000_0000_0000_0000_0011, 10, #  0
                        0b0000_0000_0000_0000_0000_0000_0000_0010, delay_us, #  1
                        0b0000_0000_0000_0000_0000_0000_0000_0001, delay_us, #  2
                        #0b0000_0000_0000_0000_0000_0000_0000_0010, delay_us, #  3
                        #0b0000_0010_0000_0000_0000_0000_0000_0000, delay_us, #  4
                        #0b0000_0000_0000_0000_0000_0000_0000_0010, delay_us, #  5
                        #0b0000_0010_0000_0000_0000_0000_0000_0000, delay_us, #  6
                        #0b0000_0000_0000_0000_0000_0000_0000_0010, delay_us, #  7
                                                  
                        #0b0000_0010_0000_0000_0000_0000_0000_0000, delay_us, #  8
                        #0b0000_0000_0000_0000_0000_0000_0000_0000, delay_us, #  9
                        #0b0000_0010_0000_0000_0000_0000_0000_0011, delay_us, # 10
                        #0b0000_0000_0000_0000_0000_0000_0000_0010, delay_us, # 11
                        #0b0000_0010_0000_0000_0000_0000_0000_0000, delay_us, # 12
                        #0b0000_0000_0000_0000_0000_0000_0000_0011, delay_us, # 13
                        #0b0000_0010_0000_0000_0000_0000_0000_0001, delay_us, # 14
                        #0b0000_0000_0000_0000_0000_0000_0000_0000, delay_us, # 15  
                        )
        )

