# RP2DAQ - Raspberry Pi Pico for Data Acquisition (and much more)

*Making the Raspberry Pi Pico module a universal $5 peripheral for data acquisition and generic laboratory automation.*

## Rationale:
There are numerous projects online where computer (Python script) uses a Raspberry Pi Pico (with C firmware) as an interface to measure voltage like an oscilloscope, monitor temperature and humidity sensors, make stepping motors or servos turn etc. However, adapting such projects for practical tasks may still require a lot of programming experience and effort. 

This project aims to provide one pre-compiled firmware, the behaviour of which is defined from a Python script in the computer. This makes its deployment as easy as possible, still the full speed of C-compiled routines is maintained.

**Work under progress. Not ready enough to be recommended for practical use yet. I need this for my work, too, so stay tuned.**

## Work under progress - ROADMAP

 * Basic features: 
    * [ ] async message communication
    * [ ] fresh rewritten stepper control ⚠️ never disconnect steppers when powered
    * [ ] analog pin direct read
	* [ ] digital pin input/output
 * Documentation:
    * [ ] No programming: setting up hardware and first tests
    * [ ] Python programming: basic concepts and examples
    * [ ] C programming: extending rp2daq's capabilities
    * [ ] Presumably asked questions
 * Advanced features
    * [x] second core for time-critical tasks
    * [x] o/c @250 MHz

## Getting it work

### Hardware you will need
The bare minimum to start is 

 * Raspberry Pi Pico ($5),
 * USB micro cable ($3),
 * a computer with with Python3 and ```python-pyserial``` package installed.
	* On Windows, [get anaconda](https://docs.anaconda.com/anaconda/install/windows/) if unsure.
	* On Linux, Python3 should already be there

### Uploading the firmware to Raspberry once

Follow the classical procedure:

 1. Download the pre-compiled firmware: [build/rp2daq.uf2](build/rp2daq.uf2) 
 1. Hold the ```BOOTSEL``` switch on your Raspberry Pi Pico and connect it with micro-USB cable to your computer.
 1. A new USB storage medium should appear, containing INDEX.HTM and INFO_UF2.TXT. Release the ```BOOTSEL``` switch.
 1. Copy rp2daq.ino.uf2 to the medium.

After few seconds, the USB storage should disappear. Your RP2 becomes accessible for any program as a new COM/ttyACM port.  Let's try it.

### Simplest test of rp2daq

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


## Quick overview of software features

 * implemented & tested 
 * under development
	 * [ ] identification message
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
	* [ ] user-defined data storage in flash memory unused by firmware 
	* [ ] high-speed external ADC (i.e. oscilloscope, using built-in PIO 8-Bit 100MSps, e.g. AD9288-100)
 * considered, not planned in near future
    * [ ] fast & autonomous pipe-lining infrastructure (all tasks can actually be some pipe elements) 
        * pipeline sources: USB message received, task finished, periodic timer, digital pin trigger, numeric ramp generator
        * pipeline operators: moving average, pairwise lock-in detection of interlaced signals, Kálmán filter, PID regulator, boxcar, multichannel pulse histogram, ...
        * pipeline ends: USB data transmit, setting PWM, transmit I2S 
    * [ ] high-speed external DAC (+ support for direct digital synthesis, AD9708ARZ?)
	* [ ] SCCB support for cameras like OV2640


### Extending and compiling the firmware

While the basic use of RP2DAQ is to upload the [pre-compiled firmware](build/rp2daq.uf2) once and then write Python scripts only, it can be modified if needed.
* We will appreciate your contributions if you decide to share them back. You can first discuss your needs and plans on the issues page.
* RP2DAQ can also serve as a convenient "boilerplate" for your own projects. We try to keep the code base readable and reasonably short.

More information on compilation procedures and code structure is in the [developers page](DEVELOPERS.md).

## Resources used

* Concepts and parts of code
    * https://github.com/MrYsLab/Telemetrix4RpiPico
    * https://docs.tinyusb.org/en/latest/reference/index.html
* Official documentation & examples
    * https://www.raspberrypi.com/documentation/microcontrollers/rp2040.html
    * https://raspberrypi.github.io/pico-sdk-doxygen/index.html
    * https://github.com/raspberrypi/pico-examples
* Other useful articles online
    * https://gregchadwick.co.uk/blog/playing-with-the-pico-pt2/

## PAQ - Presumably Asked Questions

**Q: Are there projects with similar scope?**

A: [Telemetrix](https://github.com/MrYsLab/Telemetrix4RpiPico) also uses RP2 as a device controlled from Python script in computer. RP2DAQ aims for higher performance in laboratory automation. Parts of RP2DAQ code was thankfully "borrowed" from Telemetrix.

**Q: How does RP2DAQ differ from writing MicroPython scripts directly on RP2?**

A: Fundamentally, but use cases may overlap. [MicroPython](https://github.com/micropython/micropython) (and [CircuitPython](https://circuitpython.org/)) interpret Python code directly on a microcontroller (including RP2), so they are are good choice for a stand-alone device (if speed of code execution is not critical, which may be better addressed by custom C firmware). There are many libraries that facilitate development in MicroPython. 

In contrast, RP2DAQ assumes the microcontroller is constantly connected to computer via USB; then the precompiled firmware efficiently handles all actions and communication, so that you only need to write one Python script for your computer. 

**Q: Is the use of RP2DAQ limited to Raspberry Pi Pico, or can it be transferred on other boards with RP2040 chip?**

A: Not tested. Most, if not all, of the functionality should be available, but the pin definitions would probably change. 

**Q: Does RP2DAQ implement all functions available by the Raspberry Pico SDK?**

A: By far not and it is not even its scope. RP2DAQ's features make a higher layer above (a subset) of the SDK functions.


**Q: Does RP2DAQ help communicating with scientific instruments, e.g. connected over GPIB/VISA?**

A: This is outside of RP2DAQ's scope, but [over 40 other projects](https://github.com/python-data-acquisition/meta/issues/14) provide Python interfaces for instrumentation and they can be imported into your scripts independently. While RP2DAQ does not aim to provide such interfaces, capabilities of RP2 could substitute some commercial instruments in less demanding use cases. 

**Q: Why are no displays or user interaction devices supported?**

A: The Python script in your computer has a very good display and user interaction interface. RP2DAQ only takes care for the hardware interaction that computer cannot do. 

**Q: Can RP2DAQ control unipolar stepper motors using ULN2003?**

A: No. Both bipolar and unipolar steppers seem to be supported by stepstick/A4988 modules, with better accuracy and efficiency than provided by ULN2003. 

## Legal

The firmware and software are released under the MIT license. 

They are free as speech after drinking five beers, that is, with no warranty of usefulness or reliability. RP2DAQ cannot be recommended for industrial process control.



