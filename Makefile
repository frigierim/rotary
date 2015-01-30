
MODULE=rotary_drv
RPI_ADDRESS=192.168.1.14
 
# these two variables depend on where you have you raspberry kernel source and tools installed
CCPREFIX=/home/massimiliano/projects/rpi/tools-master/arm-bcm2708/arm-bcm2708-linux-gnueabi/bin/arm-bcm2708-linux-gnueabi-
KERNEL_SRC=/home/massimiliano/projects/rpi/linux/linux-rpi-3.6.y
 
obj-m += ${MODULE}.o
 
module_upload=${MODULE}.ko
 
all: clean compile
 
compile:
	make ARCH=arm CROSS_COMPILE=${CCPREFIX} -C ${KERNEL_SRC} M=$(PWD) modules
 
clean:
	make -C ${KERNEL_SRC} M=$(PWD) clean
 
 
# this just copies a file to raspberry
install:
	scp ${module_upload} pi@${RPI_ADDRESS}:/home/pi/
	ssh pi@${RPI_ADDRESS} "sudo rmmod ${module_upload} ; sudo insmod ${module_upload}"
 
info:
	modinfo  ${module_upload}


