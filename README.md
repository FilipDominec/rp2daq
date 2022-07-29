# RP2DAQ - Raspberry Pi Pico for Data Acquisition (and much more)

Raspberry Pi Pico is a small, but quite powerful microcontroller board. When connected to a computer over USB, it can serve as an interface to hardware - which may be as simple as a digital thermometer, or as complicated as scientific experiments tend to be. 

This project presents both precompiled firmware and a user-friendly Python module to control it. The firmware takes care of all technicalities at the microcontroller side including parallel task handling and reliable communication, and is optimized to harness Raspberry Pi's maximum performance. All actions of RP2DAQ are triggered by the Python script in the computer. This saves the user from programming in C and from error-prone hardware debugging. Even without any programming, one can try out few supplied *Example programs*. 

If needed, entirely new capabilities can be added into the [open source](LICENSE) firmware. More is covered in the [developer documentation for the C firmware](docs/DEVELOPERS.md). Contributing new code back is welcome. 

**Project status: basic work done, real-world testing underway **

 * Features implemented and planned: 
    * [x] built-in ADC (continuous 12-bit measurement, at 500k samples per second)
    * [x] stepper motors (pulse control for up to 12 "stepstick" drivers simultaneously)
	* [x] digital pin output
	* [x] digital pin input (direct, or call-back on change)
	* [x] pulse-width modulation (up to 16 PWM channels)
	* [ ] pulse frequency and timing measurement
	* [ ] digital messaging (USART/I2C/I2S/SPI) for sensors 
	* [ ] high-speed digital acquisition (i.e. 100 MSPS oscilloscope using AD9288)
 * Highlights:
	* All features can be activated in parallel without interference.
	* Friendly (i)Python interface - all commands return human-readable dicts with more or less useful data.
	* Every command can be either synchronous (easier to program) or asynchronous (allowing actions to run in parallel).
	* RP2DAQ [is](docs/LICENSE) free software.
	* Tested in practice. No animals nor humans were harmed.
 * Documentation:
    * [x] No programming: setting up hardware and first tests
    * [ ] Python programming: basic concepts and examples
    * [ ] C programming: extending rp2daq's capabilities
    * [ ] Presumably asked questions

## Getting it work (without any programming)

#### What you will need

 * Raspberry Pi Pico ($5),
 * USB micro cable ($3),
 * a computer with [Python (3.8+)](https://realpython.com/installing-python/) and ```python-pyserial``` package installed.
	* On Windows, [get anaconda](https://docs.anaconda.com/anaconda/install/windows/) if unsure.
	* On Linux, Python3 should already be there
    * On Mac, it should be there though [version update](https://code2care.org/pages/set-python-as-default-version-macos) may be needed

#### Uploading the firmware to Raspberry (once)

1. [Download](https://github.com/FilipDominec/rp2daq/archive/refs/heads/main.zip) and unzip this project. 
    * (If preferred, one can also use ```git clone https://github.com/FilipDominec/rp2daq.git```)
1. Holding the white "BOOTSEL" button on Raspberry Pi Pico, connect it to your computer with the USB cable. Release the ```BOOTSEL``` switch.
    * In few seconds it should register as a new flash drive, containing INDEX.HTM and INFO_UF2.TXT. 
1. Copy the ```build/rp2daq.uf2``` file to RP2. 
    * *The flashdrive should disconnect immediately.* 
    * *The green diode on RP2 should blink twice, indicating the firmware is running and awaiting commands.*
After few seconds, the USB storage should disconnect. Your RP2 becomes accessible for any program as a new COM/ttyACM port.  Let's try it.

#### hello_world.py

Launch the ```hello_world.py``` script in the main project folder. 
* If an rp2daq device is available, a window like the one depicted on left should appear; you can interactively control the onboard LED with the buttons.  
* If an error message appears (like depicted right), the device does not respond correctly. Check it blinks twice when USB is re-connected, or make sure you uploaded fresh firmware. 
* If no window appears, there is some trouble with your Python installation.


![](docs/hello_world_screens.png)


## Programming concepts

To check everything is ready, run in your Python3 interpreter:

```Python
import rp2daq
rp = rp2daq.Rp2daq()
rp.pin_out(25, 1)
```

The pin number 25 is connected to the green onboard LED - it should turn on.

Or similarly, you can get ADC readout. With default configuration, it will spit out 1000 numbers sampled from the pin 26:

```Python
import rp2daq
rp = rp2daq.Rp2daq()
print(rp.internal_adc())
```


## PAQ - Presumably Asked Questions

**Q: How does RP2DAQ differ from writing MicroPython scripts directly on RP2?**

A: They are two fundamentally different paths that may lead to similar results. [MicroPython](https://github.com/micropython/micropython) (and [CircuitPython](https://circuitpython.org/)) interpret Python code directly on a microcontroller (including RP2), so they are are good choice for a stand-alone device (if speed of code execution is not critical, which may be better addressed by custom C firmware). There are many libraries that facilitate development in MicroPython. 

In contrast, RP2DAQ assumes the microcontroller is constantly connected to computer via USB; then the precompiled firmware efficiently handles all actions and communication, so that you only need to write one Python script for your computer. 


**Q: Is the use of RP2DAQ limited to Raspberry Pi Pico board?**

A: Very likely it can be directly uploaded on other boards featuring the RP2040 microcontroller, e.g. RP2040-zero, but this has not been tested yet. 

Development of this project was started on the ESP32-WROOM module, but it suffered from its randomly failing (and consistently slow) USB communication, as well as somewhat lacking documentation.


**Q: Can RP2DAQ be controlled from other language than Python 3.8+?**

A: Perhaps, but - the firmware and computer communicate over a binary interface that would have to be ported to this language. One of the advantages of RP2DAQ is that the interface on the computer side is autogenerated; the corresponding C-code parser would have to be rewritten. Hard-coding the messages in another language would be a quicker option, but it would be bound to a single firmware version. Python is fine.


**Q: Are there projects with similar scope?**

A: [Telemetrix](https://github.com/MrYsLab/Telemetrix4RpiPico) also uses RP2 as a device controlled from Python script in computer. RP2DAQ aims for higher performance and broader range of capabilities. However, parts of RP2DAQ code and concepts were inspired by Telemetrix.


**Q: Does RP2DAQ implement all functions available by the Raspberry Pico SDK?**

A: By far not - and it is not even its scope. RP2DAQ's features make a higher layer above (a subset) of the SDK functions. But it is intended to cover most RP2040's features in the future.


**Q: Does RP2DAQ help communicating with scientific instruments, e.g. connected over GPIB/VISA?**

A: Interfacing to instruments is outside of RP2DAQ's scope, but [over 40 other projects](https://github.com/python-data-acquisition/meta/issues/14) provide Python interfaces for instrumentation and they can be imported into your scripts independently. While RP2DAQ does not aim to provide such interfaces, capabilities of RP2 could substitute some commercial instruments in less demanding use cases. 


**Q: Why are no displays or user interaction devices supported?**

A: The Python script has a much better display and user interaction interface - that is, your computer. RP2DAQ only takes care for the hardware interaction that computer cannot do. 


**Q: Can RP2DAQ control unipolar stepper motors using ULN2003?**

A: No. Both bipolar and unipolar steppers seem to be supported by stepstick/A4988 modules, with better accuracy and efficiency than provided by ULN2003. 


## Legal

The firmware and software are released under the [MIT license](LICENSE). 

They are free as speech after drinking five beers, that is, with no warranty of usefulness or reliability. RP2DAQ cannot be recommended for industrial process control.



