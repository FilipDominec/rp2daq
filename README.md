# RP2DAQ - Raspberry Pi Pico for Data Acquisition (and much more)

Raspberry Pi Pico is an inexpensive, yet relatively powerful microcontroller board. 

This project presents both precompiled firmware for this microcontroller and a python module to control it from a computer. Together they offer a solution to various automation tasks - which may be as simple as a digital thermometer, or as complicated as scientific experiments tend to be. 

 * digital pins input and output,
 * voltage measurements by internal ADC (12-bit 366kSPS sustained streaming),
 * stepper motor movement (driving multiple Stepstick boards synchronously),
 * pulse-width modulation (for analog output or servo control),
 * high frequency clock output (crystal-stabilized 250 MHz or its integer fractions),
and more - see *Detailed features* below.

The firmware takes care of all technicalities at the microcontroller side including parallel task handling and reliable communication, and is optimized to harness its maximum performance. Practical deployment of RP2DAQ therefore only requires basic Python skills, and saves the user from programming in C and error-prone hardware debugging. Entirely without programming, one can try out the supplied *Example programs*. 

However, if entirely new capabilities are needed, the project is [open source](LICENSE) and there is also [developer documentation for the C firmware](DEVELOPERS.md). Contributing new code back is welcome. 


## Status: Work under progress

**Work under progress. Not ready enough to be recommended for practical use yet.**

 * Basic features: 
    * [ ] async message communication
    * [ ] fresh rewritten stepper control
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

### What you will need

 * Raspberry Pi Pico ($5),
 * USB micro cable ($3),
 * a computer with with [Python (3.8+)](https://realpython.com/installing-python/) and ```python-pyserial``` package installed.
	* On Windows, [get anaconda](https://docs.anaconda.com/anaconda/install/windows/) if unsure.
	* On Linux, Python3 should already be there
    * On Mac, it should be there though [version update](https://code2care.org/pages/set-python-as-default-version-macos) may be needed

### Uploading the firmware to Raspberry once

1. [Download](https://github.com/FilipDominec/rp2daq/archive/refs/heads/main.zip) and unzip this project. 
    * (If preferred, you you can use ```git clone https://github.com/FilipDominec/rp2daq.git```)
1. Holding the white "BOOTSEL" button on Raspberry Pi Pico, connect it to your computer with the USB cable. Release the ```BOOTSEL``` switch.
    * In few seconds it should register as a new flash drive, containing INDEX.HTM and INFO_UF2.TXT. 
1. Copy the ```build/rp2daq.uf2``` file to RP2. 
    * *The flashdrive should disconnect immediately.* 
    * *The green diode on RP2 should blink twice, indicating the firmware is running and awaiting commands.*
After few seconds, the USB storage should disconnect. Your RP2 becomes accessible for any program as a new COM/ttyACM port.  Let's try it.

### Simplest test of rp2daq

TBA


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

The firmware and software are released under the [MIT license](LICENSE). 

They are free as speech after drinking five beers, that is, with no warranty of usefulness or reliability. RP2DAQ cannot be recommended for industrial process control.



