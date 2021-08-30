## RP2DAQ - Raspberry Pi Pico for Data Acquisition (and much more)

*Making the Raspberry Pi Pico module a universal $5 peripheral for data acquisition and generic laboratory automation.*

### Rationale:
I have finished several projects where computer (Python script) used a microcontroller (C firmware) as an interface to make stepping motors turn, ADC measure voltage, PWM move servos etc. Then I realized that most of the firmware is reused among these projects. So I merged all relevant firmware functions into this *rp2daq* package. 

Once flashed with the firmware supplied below, Raspberry Pi Pico (usually) requires no further firmware compilation nor modification. Its behaviour will be fully defined by the Python script in the computer. This makes deployment of a new experiment as easy as possible.

This document also gives several suggestions on electrical design, see below.

**Work under progress. Not ready enough to be recommended.**

When finished: Primarily intended for my own projects at work, but may be useful for others as a starter whenever computer control of electronics is needed. Feel free to fork it, but no warranty is provided.


 * Merging my projects: **not finished** yet,
 * Basic features: implemented,
 * Documentation: missing (except for comments in code)

## Getting it work

### Hardware you will need
The bare minimum to start is 

 * Raspberry Pi Pico ($5),
 * USB micro cable ($3),
 * a computer with with Python3 installed.
	* On Windows, [get anaconda](https://docs.anaconda.com/anaconda/install/windows/) if unsure.
	* On Linux, Python3 should be pre-installed; make sure you also have the ```python-pyserial``` package.

### Uploading the firmware to Raspberry

This classical procedure has to be done only once (unless you decided to extend the firmware capabilities, see the *Development* section below).

 1. Download the pre-compiled firmware: [rp2daq.ino.uf2](./rp2daq.ino.uf2) 
 1. Hold the ```BOOTSEL``` switch on your Raspberry and connect it with micro-USB cable to your computer.
 1. A new USB storage medium should appear. Release the ```BOOTSEL``` switch.
 1. Copy rp2daq.ino.uf2 to the medium, next to INDEX.HTM and INFO_UF2.TXT.

After few seconds, the USB storage should disappear, and your new rp2daq module becomes accessible as a COM/ttyACM.  Let's try it.

### Simplest test of rp2daq

Download the rp2daq.py module and save it to the folder where your project will reside. 

Create a new file with the following content:

```python
import rp2daq							# wrapper around the low-level binary communication
rp2daq.init_error_msgbox()				# facilitates reading of possible error message

rp2 = rp2daq.Rp2daq()					# initialize communication (with the RP2DAQ device found)

response = rp2.identify()				# ask the device for a short message  +  make its LED blink
print(response)					

import tkinter							
tkinter.messagebox.showinfo(response)	# show the message in a clickable window
```

Run this code. (If you are not familiar with the python scripting, find some nice tutorial online.) 

If everything is OK, a message box should appear showing the 30-byte identification string of your device. 

NOT FINISHED YET: You can proceed to the *Quick overview of software features*.  If there is an error, check troubleshooting.

### Troubleshooting

 * ImportError: No module named serial
	* get the ```python-pyserial``` module
 * Script reports device not found
	* Does the LED shortly blink after connected to computer? If not, flashing went wrong (repeat it). Can there be cable problem (replace it).
	* Can you see a new device with VID:PID = "2e8a:0005 MicroPython Board in FS mode" appear when you connect it?
	* TODO elaborate solutions
 * Rp2daq device is found and responds to the ```identify()``` command, but some other behaviour is weird
	* This is probably error in the firmware, feel free to open an issue, copying full working copy of the troublesome code.
 * Stepper motor randomly "forgets" its position and behaves as if its end switch has triggered
	* There is too much noise in the end switch cable. Try shielding it, or add a 10nF parallel capacitance to the switch.  


## Quick overview of software features

 * implemented & tested
	 * identification message
	 * stepper motor (using Stepstick - A4988) with end-stop support
 * under development
	 * digital pin input/output
	 * voltage measurement (internal 12-bit 500kSps ADC) with oversampling and burst capability
	 * pulse width modulation (built-in PWM in RP2)
 * planned
	 * analog voltage output (precise external DAC with TDA1543 dual 192kHz 16-bit DACs; multichannel I2S protocol implemented in software)
	 * precise external ADC (TBA, 16-bit, 100s kSps)
	 * fast external ADC/DAC (using built-in PIO, e.g. AD9708ARZ? 8-Bit 100MSps)
	 * TCD1304 (linear charge-coupled light sensor)
	 * user-defined data storage in flash memory unused by firmware 
	 * digital control for scanning tunneling microscope
 * considered, not planned in near future
	 * PID regulation loop
	 * lock-in detection of synchronous weak signal
	 * frequency generator/counter using RP2's PIO (up to 125MHz crystal-controlled square wave)
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
### Stepper control

Usually, stepper motors should have some end switch; on power-up, they go towards it until their position gets calibrated. 

Mechanical switches are fine in most cases. Upon initialization, you can freely chose an unused pin on RP2 to be the ```endswitch```. Connect it so that the switch shorts this pin to ground at end position.

Alternately, I suggest Omron EE-SX1041 for the optical end switch. They can be connected by 3 wires, and complemented by two resistors to behave analogous to a mechanical end switch
(i.e. to drop voltage at end stop): 

 * +3.3V (red wire) on "Collector" pin, 
 * ground (green wire) on "Kathode" pin close to the "1041" marking, 
 * sensing (yellow wire) on "Emitter" pin, 
 * 100ohm current-limiting resistor diagonally from "Collector" to "Anode"
 * 1k5 pull-up resistor diagonally from "Emitter" to "Kathode"

## Legal

The firmware and software are released under the MIT license. 

That is, they are free as speech after drinking five beers, with no warranty of usefulness or reliability.  If anybody uses rp2daq for industrial process control and it fails, I am sorry.


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



