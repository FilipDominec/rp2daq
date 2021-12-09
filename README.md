## RP2DAQ - Raspberry Pi Pico for Data Acquisition (and much more)

*Making the Raspberry Pi Pico module a universal $5 peripheral for data acquisition and generic laboratory automation.*

### Rationale:
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
    * [ ] second core for time-critical tasks
    * [ ] o/c @250 MHz

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

 1. Download the pre-compiled firmware: [rp2daq.ino.uf2](./rp2daq.ino.uf2) 
 1. Hold the ```BOOTSEL``` switch on your Raspberry and connect it with micro-USB cable to your computer.
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
	 * identification message
	 * stepper motor (using Stepstick - A4988) with end-stop support
	 * digital pin input/output
	 * voltage measurement (internal 12-bit 500kSps ADC) with oversampling and burst capability
	 * pulse width modulation (built-in PWM in RP2)
 * planned
	* medium-speed external ADCs (e.g. [AD7685](https://www.analog.com/en/products/ad7685.html#product-overview) through I2C/SPI, 16-bit, 250 kSps)
        * burst mode with optional programmable delay
        * optional synchronized ICG/φM/SH driving signals for [TCD1304](https://pdf1.alldatasheet.com/datasheet-pdf/view/32197/TOSHIBA/TCD1304AP.html) (linear charge-coupled light sensor)
	* medium-speed external DACs (through I2S, e.g. [TDA1543](http://www.docethifi.com/TDA1543_.PDF) dual 192kHz 16-bit DACs; multichannel I2S protocol implemented in software)
	* user-defined data storage in flash memory unused by firmware 
 * considered, not planned in near future
	* built-in ramps and feedback loops for autonomous scanning (e.g. PID regulation, digital control of scanning tunneling microscope etc.)
	* high-speed external ADC (i.e. oscilloscope, using built-in PIO 8-Bit 100MSps, e.g. AD9288)
    * high-speed external DAC (i.e. direct digital synthesis, AD9708ARZ?)
	* lock-in detection of synchronous weak signal
	* I2C, RS232 and GPIB interfaces
	* *obsolete and removed:* PCM56P (bipolar 16bit DAC)

### Potentially useful tips for additional electronics

 * planned
	 * notes on using photodiodes
	 * passive smoothing of PWM signal to get (slow) analog output
	 * high-efficiency voltage-controlled power supply (0-30 V with LM2596)
	 * ultra-low current measurement with (custom logarithmic pre-amp with 10fA-10μA range)
	 * suggested modular design for modules

## Detailed technical information 
### Generally useful messages implemented:
 * CMD_MOVE_SYMBOL 1  // moves the stepper
	nanospeed = 1 leads to  10000/16/256 = 2.44 steps per seconds, this is the minimum speed that can be directly set
 * CMD_GET_STEPPER_STATUS 3		// just report the current nanopos and status

### Mission-specific messages implemented:

 * CMD_APPROACH 2		// safely approach the sample to the STM tip, using both stepper and piezo
 * CMD_GET_STM_STATUS 4		// just report the current nanopos and status
 * CMD_SET_PIEZO  9		// set a concrete position on the piezo
 * CMD_LINESCAN 10

 * CMD_SET_PWM 20
 * CMD_INIT_PWM 21 

### Extending the firmware capabilities

In most simple cases, the firmware can do the job as is. But in more specific applications, where a new functionality, some communication protocol or tight timing is required, you may want to fork this project and modify the firmware to your needs. 

Developing firmware for Raspberry Pi is documented on many websites online. In short, the procedure we use for this project is this:

 1. connect the rp2 to your computer using USB
 1. [download and unpack Arduino studio](https://www.tecmint.com/install-arduino-ide-on-linux/) and run it,
 1. set up Arduino studio compile for RP2: 
    1. add the ``` ??? ``` link to the menu *File* -> *Preferences* -> Additional Boards Manager 
	1. in menu: *Tools* -> *Board*, click *Boards manager*, search for ```pico```, select "Raspberry Pi pico/RP2040 by E A Philhower" ... and click *Install* (it takes up some 430 MB)
	1. select *Tools* -> *Board -> *Raspberry Pi RP2040 Boards* -> *Raspberry Pi pico* (...the first one)
	1. select *Tools* -> *Menu* -> *USB Stack* -> *Adafruit TinyUSB*
	1. select *Tools* -> *Port*, select the appropriate port 
    1. other default options seem to be OK
 1. open and upload ```rp2daq.ino``` into the module, 
 1. adapt one of the examples in Python 
 1. if the Python script fails to find the COM* port on Windows
		

Ctrl+U to re-upload 



## Practical design tips
### ADC, noise and accuracy

<table> <tr>
<th> Hardware setup </th>
<th> Python script </th>
</tr> 

<tr> 
<td>
TBA
</td>

<td>
```python
#HISTOGRAM FOR NOISE ANALYSIS
histx, histy # [], [] 
for x in range(1700, 2050):
    histx.append(x)
    histy.append(np.count_nonzero(vals##x))
np.savetxt(f'histogram_{sys.argv[1] if len(sys.argv)>1 else "default"}.dat', np.vstack([histx,histy]).T)
print(f'time to process: {time.time()-t0}s'); t0 # time.time()
#
plt.hist(vals, bins#int(len(vals)**.5)+5, alpha#.5)
plt.plot(histx,histy, marker#'o')
print(np.sum(vals**2)/np.mean(vals)**2, np.sum(vals/np.mean(vals))**2, np.sum(vals**2)/np.mean(vals)**2-np.sum(vals/np.mean(vals))**2)
plt.show()
```
</td>

</tr>
</table>

### Stepper control end switches

Usually, stepper motors should have some end switch; on power-up, they go towards it until their position gets calibrated. 

Mechanical switches are fine in most cases. Upon initialization, you can freely chose an unused pin on RP2 to be the ```endswitch```. Connect it so that the switch shorts this pin to ground at end position.

Alternately, I suggest Omron EE-SX1041 for the optical end switch. They can be connected by 3 wires, and complemented by two resistors to behave analogous to a mechanical end switch
(i.e. to drop voltage at end stop): 

 * +3.3V (red wire) on "Collector" pin, 
 * ground (green wire) on "Kathode" pin close to the "1041" marking, 
 * sensing (yellow wire) on "Emitter" pin, 
 * 100ohm current-limiting resistor diagonally from "Collector" to "Anode"
 * 1k5 pull-up resistor diagonally from "Emitter" to "Kathode"

## PAQ - Presumably Asked Questions

**Q: Are there projects with similar scope?**

A: [Telemetrix](https://github.com/MrYsLab/Telemetrix4RpiPico) also uses RP2 as a device controlled from Python script in computer. Rp2daq aims for high performance laboratory automation. 

**Q: Should I use rp2daq or MicroPython?**

A: With [MicroPython]() or CircuitPython, one can write Python code that is run directly in RP2. But the performance of compiled C is substantially better than Python interpreter, particularly for branching pieces of code needed e.g. for stepper control. Also with rp2daq the user has to write only one script (in computer), instead of two that moreover need to communicate reliably.

**Q: Why are no displays or user interaction devices supported?**

A: Computer has much better display and user interaction interface. Rp2daq takes care for the hardware interaction that computer cannot do. 

## Legal

The firmware and software are released under the MIT license. 

They are free as speech after drinking five beers, that is, with no warranty of usefulness or reliability. If anybody uses rp2daq for industrial process control and it fails, I am sorry.





