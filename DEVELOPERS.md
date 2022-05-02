# Extending and compiling the firmware 

In most expected use cases, pre-compiled RP2DAQ firmware only needs to be downloaded and flashed once, as described in the [main README document](README.md).

But in more specific applications, where a new functionality, some communication protocol or tight timing is required, you may want to fork this project and modify the C firmware to your needs. All necessary information should be summed up in this document and in the corresponding C code.

We will appreciate your contributions if you decide to share them back. You can first discuss your needs and plans on the issues page.

RP2DAQ can also serve as a convenient "boilerplate" for your own projects. We try to keep the code base readable and reasonably short.


## Re-compiling the firmware

The firmware is written in C language and uses [Raspberry Pi Pico SDK](https://raspberrypi.github.io/pico-sdk-doxygen/) (which was chosen instead of the Arduino ecosystem, but some libraries from the latter [can be ported](https://www.hackster.io/fhdm-dev/use-arduino-libraries-with-the-rasperry-pi-pico-c-c-sdk-eff55c)). 

If you can already compile and upload [the official blinking LED example](https://www.raspberrypi.com/news/how-to-blink-an-led-with-raspberry-pi-pico-in-c/), doing the same with RP2DAQ should be straightforward. The following procedure is therefore mostly for convenience. 

I use Linux for primary development; development of firmware on other OS is not covered here yet.

#### Prerequisities (on Linux)

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

#### Code structure and concepts

RP2DAQ aims to squeeze maximum power from raspberry pi, without putting programming burden on the user. To this end, it employs 
    * **parallelism in the firmware** (1st CPU core mostly for communication and immediate commands, 2nd core for real-time control; DMA use, planned PIO use),
    * Pi Pico is by default **overclocked from 133 MHz to 250 MHz**,
    * **Asynchronous operation**, i.e. multiple commands can be issued in one moment, and the corresponding reports are handled in computer later when they arrive. However, such high-performance behaviour is somewhat harder to learn, so it is kept optional. The default behaviour for all commands is to block Python code until the corresponding report is received. 

Each instance of Rp2daq class connects to a single board, but you may initialize more instances easily. Doing so, use the "required_device_tag=" parameter to tell devices apart.

* Avoiding defining new interfaces and constants where not necessary: The aim is not to build a wrapper for pico-SDK, but simply make its most useful features quickly accessible to one python script in the computer. The [pico-SDK docs](https://raspberrypi.github.io/pico-sdk-doxygen/) are still a good resource for details.

#### Communication and messaging

Rp2daq implements a custom binary communication protocol for messages in both directions. Henceforth we will call all messages going from computer "commands" and all messages going back as "reports".

The device part, written in C code, holds all the details of the protocol. In contrast, on the computer side, the communication interface will be dynamically *auto-generated* at each startup by parsing the firmware code. While this solution may appear unusual, it saves the developer from writing somewhat redundant messaging interface in Python. More importantly, it also prevents them from making possibly hard-to-debug mistakes in protocol mismatch. 


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

