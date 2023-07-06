#!/bin/sh
# drive mappings

# dos=DOS
dos=503C-4946
linux=ef075780-ecd1-4e32-b77a-e942cb6c5004

if [ -d /media/timo/$dos ] && [ -d /media/timo/$linux ]; then
	sudo make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- INSTALL_MOD_PATH=/media/timo/$linux/ modules_install
	ls -la ./arch/arm/boot/zImage
	if [ -f /media/timo/$dos/zImage_tb ]; then
       		rm /media/timo/$dos/zImage_tb
	fi
	sudo cp -af ./arch/arm/boot/zImage /media/timo/$dos/zImage_tb
	sync
else 
	echo "CF card not found"
fi
echo DONE
