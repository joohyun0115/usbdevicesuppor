# usbdevicesuppor
Most of Embedded Linux devices have USB client device. This project enables embedded developpers to use USB cable for Linux console and for Linux kernel debugging by KGDB. No more serial cable for Linux console and debug !

Juhyun Kyong, Gu-Min Jeong, Minsuk Lee, Chanik Park, and Sung-Soo Lim, “A Systematic Debugging and Performance Analysis Framework for Android Platforms”, Proceedings of 2012 International Workshop on Performance, Applications, and Parallelism for Android and HTML5, Venice, Italy, June 2012. [Paper](PAPAH2012.pdf)
* original project page : http://sourceforge.net/projects/usbdevicesuppor/

* 경주현, 임성수, 이민석, 허성민, “안드로이드 스마트폰을 위한 USB기반 통합 디버깅 방법”, 정보과학회 논문지, 제39권 제2호, pp.170 ~ 177, 2012년 4월. 

There are multiple ways to debug your Linux kernel. They fall in two main categories
:* Debugging Setup
:* Using a kernel debugging

Both methods can be again divided in two groups
:* Using the DDD
:* Using the Eclipse debugger


# Debugging Setup

### Setup

The following step by step instructions were performed on my development system, consisting of
this is known to work on both Ubuntu Lucid Lynx (10.04.x LTS 64bit and 32bit). Other versions of Ubuntu or other variants of GNU/linux might require different configurations.

* Linux PC
* android full source (for gdb host application - $ANDROID_DIR/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/arm-eabi-gdb)
* android-agent-proxy : https://sourceforge.net/projects/usbdevicesuppor/files/android-agent-proxy/
* target board and reference kernel source

    pandaboard : android-2.6.35 L27.12.1-P2 (http://www.omappedia.org/wiki/PandaBoard_L27.12.1-P2_Release_Notes)
    nexus one  : android-2.3.4_r1 kernel GRJ22 (http://source.android.com/source/building-devices.html)
    nexus S    : android-2.3.4_r1 kernel GRJ22 (http://source.android.com/source/building-devices.html)
    odroid7    : kernel-20110629.gz.tar (http://com.odroid.com/sigong/nf_file_board/nfile_board_view.php?keyword=&bid=41)

### enable kgdb over the usb

* Download prebuilt image or apply partial source

prebuilt image and partial source
    pandaboard : L27.12.1-P2 - https://sourceforge.net/projects/usbdevicesuppor/files/
    nexus one  : android-2.3.4_r1 GRJ22  - https://sourceforge.net/projects/usbdevicesuppor/files/
    nexus S    : android-2.3.4_r1 GRJ22  - https://sourceforge.net/projects/usbdevicesuppor/files/
    odroid7    : kernel-20110629 - https://sourceforge.net/projects/usbdevicesuppor/files/

* enable kgdb over the usb device feature and compile

make menuconfig ARCH=arm

    Kernel hacking  --->
    [*] KGDB: kernel debugger  --->
      <*>   KGDB: use kgdb over the usb device 

    Device Drivers  --->
    [*] USB support  --->
      <*>   USB Gadget Support  ---> 
        [*]       Android gadget kgdb function


* flash prebuilt image or compiled image

* alternative method : using the already applied source.
    pandaboard(L27.12.1-P2): git clone git://usbdevicesuppor.git.sourceforge.net/gitroot/usbdevicesuppor/pandaboard
    nexus one(android-2.3.4_r1 GRJ22): git clone git://usbdevicesuppor.git.sourceforge.net/gitroot/usbdevicesuppor/nexus_one
    nexus S(android-2.3.4_r1 GRJ22): git clone git://usbdevicesuppor.git.sourceforge.net/gitroot/usbdevicesuppor/nexus_s
    odroid7(kernel-20110629) : git clone git://usbdevicesuppor.git.sourceforge.net/gitroot/usbdevicesuppor/odroid7


### Starting target machine

* pandaboard 

If you connect via serial console, you can see the below message.

     kgdb: Waiting for USB kgdb connection from remote gdb...

* nexus one

This machine does not have serial port.

* nexus S

This machine does not have serial port.

* odroid7

If you connect via serial console you can see below message.

     kgdb: Waiting for USB kgdb connection from remote gdb...


* we currently implemented using compile time breakpoint. If you do not want to stop. delete the below source and then use kgdbwait kernel parameter

     drivers/usb/gadget/f_kgdb.c 
     '''kgdb_schedule_breakpoint();'''

### checking for usb device

Configuring USB Access
Under GNU/linux systems (and specifically under Ubuntu systems), regular users can't directly access USB devices by default. The system needs to be configured to allow such access.

The recommended approach is to create a file /etc/udev/rules.d/51-android.rules (as the root user) 
and to copy the following lines in it. It must be replaced by the actual username of the user who is authorized to access the phones over USB.


     # Nexus One
     SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", ATTR{idProduct}=="4e12", MODE="0600", OWNER="<username>"
     # Nexus S
     SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", ATTR{idProduct}=="4e22", MODE="0600", OWNER="<username>"
     # Pandaboard
     SUBSYSTEM=="usb", ATTR{idVendor}=="0451", ATTR{idProduct}=="d100", MODE="0600", OWNER="<username>"
     # Odroid7
     SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", ATTR{idProduct}=="4e22", MODE="0600", OWNER="<username>"

* Pandaboard
     $ lsusb 
     Bus 002 Device 097: ID 0451:d100 Texas Instruments, Inc.


* Odroid7
     $ lsusb 
     Bus 002 Device 114: ID 18d1:0001 Google Inc

### Starting the android-agent-proxy

android-agent-proxy acts as gdb agent(gdb server) for USB connections, so it is an interface between gdb and USB. 
Currently android-agent-proxy supports usb interfaces

The easiest way to start the android-agent-proxy is using prebulit image.
(https://sourceforge.net/projects/usbdevicesuppor/files/android-agent-proxy/)

run $ sudo ./android-agent-proxy 5550^5551 0 v

     5550 (gdb TCP/IP port)
     5551 (telnet port for kernel console)
     0    ( )
     v    (usb)

you can check the connection.
     Agent Proxy 1.95 Started with: 5550^5551 0 v
     Agent Proxy running. pid: 14065
     '''check_device(): Device matches Android interface <~ establish a connection.'''

### run host gdb application

* go into your kernel source directory
* export ANDROID_DIR=<android full source directory>
* $ANDROID_DIR/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/arm-eabi-gdb vmlinux

### connect to target machine

* target remote localhost:5550
* info thread
print thread information.

* using sysrq-trigger 
     adb shell
     echo g > /proc/sysrq-trigger

### kernel console via telnet port
 * using kernel parameter : kgdbcon. 
 * or modify global valiable like below source.
     
     kernel/debug/debug_core.c  
     static int kgdb_use_con = 1;

 * run $ telnet localhost 5551



# Using a kernel debugging 

## Debugging with DDD
* ddd --debugger $ANDROID_DIR/mydroid/prebuilt/linux-x86/toolchain/arm-eabi-4.3.1/bin/arm-eabi-gdb vmlinux
* target remote localhost:5550

[[File:Ddd_3.png]]

## Debugging with Eclipse


[[project_screenshots]]
[[project_admins]]
[[download_button]]
