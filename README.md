*Example kernel driver for Raspberry Pi.*

This driver collects the pulses from a rotary dialer (an old telephone) and converts those pulses into a stream of digits to a pipe device.
The (admittedly very simple) debouncing algorithm must probably be tuned to each specific dialer, this is what worked for me.  

In order to compile the driver you will need the kernel sources for your RPi distro and the ARM build toolchain (please follow the instructions in http://elinux.org/RPi_Kernel_Compilation)

Just modify the makefile with the correct locations of these folders.

This is a sample implementation of the receiving end for the pipe, written in bash:


	#!/bin/bash

	################## MAIN ######################################

	# data file
	INPUT=/dev/rotary
	CHARDEV=$(grep rotary_device /proc/devices | cut -f 1 -d " ")

	# Create device
	&>/dev/null sudo rm $INPUT
	sudo mknod $INPUT c $CHARDEV 0

	# main loop
	cat "$INPUT" | while read char
	do
			echo Received: $char
			char=
	done
	
	
*License*

This code is distributed under the GPL license (https://www.gnu.org/licenses/gpl), because it uses gpio_free which is marked as GPL-only.
