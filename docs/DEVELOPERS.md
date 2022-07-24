# Developer information

## Don't write C code, unless you are sure you want to

In most expected use cases, pre-compiled RP2DAQ firmware only needs to be downloaded and flashed once, as described in the [main README document](README.md). All its functions will then be activated at runtime by the Python script in your computer.

But in more specific applications, where entirely new functionality with tight timing, or some new communication protocol is required, you may want to fork this project and modify the C firmware to your needs. All necessary information should be summed up in this document and in the corresponding C code.

We will appreciate your contributions if you decide to share them back. You can first discuss your needs and plans on the [issues page](https://github.com/FilipDominec/rp2daq/issues).

RP2DAQ can also serve as a convenient "boilerplate" for your own projects. We try to keep the code base readable and reasonably short.



## Re-compiling the firmware

The firmware is written in C language and uses [Raspberry Pi Pico SDK](https://raspberrypi.github.io/pico-sdk-doxygen/) (which was chosen instead of the Arduino ecosystem, but some libraries from the latter [can be ported](https://www.hackster.io/fhdm-dev/use-arduino-libraries-with-the-rasperry-pi-pico-c-c-sdk-eff55c)). 

If you can already compile and upload [the official blinking LED example](https://www.raspberrypi.com/news/how-to-blink-an-led-with-raspberry-pi-pico-in-c/), doing the same with RP2DAQ should be straightforward. The following procedure is therefore mostly for convenience. 

I use Linux for primary development; development of firmware on other OS is not covered here yet.

#### Development dependencies (on Linux)

The official approach (TODO test afresh):

    wget -O pico_setup.sh https://rptl.io/pico-setup-script
    chmod +x pico_setup.sh
    ./pico_setup.sh

#### Compilation procedure (Linux version)

First compilation (or re-compilation if Cmake options changed):

    rm -r build/ 
    cmake -B build -S . 
    pushd build 
    make
    popd

A new ```build/rp2daq.uf2``` file should appear. It can be uploaded by drag&drop as described in [README.md], or a following trick can be used that saves a bit of clicking.

#### Flash upload without manual bootsel/reset (on Linux)

As a first step, [one can switch](https://gist.github.com/tjvr/3c406bddfe9ae0a3860a3a5e6b381a93) udev rules so that *picotool* works without root privileges:

    echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="0003", MODE="0666"' | sudo tee /etc/udev/rules.d/99-pico.rules


The procedure with [pressing the *bootsel* button](https://gist.github.com/Hermann-SW/ca07f46b7f9456de41f0956d81de01a7) and resetting RP2 can be entirely handled by software:

    pushd build ; stty -F /dev/ttyACM0 1200; make rp2daq && ~/bin/picotool load *.uf2 && ~/bin/picotool reboot; popd



## Under the hood

#### Parallelism

RP2DAQ aims to squeeze maximum power from raspberry pi, without putting programming burden on the user. To this end, it employs 
    * the board is by default **overclocked from 133 MHz to 250 MHz**,
    * **parallel use of hardware**: 1st CPU core mostly for communication and immediate command handling, 2nd core for real-time control; other hardware features like ADC+DMA and PWM operate independent the CPU cores),
    * **parallelism in the firmware**, thanks to which commands of different types can be run concurrently without interfering with each other, 
    * **optional asynchronous programming**, i.e. multiple commands can be issued in one moment, and the corresponding reports are handled in computer later when they arrive. However, such high-performance behaviour is somewhat harder to learn, so it is kept optional. The default behaviour for all commands is to block Python code until the corresponding report is received. 

Each instance of Rp2daq class in Python connects to a single board. In demanding applications, one use several boards simultaneously; this simply requires to initialize more Rp2daq instances. Initialization routine has the optional ```required_device_tag=``` parameter to tell devices apart.


#### Code structure and concepts

RP2DAQ had to define its own Python interface, which is supposed to be as friendly and terse as possible. It should enable one to design useful experiments with literally a dozen of Python code lines.

Simultaneously, the firmware is built atop on the [Pi Pico C SDK](https://github.com/raspberrypi/pico-sdk) and it does not obscure this fact. Instead it adheres to its logic where possible. The aim is not to build a new wrapper for the SDK, but simply make its most useful features quickly accessible to the python script in the computer. The [pico-SDK reference](https://raspberrypi.github.io/pico-sdk-doxygen/) and [RP2040 datasheet](https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf) are still good resources for advanced use of RP2DAQ.


#### Communication and messaging

Rp2daq implements its own binary communication protocol for efficient data transfer in both directions. Henceforth we will call all messages going from computer "commands" and all messages going back "reports". To every type of command, one type of report is assigned. 

Calling one command from computer will result in least one report being received, either immediately (e.g. device ID would be reported within a millisecond) or delayed (e.g. if motor movement is to be finished first). Some commands may result in several or even unlimited number of reports (e.g. continuous ADC measurement), but in no case a command passes without *any* report coming in future. The reason for this rule is this: When any command is called in Python without callback explicitly specified, the command is *synchronous*, that is, the script waits until the first report of the corresponding type is received. Not receiving a report would thus lead to the script halting indefinitely.

The ```rp2daq.py``` is a thin wrapper to the commands and reports, and since it is dynamically generated at runtime, its code contains no information about them. Commands and reports are fully defined once in the C code, namely in the ```include/``` directory, and listed in the ```message_table``` (in ```rp2daq.c```). 
The exact format for a command is defined in the ```args``` structure within a command handler, the format of corresponding report is a struct simply named as ```..._report```. During compilation, the message formats are hard-coded in the firmware. In contrast, on the computer side, the Python communication interface will be dynamically *auto-generated* at each startup by Python parsing the C firmware code. While this solution may appear unusual, it is very practical, completely avoiding redundant definitions between C and Python. Not only this saves programmer's time, but it also elegantly prevents possible hard-to-debug errors from protocol mismatch. 

In the device, all reports are *scheduled* into a cyclic buffer of buffers. This approach is thread-safe even if a report is to be issued from *core1* while 1st *core0* is busy transmitting. It also does not block execution of time-critical code. 

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

