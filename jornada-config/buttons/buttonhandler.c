#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h> 
#include <libevdev/libevdev.h>         /* sudo apt-get install -y libevdev-dev */
#include <libevdev/libevdev-uinput.h>  /* sudo apt-get install -y libevdev-dev */

#define EV_BUF_SIZE 16

// Handler scripts to call when a button is pressed.
#define FILE_POWER "/etc/j720b_power.sh"
#define FILE_VOLUP "/etc/j720b_volup.sh"
#define FILE_VOLDN "/etc/j720b_voldn.sh"
#define FILE_MUTE  "/etc/j720b_mute.sh"

// Handler for signint (Ctrl-C) to exit gracefully
static volatile int running = 1;
void intHandler(int dummy) {
    running = 0;
}

// Main entry point. Needs the event device to monitor as a parameter.
// Example:
// buttonhandler /dev/input/event0
int main(int argc, char *argv[]) {
    int fd, sz;
    unsigned i;

    unsigned short id[4];
    char name[256] = "N/A";

    struct input_event ev[EV_BUF_SIZE]; /* Read up to N events ata time */

    // Keep track of keys pressed/released
    unsigned short key = 0;
    signed   int   value = -1;
    unsigned short lastkey = 0;
    signed   int   lastvalue = -1;

    // Argument checking
    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s /dev/input/eventN\n"
            "Where X = input device number\n",
            argv[0]
        );
        return EINVAL;
    }

    // try to open event device
    if ((fd = open(argv[1], O_RDONLY)) < 0) {
        fprintf(stderr,
            "ERR %d:\n"
            "Unable to open `%s'\n"
            "%s\n",
            errno, argv[1], strerror(errno)
        );
    }

    // Get device information
    ioctl(fd, EVIOCGID, id); 
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    printf("Monitoring event device: %s\n", name);

    // Setup CTRL-C Handler
    signal(SIGINT, intHandler);

    /* Main Loop. Read event file and parse result while running is true; 
     * it will be set to false by the sigint handler. 
     */
    while(running) {
        sz = read(fd, ev, sizeof(struct input_event) * EV_BUF_SIZE);

        if (sz < (int) sizeof(struct input_event)) {
            fprintf(stderr,
                "ERR %d:\n"
                "Reading of `%s' failed\n"
                "%s\n",
                errno, argv[1], strerror(errno)
            );
            goto fine;
        }

        /* Implement code to translate type, code and value */
        for (i = 0; i < sz / sizeof(struct input_event); ++i) {
            #ifdef DEBUG
            fprintf(stderr,
                "%ld.%06ld: "
                "type=%02x "
                "code=%02x "
                "value=%02x\n",
                ev[i].time.tv_sec,
                ev[i].time.tv_usec,
                ev[i].type,
                ev[i].code,
                ev[i].value
            );
            #endif

            // Key and either pressed or released
            if (ev[i].type == EV_KEY && (ev[i].value==1 || ev[i].value==2)) {
                key = ev[i].code;
                value = ev[i].value;

                // I want key transitions
                if (key==lastkey && value!=lastvalue) {
                    //Handle J720 R/M/L/Power Keys, for each the existence 
                    // of a handler file is tested and if found executed
                    if (ev[i].code == KEY_POWER) {
                        #ifdef DEBUG
                        printf("power key\n");
                        #endif
                        if( access(FILE_POWER, F_OK ) == 0 ) {
                            #ifdef DEBUG
                            printf("file found, running\n");
                            #endif
                            int status = system(FILE_POWER);
                        }
                    }
                    else if (ev[i].code == KEY_VOLUMEDOWN) {
                        #ifdef DEBUG
                        printf("volume- key\n");
                        #endif
                        if( access(FILE_VOLDN, F_OK ) == 0 ) {
                            #ifdef DEBUG
                            printf("file found, running\n");
                            #endif
                            int status = system(FILE_VOLDN);
                        }
                    }
                    else if (ev[i].code == KEY_VOLUMEUP) {
                        #ifdef DEBUG
                        printf("volume+ key\n");
                        #endif
                        if( access(FILE_VOLUP, F_OK ) == 0 ) {
                            #ifdef DEBUG
                            printf("file found, running\n");
                            #endif
                            int status = system(FILE_VOLUP);
                        }
                    }
                    else if (ev[i].code == KEY_MUTE) {
                        #ifdef DEBUG
                        printf("mute key\n");
                        #endif
                        if( access(FILE_MUTE, F_OK ) == 0 ) {
                            #ifdef DEBUG
                            printf("file found, running\n");
                            #endif
                            int status = system(FILE_MUTE);
                        }
                    }
                    else if (ev[i].code == KEY_F12) {
                        #ifdef DEBUG
                        printf("f12 key\n");
                        #endif
                        if( access(FILE_MUTE, F_OK ) == 0 ) {
                            #ifdef DEBUG
                            printf("file found, running mute file\n");
                            #endif
                            int status = system(FILE_MUTE);
                        }
                    }
                }
                lastkey = key;
                lastvalue = value;
            }

        }
    }

fine:
    close(fd);
    return errno;
}