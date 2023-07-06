#!/bin/sh
cp .config .config-`date +%s`
make ARCH=arm menuconfig
