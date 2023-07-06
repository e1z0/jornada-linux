#!/bin/sh
clear
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- scripts prepare modules_prepare
# ake ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -C . M=sound/arm
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -C . M=drivers/input/touchscreen
echo DONE
