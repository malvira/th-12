Demonstration Firmware for the TH12
===================================

Getting the code
----------------

```
    git clone https://github.com/malvira/th-12.git
    cd th-12
    git submodule init
    git submodule update
```

Building the code
-----------------

### Get the Toolchain
   * [Linux/Windows](https://github.com/malvira/libmc1322x/wiki/toolchain)
   * [OSX](https://github.com/malvira/libmc1322x/wiki/mac) 

### Build

```
    cd th-12
    make
```

This will produce the binaries that get loaded on the TH-12 hardware.

Load on to TH12 Hardware
------------------------

Load and flash on to the TH12 using the PROG12 programming pod and the
tools in [libmc1322x](https://github.com/malvira/libmc1322x). The
PROG12 and tag-connect pinout uses the BBMC "redbee-econotag" layout.

```
    mc1322x-load -f coap-post-sleep_th12-lowpower.bin -t /dev/ttyUSB1 -c 'bbmc -l redbee-econotag erase'
```

Will load the main program in to RAM and execute it. To flash it so it persists over resets and power cycles:

```
    mc1322x-load -f flasher_m12.bin -s coap-post-sleep_th12-lowpower.bin -t /dev/ttyUSB1 -c 'bbmc -l redbee-econotag erase'
```

**PLEASE NOTE** __to flash the m12 you__ **MUST** __use the m12 build of__
   __flasher. This is in the m12 branch of libmc1322x. run make__
   __BOARD=m12__

Documentation
-------------

Please see the
[TH12 Wiki](https://github.com/malvira/th-12/wiki) for detailed documentation about how to use the **coap-post-sleep** firmware.

License
=======

This source code is released under the [same
license](https://github.com/contiki-os/contiki/blob/master/LICENSE)
that [Contiki OS](https://github.com/contiki-os/contiki) is released
as.
