#!/bin/sh
### BEGIN INIT INFO
# Provides:          j720-buttons.sh
# Required-Start:    $remote_fs
# Required-Stop:
# X-Start-Before:    
# Default-Start:     2 3 4 5
# Default-Stop:
# X-Interactive:     false
# Short-Description: Button Handler
# Description:       Handle voll+/- buttons, mute and power
#                    
### END INIT INFO

if [ -f /usr/bin/jbuttons ]; then
    case "$1" in
        stop|status)
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
	    log_action_begin_msg "Stopping Jornada Button Handler"
            if killall jbuttons ; then
	        log_action_end_msg 0
	    else
	        log_action_end_msg $?
	    fi
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
	    log_action_begin_msg "Loading Jornada Button Handler"
            if (/usr/bin/jbuttons /dev/input/event0 &) ; then
	        log_action_end_msg 0
	    else
	        log_action_end_msg $?
	    fi
	    ;;
        *)
            echo 'Usage: /etc/init.d/j720-buttons {start|reload|restart|force-reload|stop|status}'
            exit 3
            ;;
    esac
fi
