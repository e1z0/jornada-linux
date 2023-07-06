#!/bin/sh
# Handler script to transition from / to sleep state
# on press of the power button
if [ -f /var/run/j720.sleep ]; then
    /opt/jornada-config/jornada-config --unsleep
    rm /var/run/j720.sleep
else
    touch /var/run/j720.sleep
    /opt/jornada-config/jornada-config --sleep
fi