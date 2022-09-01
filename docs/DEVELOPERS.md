# Developer information

## Don't write C code, unless you are sure you want to

In most expected use cases, pre-compiled rp2daq firmware only needs to be downloaded and flashed once, as described in the [main README document](README.md). All its functions will then be activated at runtime by the Python script in your computer.

But in more specific applications, where entirely new functionality with tight timing, or some new communication protocol is required, you may want to fork this project and modify the C firmware to your needs. All necessary information should be summed up in this document and in the corresponding C code.

We will appreciate your contributions if you decide to share them back. You can first discuss your needs and plans on the [issues page](https://github.com/FilipDominec/rp2daq/issues).

Rp2daq's messaging interface can also serve as a convenient "boilerplate" for your own projects. We try to keep the code base readable and reasonably short.



## Re-compiling the firmware on Debian/Ubuntu Linux

The firmware is written in C language and uses [Raspberry Pi Pico SDK](https://raspberrypi.github.io/pico-sdk-doxygen/) (which was chosen instead of the Arduino ecosystem, but some libraries from the latter [can be ported](https://www.hackster.io/fhdm-dev/use-arduino-libraries-with-the-rasperry-pi-pico-c-c-sdk-eff55c)). 

If you can already compile and upload [the official blinking LED example](https://www.raspberrypi.com/news/how-to-blink-an-led-with-raspberry-pi-pico-in-c/), doing the same with rp2daq should be straightforward. The following procedure is therefore mostly for convenience. 

Note that this procedure, with obvious minor changes, can be perused for uploading any other C-language project as well.

Linux is the primary development OS for rp2daq; development of firmware on other OS is not covered here yet.


#### Setup development dependencies (once)

Please note once again that compilation is *not* necessary to use rp2daq from Python. 
Only if you made some changes in the rp2daq firmware's, follow these steps. 

The `pico_setup.sh` script will help you to download and compile the *Standard Development Kit* (SDK) with dependencies, requiring only your password and few minutes:

```bash
    cd
    wget -O pico_setup.sh https://rptl.io/pico-setup-script
    export SKIP_VSCODE=1
    bash ./pico_setup.sh           # takes some 3 minutes
```


#### Fresh compilation (once)

Now go to the folder where you downloaded rp2daq, e.g.:

```bash
    cd ~/rp2daq/   # according to your choice
```

Fresh compilation (or re-compilation if Cmake options changed):

```bash
    rm -r build/ 
    cmake -B build -S .         # takes a minute
    pushd build 
    make
    popd
```

A new ```build/rp2daq.uf2``` file should appear. It can be uploaded by drag&drop as described in [README.md], or a following trick can be used that saves a bit of clicking.


#### Setting UDEV rules for user-space access (once)

Normally, the ```BOOTSEL``` button [has to be mechanically pressed](https://gist.github.com/Hermann-SW/ca07f46b7f9456de41f0956d81de01a7), and the device has to be restarted for a fresh upload. There is a trick to avoid it. 

As a first step, [it is preferable to switch](https://gist.github.com/tjvr/3c406bddfe9ae0a3860a3a5e6b381a93) udev rules so that `picotool` works without root privileges:

```bash
    echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="0003", MODE="0666"' | sudo tee /etc/udev/rules.d/99-pico.rules
```


#### Re-compilation & quick upload (routine)

Disconnect it & reconnect it the last time. From this point on, the upload procedure takes few seconds and can be entirely handled by software:

```bash
    pushd build ; stty -F /dev/ttyACM0 1200; sleep 0.5; make rp2daq && ~/pico/picotool/build/picotool load *.uf2 && ~/pico/picotool/build/picotool reboot; popd
```



## Under the hood

#### Parallelism

Rp2daq aims to squeeze maximum power from the RP2040 chip, without putting programming burden on the user. To this end,

 * the board is by default **overclocked from 133 MHz to 250 MHz**,
 * **hardware is used in parallel**: 1st CPU core mostly for communication and immediate command handling, 2nd core for real-time control; other hardware features like ADC+DMA and PWM operate independent the CPU cores),
 * **the firmware handles real-time tasks in non-blocking manner**, thanks to which commands of different types can be run concurrently without interfering with each other, 
 * and **also the Python API allows for optional asynchronous programming**, i.e. multiple commands can be issued in one moment, and the corresponding reports are handled in computer later when they arrive. However, such high-performance behaviour is somewhat harder to learn, so it is kept optional. The default behaviour for all commands is to block Python code until the corresponding report is received. 

Each instance of Rp2daq class in Python connects to a single board. In demanding applications, one use several boards simultaneously; this simply requires to initialize more Rp2daq instances. Initialization routine has the optional ```required_device_tag=``` parameter to tell devices apart.


#### Code structure and concepts

Rp2daq had to define its own Python interface, which is supposed to be as friendly and terse as possible. It should enable one to design useful experiments with literally a dozen of Python code lines.

Simultaneously, the firmware is built atop on the [Pi Pico C SDK](https://github.com/raspberrypi/pico-sdk) and it does not obscure this fact. Instead it adheres to its logic where possible. The aim is not to build a new wrapper for the SDK, but simply make its most useful features quickly accessible to the python script in the computer. The [pico-SDK reference](https://raspberrypi.github.io/pico-sdk-doxygen/) and [RP2040 datasheet](https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf) are still good resources for advanced use of rp2daq.


#### Communication and messaging

Rp2daq implements its own binary communication protocol for efficient data transfer in both directions. Henceforth we will call all messages going from computer "commands" and all messages going back "reports". To every type of command, one type of report is assigned. 

Calling one command from computer will result in least one report being received, either immediately (e.g. device ID would be reported within a millisecond) or delayed (e.g. if motor movement is to be finished first). Some commands may result in several or even unlimited number of reports (e.g. continuous ADC measurement), but in no case a command passes without *any* report coming in future. The reason for this rule is this: When any command is called in Python without callback explicitly specified, the command is *synchronous*, that is, the script waits until the first report of the corresponding type is received. Not receiving a report would thus lead to the script halting indefinitely.

The ```rp2daq.py``` is a wrapper to the commands and reports, and since it is dynamically generated at runtime, the Python code contains no information about them. Commands and reports are fully defined once in the C code, namely in the ```include/``` directory, and listed in the ```message_table``` (in ```rp2daq.c```). 

The exact format for a command is defined in the ```args``` structure within a command handler, the format of corresponding report is a struct simply named as ```..._report```. During compilation, the message formats are hard-coded in the firmware. In contrast, on the computer side, the Python communication interface will be dynamically *auto-generated* at each startup by Python parsing the C firmware code. While this solution may appear unusual, it is very practical, completely avoiding redundant definitions between C and Python. Not only this saves programmer's time, but it also elegantly prevents possible hard-to-debug errors from protocol mismatch. 

In the device, all reports are *scheduled* into a cyclic buffer of buffers. This approach is thread-safe even if a report is to be issued from *core1* while 1st *core0* is busy transmitting. It also does not block execution of time-critical code. 


#### Coding requirement for commands & reports

For actual implementation, please look into any of the ```include/*.c``` files. To add your own functionality, we suggest to copy some existing code.

There is no limitation for additional functions or comments, nor there is any limit for defining multiple commands within one C file. 

Since the C code has to be parsed by the `c_code_parser.py` to generate a matching Python interface at runtime, it has to follow following rules.

 1. Report struct `XYZ_report`:
    1. MUST be allocated at startup (i.e. it is not a mere *typedef*)
    1. SHOULD be consistently named
    1. MUST follow rp2daq's bit-packed convention (using ```__attribute__((packed))```)
    1. MUST have ```uint8_t report_code``` as its first field (leave it to be auto-filled at runtime)
    1. CAN contain two fields ```uint16_t _data_count``` and ```uint8_t _data_bitwidth``` at its very end, to transmit bulk 8/12/16bit payload
 1. Command handler function `XYZ()`:
    1. MUST be of type `void` and accepts no arguments
    1. SHOULD contain a multi-line comment block of "slash-asterisk" type, basically its docstring 
    1. MUST first allocate a packed struct, describing the command format
    1. CAN call ```tx_header_and_data(&XYZ_report, sizeof(XYZ_report), ...)```
       1. if this is not called, the report MUST be transmitted later from other function
 
#### Example of a simple command & report

The following code may be put into separate file like `include/XYZ.c`; in such a case, include this file also in `rp2daq.c`.

In any case, you would also need to add a new line into `message_table` in `rp2daq.c` (the format of which follows the existing entries therein).

```C
struct __attribute__((packed)) {    
    uint8_t report_code;
    uint32_t returned_value;
    uint16_t _data_count;
    uint8_t _data_bitwidth;
} XYZ_report;

void XYZ() {   
    /* One-line hint what the command does 
     * 
     * Optionally, a longer description of the command. Practical tips are welcome. This comment 
     * block gets converted into a docstring at runtime. Also it is used to generate 
     * docs/PYTHON_REFERENCE.md. You can use markdown. Please wrap at 100 characters.
     * 
     * __Between double underscores here, tell the users what report is to be expected. It can 
     * be immediate/delayed, single/multiple/infinite...__
     */ 
	struct  __attribute__((packed)) { 
        uint8_t input_value;    // min=0 max=1 default=1 A comment for the argument, to be parsed into docstring 
	} * args = (void*)(command_buffer+1);

    // Here comes your command code. It can transmit a corresponding report here, or the report
    // can be delayed and transmitted by a different function later. But note every command 
    // eventually has to send at least one report (or Python scripts would hang indefinitely).

    XYZ_report.returned_value = args->input_value * 42;

	tx_header_and_data(&XYZ_report, sizeof(XYZ_report), 0, 0, 0);
}
``` 


## Resources used

* Concepts and parts of code
    * https://github.com/MrYsLab/Telemetrix4RpiPico
    * https://docs.tinyusb.org/en/latest/reference/index.html
* Official documentation & examples
    * https://www.raspberrypi.com/documentation/microcontrollers/rp2040.html
    * https://raspberrypi.github.io/pico-sdk-doxygen/index.html
    * https://github.com/raspberrypi/pico-examples
* Other useful articles online (to be processed)
    * https://gregchadwick.co.uk/blog/playing-with-the-pico-pt2/
    * https://github.com/jamon/pi-pico-pio-quadrature-encoder rewrite with IRQ to keep PIO free

