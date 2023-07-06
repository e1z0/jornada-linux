/*
 * drivers/input/touchscreen/jornada720_ts.c
 *
 * Copyright (C) 2022 Timo Biesenbach <timo.biesenbach@gmail.com>
 *
 * Copyright (C) 2007 Kristoffer Ericson <Kristoffer.Ericson@gmail.com>
 *
 * Copyright (C) 2006 Filip Zyzniewski <filip.zyzniewski@tefnet.pl>
 * based on HP Jornada 56x touchscreen driver by Alex Lange <chicken@handhelds.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * HP Jornada 710/720/729 Touchscreen Driver
 */

#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/jornada720.h>
#include <mach/irqs.h>

//Timer Variable for detecting pen-up
#define TIMEOUT 50    //milliseconds
static struct timer_list pen_timer;
static unsigned int count = 0;

MODULE_AUTHOR("Timo Biesenbach<timo.biesenbach@gmail.com>");
MODULE_DESCRIPTION("HP Jornada 710/720/728 touchscreen driver, improved, based on work by K.Ericson");
MODULE_LICENSE("GPL v2");

// Module parameters
static int rmb = 0;
module_param(rmb, int, 0444);
MODULE_PARM_DESC(rmb, "mousebutton: right mouse button emulation (0=off, 1=RMB on settings softkey).");

// Module parameters
static int mmb = 0;
module_param(mmb, int, 0444);
MODULE_PARM_DESC(mmb, "mousebutton: middle mouse button emulation (0=off, 1=MMB on pccard softkey).");

// Module parameters
static int lmb = 0;
module_param(lmb, int, 0444);
MODULE_PARM_DESC(lmb, "mousebutton: left mouse button emulation (0=off, 1=touch & LMB on pen, 2=no touch, LMB on pen, 3=no touch, LMB on phone softkey, media player to lock).");

static int dx = 0;
module_param(dx, int, 0444);
MODULE_PARM_DESC(dx, "calibration: x-offset.");

static int dy = 0;
module_param(dy, int, 0444);
MODULE_PARM_DESC(dy, "calibration: y-offset.");

static int mx = 1024;
module_param(mx, int, 0444);
MODULE_PARM_DESC(mx, "calibration: x-gradient in 1/1024.");

static int my = 1024;
module_param(my, int, 0444);
MODULE_PARM_DESC(my, "calibration: y-gradient  in 1/1024.");

static int filter = 0;
module_param(filter, int, 0444);
MODULE_PARM_DESC(filter, "filter: specifies smoothing over the n last coordinates. 0 to disable, max. 100.");

static int relative = 0;
module_param(relative, int, 0444);
MODULE_PARM_DESC(relative, "relative: 1:switch to reporting relative movement (like a mouse), 0: absolute movement (default).");

static int x_invert = 0;    // in case of negative calibration data
static int y_invert = 0;

static int calibrated = 0;  // Signals that calibration data is loaded.

static int rmb_sent = 0;    // we send a RMB event (need to close it on next gesture)
static int mmb_sent = 0;    // we send a RMB event (need to close it on next gesture)
static int lmb_sent = 0;    // we send a LMB event (need to close it on next gesture)
static int touch_sent = 0;    // we send a TOUCH event (need to close it on next gesture)
static int lmb_locked = 0;    // left mouse button is locked (media icon toggles) --> do not send a release.

// rolling average for smoothing coords
#define XY_HISTORY 100
static int hist_x[XY_HISTORY];
static int hist_y[XY_HISTORY];
static int hist_count = 0; // number of samples in history
static int hist_ptr = 0;   // curent sample 

// relative movement support
#define MAXX 640    // maximum screen size
#define MAXY 240
static int old_x = -1;	//previous pointer position
static int old_y = -1;

static int pendown = 0; // track if pen is touched or lifted

struct jornada_ts {
	struct input_dev *dev;
	int x_data[4];		/* X sample values */
	int y_data[4];		/* Y sample values */
};

static void jornada720_ts_collect_data(struct jornada_ts *jornada_ts)
{

    /* 3 low word X samples */
    jornada_ts->x_data[0] = jornada_ssp_byte(TXDUMMY);
    jornada_ts->x_data[1] = jornada_ssp_byte(TXDUMMY);
    jornada_ts->x_data[2] = jornada_ssp_byte(TXDUMMY);

    /* 3 low word Y samples */
    jornada_ts->y_data[0] = jornada_ssp_byte(TXDUMMY);
    jornada_ts->y_data[1] = jornada_ssp_byte(TXDUMMY);
    jornada_ts->y_data[2] = jornada_ssp_byte(TXDUMMY);

    /* combined x samples bits */
    jornada_ts->x_data[3] = jornada_ssp_byte(TXDUMMY);

    /* combined y samples bits */
    jornada_ts->y_data[3] = jornada_ssp_byte(TXDUMMY);
}

