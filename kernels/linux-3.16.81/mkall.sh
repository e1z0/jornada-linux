#!/bin/sh
make -j20 ARCH=arm clean
make -j20 ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- zImage
make -j20 ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- modules
echo DONE
