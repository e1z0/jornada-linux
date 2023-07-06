#!/bin/sh
### BEGIN INIT INFO
# Provides:          j720-keyboard.sh
# Required-Start:    $remote_fs
# Required-Stop:
# X-Start-Before:    
# Default-Start:     2 3 4 5
# Default-Stop:
# X-Interactive:     true
# Short-Description: Load the Jornada 720 keymap
# Description:       Load the keymap from /etc/j720-key.map
#                    which can be either US or German depending
#                    on what has been set in jornada-config
### END INIT INFO

if [ -f /usr/bin/loadkeys ]; then
    case "$1" in
        stop|status)
        # console-setup isn't a daemon
        ;;
        start|force-reload|restart|reload)
            if [ -f /lib/lsb/init-functions ]; then
                . /lib/lsb/init-functions
            else
                log_action_begin_msg () {
	            echo -n "$@... "
                }

                log_action_end_msg () {
	            if [ "$1" -eq 0 ]; then
	                echo done.
	            else
	                echo failed.
	            fi
                }
            fi
	    log_action_begin_msg "Loading /etc/j720-key.map"
            if /usr/bin/loadkeys /etc/j720-key.map ; then
	        log_action_end_msg 0
	    else
	        log_action_end_msg $?
	    fi
	    ;;
        *)
            echo 'Usage: /etc/init.d/j720-keyboard {start|reload|restart|force-reload|stop|status}'
            exit 3
            ;;
    esac
fi