static int jornada720_ts_average(int coords[4])
{
	int coord;
	int high_bits = coords[3];

	coord  = coords[0] | ((high_bits & 0x03) << 8);
	coord += coords[1] | ((high_bits & 0x0c) << 6);
	coord += coords[2] | ((high_bits & 0x30) << 4);

	return coord / 3;
}

// simplistic rolling average smoothing filter
// will keep a history of up to XY_HISTORY samples in two arrays
// - the current sample will be added at the hist_ptr++ location
// - the number of samples will be tracked in hist_count
// - hist_count and hist_ptr will be reset on pen lift
// this fuction will return an average of the coord values in the list
static void jornada720_ts_filter(int *px, int *py) {
	int tx, ty, i;
	tx = *px;
	ty = *py;

	hist_count++;
	if (hist_count>filter) hist_count=filter;

	hist_x[hist_ptr] = tx;
	hist_y[hist_ptr] = ty;
	hist_ptr = (hist_ptr+1) % filter;
	
	tx = 0;
	ty = 0;
	for (i=0;i<hist_count;i++) {
		tx += hist_x[i];
		ty += hist_y[i];
	}

	tx = tx / hist_count;
	ty = ty / hist_count;

	*px = tx;
	*py = ty;
}

static void jornada720_ts_filter_reset(void) {
	hist_count=0;
	hist_ptr=0;
}

// Simplistic linear calibration function.
static void jornada720_ts_calibrate(int *px, int *py) {
	int tx, ty;

	tx = *px;
	ty = *py;

	tx = ((tx * mx) >> 10) + dx;
	ty = ((ty * my) >> 10) + dy;

	// Axis inversion again
	if (x_invert) tx=640-tx;
	if (y_invert) ty=240-ty;

	// Handle potential negative values
	if (tx<0) tx=0;
	if (ty<0) ty=0;

	*px = tx;
	*py = ty;
}

static void jornada720_ts_reset_relxy(void) {
	old_x=-1;
	old_y=-1;
}

// Get dx/dy, save old x and y for next round.
static void jornada720_ts_get_relxy(int *px, int *py) {
	int dx = 0;
	int dy = 0;

	if (old_x>0) dx = *px - old_x;
	if (old_y>0) dy = *py - old_y;

	old_x = *px;
	old_y = *py;

	// Limit movement to -1 / +1
	if (dx >  1) dx=1;
	if (dx < -1) dx=-1;
	if (dy >  1) dy=1;
	if (dy < -1) dx=-1;

	*px = dx;
	*py = dy;
}

// Timer Callback function. This will be called when timer expires to detect if then pen was lifted.
void pen_timer_callback(long unsigned int data)
{
	struct input_dev *input = (struct input_dev*)data;
	if (input!=NULL) {

		// Now Check pen state - if GPIO9 is high it has been lifted.
		if (GPLR & GPIO_GPIO(9)) {

			// Track pen state
			pendown=0;

			if (touch_sent) {
				input_report_key(input, BTN_TOUCH, 0);
				touch_sent=0;
			}

			if (lmb_sent && !lmb_locked) {
				input_report_key(input, BTN_LEFT, 0);				
				lmb_sent=0;
			}

			if (mmb_sent) {
				input_report_key(input, BTN_MIDDLE, 0);	
				mmb_sent=0;
			}

			if (rmb_sent) {
				input_report_key(input, BTN_RIGHT, 0);
				rmb_sent=0;
			}

			input_sync(input);
		} 
	}
	// Reset the averaging on pen up
	jornada720_ts_filter_reset();

	// reset old rel x/y to avoid a jump
	jornada720_ts_reset_relxy();
}

