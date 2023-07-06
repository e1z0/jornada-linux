#!/bin/bash

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root"
   exit 1
   else
        if [ "$1" == 59 ]; then
                echo "Setting CPU frequency to 59.0 MHz"
                busybox devmem 0x90020014 w 0x00000000

                elif [ "$1" == 73 ]; then
                echo "Setting CPU frequency to 73.7 MHz"
                busybox devmem 0x90020014 w 0x00000001

                elif [ "$1" == 88 ]; then
                echo "Setting CPU frequency to 88.5 MHz"
                busybox devmem 0x90020014 w 0x00000002

                elif [ "$1" == 103 ]; then
                echo "Setting CPU frequency to 103.2 MHz"
                busybox devmem 0x90020014 w 0x00000003

                elif [ "$1" == 118 ]; then
                echo "Setting CPU frequency to 118.0 MHz"
                busybox devmem 0x90020014 w 0x00000004

                elif [ "$1" == 132 ]; then
                echo "Setting CPU frequency to 132.7 MHz"
                busybox devmem 0x90020014 w 0x00000005

                elif [ "$1" == 147 ]; then
                echo "Setting CPU frequency to 147.5 MHz"
                busybox devmem 0x90020014 w 0x00000006

                elif [ "$1" == 162 ]; then
                echo "Setting CPU frequency to 162.2 MHz"
                busybox devmem 0x90020014 w 0x00000007

                elif [ "$1" == 176 ]; then
                echo "Setting CPU frequency to 176.9 MHz"
                busybox devmem 0x90020014 w 0x00000008

                elif [ "$1" == 191 ]; then
                echo "Setting CPU frequency to 191.7 MHz"
                busybox devmem 0x90020014 w 0x00000009

                elif [ "$1" == 206 ]; then
                echo "Setting CPU frequency to 206.4 MHz"
                busybox devmem 0x90020014 w 0x0000000A

                elif [ "$1" == 221 ]; then
                echo "Setting CPU frequency to 221.2 MHz"
                busybox devmem 0x90020014 w 0x0000000B

                else
                echo "Set the CPU frequency (example "./cpufreq.sh 206") - allowed values: 59, 73, 88, 103, 118, 132, 147, 162, 176, 191, 206, 221"

        fi
fi
