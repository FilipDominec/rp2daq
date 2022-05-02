## Detailed features - roadmap

 * implemented & tested 
	 * [x] identification message
 * under development
	 * [ ] voltage measurement (internal 12-bit 500kSPS ADC) 
        * [ ] additionally, with lookup-table calibration, oversampling and burst capability
	 * [ ] stepper motor (using Stepstick - A4988) with end-stop support
	 * [ ] digital pin input/output
	 * [ ] pulse width modulation (built-in PWM in RP2)
        * [ ] wform generator using DMA channel, along https://gregchadwick.co.uk/blog/playing-with-the-pico-pt2/
 * planned to be added
    * [ ] high-frequency generator (direct ```clock_gpio_init``` on pins )
    * [ ] inbuilt frequency counter and accurate timer,
	* [ ] arbitrary I2C, 1wire etc. interfaces 
        * [ ] examples of their use for several popular sensors like DHT22/AM2320 temp/humi meters, ADXL345 accelmeter, HC_SR04 rangefinder ...
        * [ ] medium-speed external ADCs (e.g. [AD7685](https://www.analog.com/en/products/ad7685.html#product-overview) through I2C/SPI, 16-bit, 250 kSPS)
        * [ ] burst mode (like internal ADC), with optional programmable delay
        * [ ] optional synchronized ICG/φM/SH driving signals for [TCD1304](https://pdf1.alldatasheet.com/datasheet-pdf/view/32197/TOSHIBA/TCD1304AP.html) (linear charge-coupled light sensor)
        * [ ] medium-speed external DACs (through I2S, e.g. [TDA1543](http://www.docethifi.com/TDA1543_.PDF) dual 192kHz 16-bit DACs; multichannel I2S protocol implemented in software)
        * [ ] I2S input (e.g. from INMP441, MSM261S4030H0 or SPH0645 digital microphones)
	* [ ] user-defined data storage in flash memory unused by firmware https://www.aranacorp.com/en/using-the-eeprom-with-the-rp2daq/
	* [ ] high-speed external ADC (i.e. oscilloscope, using built-in PIO 8-Bit 100MSps, e.g. AD9288-100)
 * considered, not planned in near future
    * [ ] fast & autonomous pipe-lining infrastructure (all tasks can actually be some pipe elements) 
        * pipeline sources: USB message received, task finished, periodic timer, digital pin trigger, numeric ramp generator
        * pipeline operators: moving average, pairwise lock-in detection of interlaced signals, Kálmán filter, PID regulator, boxcar, multichannel pulse histogram, ...
        * pipeline ends: USB data transmit, setting PWM, transmit I2S 
    * [ ] high-speed external DAC (+ support for direct digital synthesis, AD9708ARZ?)
	* [ ] SCCB support for cameras like OV2640
	* [ ] user data in EEPROM https://www.aranacorp.com/en/using-the-eeprom-with-the-rp2daq/




## Random notes 

Examples like this? 

| Name |             Signature Code                 |
|----------------------------------------------|--------------------------------|
| <pre><i>#infile = os.path.realpath(__file__)</i><br>    <i>#print("DEBUG: infile = ", infile)</i><br>    settings <i>=</i> <i>{</i><i>}</i><br>    <b>with</b> <b>open</b><i>(</i>infile<i>)</i> <b>as</b> f<i>:</i><br>        <b>for</b> l <b>in</b> f<i>.</i>readlines<i>(</i><i>)</i><i>:</i><br>            l <i>=</i> l<i>.</i>split<i>(</i><b>'#'</b><i>)</i><i>[</i>0<i>]</i> <i># ignore comments</i><br>            k<i>,</i>v <i>=</i> <i>[</i>s<i>.</i>strip<i>(</i><i>)</i> <b>for</b> s <b>in</b> l<i>.</i>split<i>(</i><b>'='</b><i>,</i> 1<i>)</i><i>]</i>  <i># split at first '=' sign</i><br>            settings<i>[</i>k<i>]</i> <i>=</i> v<br>    <b>return</b> settings<br></pre> | ![](output_1000.png) |

temperature reads some 258 degree C - why? Compile from examples!

correctly implement messaging (instead of raw data stream)
    * computer: receive & dispatch callbacks
    * computer: auto-generate low-level func

find out if TinyUSB buffer size can be changed, and if it changes bulk transfer rate closer to 12Mbit/s = 1008 kBps
    Optimize USB throughput beyond 850 kBps, tweaking TinyUSB or running more endpoints?
        https://www.pjrc.com/teensy/benchmark_usb_serial_receive.html
        https://github.com/micropython/micropython/pull/7492 rp2/tusb_config.h: Set CFG_TUD_CDC_EP_BUFSIZE to 256
        https://github.com/micropython/micropython/pull/7492
        #define TUD_OPT_HIGH_SPEED (1) // TODO!


post link to rp2daq code to 
    https://forums.raspberrypi.com/viewtopic.php?p=1962593#p1962593
    https://forums.raspberrypi.com/viewtopic.php?p=1963278#p1963278


Contribute back with  non-blocking DMA as alternative to:
    https://github.com/raspberrypi/pico-examples/blob/master/adc/dma_capture/dma_capture.c

	// simple, but stable frequency generators
    clock_gpio_init(21, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 2); // f_base = 250.0 MHz
	gpio_set_dir(21, GPIO_OUT);
    clock_gpio_init(23, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_USB, 1); // GPIO23=TP4 f_base = 48.00 MHz
	gpio_set_dir(23, GPIO_OUT);
    clock_gpio_init(24, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_ADC, 1); // GPIO24=TP5 f_base = 46.875 kHz
	gpio_set_dir(24, GPIO_OUT);
    clock_gpio_init(25, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 1); // GPIO25=TP6 constant 3.3V (?)
	gpio_set_dir(25, GPIO_OUT);
    //* TP1 Ground (close coupled ground for differential USB signals)
        TP2 USB DM
        TP3 USB DP
        TP4 GPIO23/SMPS PS pin (do not use)
        TP5 GPIO25/LED (not recommended to be used)
        TP6 BOOTSEL */

* remove dependency on pyserial module?  - you can copy the serial folder into your project as a package, and it would work without installing anything.you can copy the serial folder into your project as a package, and it would work without installing anything.
* check ttyACM* can be accessed without being root & without setting udev rules
* check installation & operation on Win, MacOS, 
* check with USB hub https://github.com/hathach/tinyusb/discussions/1248
* check max really working cable length 
* auto-generate VERSION on each compilation? 
* resources to "assimilate"
    https://github.com/dorsic/PicoPET
    https://github.com/dorsic/PicoDIV
* HW to support?
    GY-BMP280-3.3


TODO Check installer istructions:
    1. On Linux, it should be already there.
    1. On Windows, the simplest way seems to get Python from the store. Or you can download [some recent release](https://www.python.org/downloads/windows/).
    1. On Mac, the [installation](https://realpython.com/installing-python/) should be easy, too.


## Set up

You can proceed to the examples below.


## First steps with Python  interface - OBSOLETE

Download the rp2daq.py module and save it to the folder where your project will reside. 

Create a new file with the following content:

```python
import rp2daq                            # wrapper around the low-level binary communication
rp2daq.init_error_msgbox()                # facilitates reading of possible error message

rp2 = rp2daq.Rp2daq()                    # initialize communication (with the RP2DAQ device found)

response = rp2.identify()                # ask the device for a short message  +  make its LED blink
print(response)                    

import tkinter                            
tkinter.messagebox.showinfo(response)    # show the message in a clickable window
```

Run this code. (If you are not familiar with the python scripting, find some nice tutorial online.) 

If everything is OK, a message box should appear showing the 30-byte identification string of your device. 

NOT FINISHED YET: You can proceed to the *Quick overview of software features*.  If there is an error, check troubleshooting.