static irqreturn_t jornada720_ts_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct jornada_ts *jornada_ts = platform_get_drvdata(pdev);
	struct input_dev *input = jornada_ts->dev;
	int x, y;

	/*
	From HPs hardware manual:
	"The pen status should be monitored on GPIO 9. When the pen is down,
	GPIO 9 is low, and when there's a new ADC samples for the CPU to
	collect from MCU, the GPIO 9 will be pulsed (i.e. goes high and low).
	So the CPU should collect data whenever it detect a falling edge on
	GPIO 9. GPIO 9 will go back high when the pen is up.  To collect the
	ADC samples from MCU, send out GetTouchSamples and 8 TxDummy."

	1) Change IRQ handler to falling edge, trigger on rising edge could explain some glitches since these is no real data to read.
	2) Change the below from checking pen state before to checking after the samples have been collected. Assumption is that if
	   it is high after, then pen has been lifted. If it stays low, would indicate that the pen is still touched.
	   Turns out this requires a timer be added after the samples reading part.
	*/

	// Cancel the pen-up timer for the duration of the below and track pen-down state
	del_timer(&pen_timer);

	// This was called due to falling edge on GPIO, means: pen must have touched surface and there are coords to fetch.
	jornada_ssp_start();

	/* proper reply to request is always TXDUMMY */
	if (jornada_ssp_inout(GETTOUCHSAMPLES) == TXDUMMY) {
		jornada720_ts_collect_data(jornada_ts);

		x = jornada720_ts_average(jornada_ts->x_data);
		y = jornada720_ts_average(jornada_ts->y_data);

		// If the touchscreen is calibrated activate additional features
		// drop first coordinate samples if the pen was up before
		if (calibrated && pendown) { 
			// Adjust coords if we have calibration data
			jornada720_ts_calibrate(&x, &y);

			// We're in the active area, send the position update
			if (x<=640) {
				// smooth out coordinates if activated.
				if (filter) jornada720_ts_filter(&x, &y);

				if (lmb<2) {
					input_report_key(input, BTN_TOUCH, 1);
					touch_sent=1;
				}
				if (lmb==1 || lmb==2) {
					input_report_key(input, BTN_LEFT, 1);
					lmb_sent=1;
				}

				// if we play mouse, calculate the movement offset
				if (relative) {
					jornada720_ts_get_relxy(&x, &y);  // might need to scale the reported value
					input_report_rel(input, REL_X, x);
					input_report_rel(input, REL_Y, y);
					
				} else {
					input_report_abs(input, ABS_X, x);
					input_report_abs(input, ABS_Y, y);				
				}
				input_sync(input);						
			}					
			// else we're in the app key area 
			else {
				if (y<=60) {  //(1) settings hotkey (RMB)
					// If LMB was locked, release it now.
					if (lmb==3 && lmb_locked) {
						input_report_key(input, BTN_LEFT, 0);								
						lmb_locked=0;
					}							

					// if RMB emulation is active
					if (rmb) { 
						// Send the RMB event
						input_report_key(input, BTN_RIGHT, 1);
						rmb_sent=1;
					}							
				}

				if (y<=120 && y>60) {  // pc card hotkey (2)
					// if MMB emulation is active
					if (mmb) { 
						// Send the RMB event
						input_report_key(input, BTN_MIDDLE, 1);
						mmb_sent=1;
					}
				}

				if (y<=180 && y>120) {  // phone hotkey (3)
					// reserved for LMB
					if (lmb==3 && !lmb_locked) { 
						// Send the LMB event
						input_report_key(input, BTN_LEFT, 1);
						lmb_sent=1;
					}

					// If LMB was locked, we release it now.
					if (lmb==3 && lmb_locked) {
						lmb_locked=0;
					}
				}

				if (y<=240 && y>180) {  // media hotkey (3)
					// reserved for LMB lock
					if (lmb==3 && !lmb_locked) {
						// Send the LMB event
						input_report_key(input, BTN_LEFT, 1);
						lmb_locked=1;
					}
				}

				// Send app key area update
				input_sync(input);
			}
		} 
		// Just dump the data out if uncalibrated
		else if (!calibrated) {					
			input_report_key(input, BTN_TOUCH, 1);
			input_report_abs(input, ABS_X, x);
			input_report_abs(input, ABS_Y, y);				
			input_sync(input);
			touch_sent=1;
		}
	}
	jornada_ssp_end();

	// Track pendown state.
	pendown=1;

    /* setup a timer to check for the pen-up state in 50msec */
    mod_timer(&pen_timer, jiffies + msecs_to_jiffies(TIMEOUT));

	return IRQ_HANDLED;
}

static int jornada720_ts_probe(struct platform_device *pdev)
{
	struct jornada_ts *jornada_ts;
	struct input_dev *input_dev;
	int error;

	jornada_ts = kzalloc(sizeof(struct jornada_ts), GFP_KERNEL);
	input_dev = input_allocate_device();

	if (!jornada_ts || !input_dev) {
		error = -ENOMEM;
		goto fail1;
	}

	platform_set_drvdata(pdev, jornada_ts);

	jornada_ts->dev = input_dev;

	input_dev->name = "HP Jornada 7xx Touchscreen";
	input_dev->phys = "jornadats/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->dev.parent = &pdev->dev;

	// did we get calibration data?
	if (mx!=1024 && my!=1024) {
		calibrated=1;
		printk(KERN_INFO "HP7XX TS : Calibration data mx %d dx %d, my %d dy %d\n", mx, dx, my, dy);
	} 
	// default stupid touchscreen.
	else {
		calibrated=0;
		lmb=0;
		rmb=0;
		mmb=0;
		filter=0;
		relative=0;
		printk(KERN_INFO "HP7XX TS : Uncalibrated mode, disabling advanced features. Provide calibration data to use them.");
	}

	input_dev->evbit[0] |= BIT_MASK(EV_KEY);

	// should we behave like a mouse?
	if (relative) {
		input_dev->evbit[0] |= BIT_MASK(EV_REL);
		input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	} else {
		input_dev->evbit[0] |= BIT_MASK(EV_ABS);
		if (calibrated) {
			input_set_abs_params(input_dev, ABS_X, 0, MAXX, 8, 0);			
			input_set_abs_params(input_dev, ABS_Y, 0, MAXY, 8, 0);
		} else {
			// default if uncalibrated.
			input_set_abs_params(input_dev, ABS_X, 270, 3900, 0, 0);
			input_set_abs_params(input_dev, ABS_Y, 180, 3700, 0, 0);
		}			
	}

	// __set_bit(ABS_MISC, input_dev->absbit); // does this help with anything?

	// In relative we have to have seperate mouse buttons -> lmb mode 3
	if (relative) {
		lmb=3;
		rmb=1;
		mmb=1;
		printk(KERN_INFO "HP7XX TS : Relative mode active, re-configuring mouse button settings.\n");
	}

	// Activate LMB / RMB reporting if emulation on and not in relative mode
	if (lmb<2) {
		__set_bit(BTN_TOUCH, input_dev->keybit);
		printk(KERN_INFO "HP7XX TS : Touch events active.\n");
	}
	if (lmb) {
		__set_bit(BTN_LEFT, input_dev->keybit);
		printk(KERN_INFO "HP7XX TS : Left mouse button emulation active.\n");
	}
	if (lmb==1) 
		printk(KERN_INFO "         : mode 1: Adding LMB to touch event.");

	if (lmb==2) 	
		printk(KERN_INFO "         : mode 2: LMB only on pendown, no touch");

	if (lmb==3)
		printk(KERN_INFO "         : mode 3: LMB only on phone softkey (mediaplayer key for LMB lock)");

	if (mmb) {
		__set_bit(BTN_MIDDLE, input_dev->keybit);
		printk(KERN_INFO "HP7XX TS : Middle mouse button emulation active.\n");
	}

	if (rmb) {
		__set_bit(BTN_RIGHT, input_dev->keybit);
		printk(KERN_INFO "HP7XX TS : Right mouse button emulation active.\n");
	}

	// clamp filter parameter to acceptable range
	if (filter<0) filter=0;
	if (filter>XY_HISTORY) filter=XY_HISTORY;
	if (filter) {
		printk(KERN_INFO "HP7XX TS : X/Y averaging filter active with %d samples.\n", filter);
	}

	// Changed to trigger on falling edge
	error = request_irq(IRQ_GPIO9, jornada720_ts_interrupt, IRQF_TRIGGER_FALLING, "HP7XX Touchscreen driver", pdev);
	if (error) {
		printk(KERN_INFO "HP7XX TS : Unable to acquire irq!\n");
		goto fail1;
	}

	error = input_register_device(jornada_ts->dev);
	if (error) goto fail2;

	// If we have calibration data, report a reasonable screen size
	// and process inverted axes if needed.
	if (calibrated) {
		// Handle axis inversion
		if (mx<0) {
			printk(KERN_INFO "HP7XX TS : Inverted X Axis\n");
			x_invert = 1;
			mx = mx * -1;
			dx = dx * -1;
		}
		if (my<0) {
			printk(KERN_INFO "HP7XX TS : Inverted Y Axis\n");
			y_invert = 1;
			my = my * -1;
			dy = dy * -1;
		}
	}	

	// Setup pen-up timer, do not start yet
    setup_timer(&pen_timer, pen_timer_callback, (long unsigned int)input_dev);

	return 0;

 fail2:
	free_irq(IRQ_GPIO9, pdev);
 fail1:
	input_free_device(input_dev);
	kfree(jornada_ts);
	return error;
}

static int jornada720_ts_remove(struct platform_device *pdev)
{
	struct jornada_ts *jornada_ts = platform_get_drvdata(pdev);

    /* remove the pen-timer when unloading module */
    del_timer(&pen_timer);

	free_irq(IRQ_GPIO9, pdev);
	input_unregister_device(jornada_ts->dev);
	kfree(jornada_ts);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:jornada_ts");

static struct platform_driver jornada720_ts_driver = {
	.probe		= jornada720_ts_probe,
	.remove		= jornada720_ts_remove,
	.driver		= {
		.name	= "jornada_ts",
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(jornada720_ts_driver);
