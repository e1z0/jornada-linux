/*
 *  jornada720-sac.h
 *
 *  Register interface to SA1111 Serial Audio Controller and L3 bus
 *
 *  Copyright (C) 2021 Timo Biesenbach
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/hrtimer.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/types.h>
// Hardware stuff
#include <linux/kernel.h>
#include <linux/kern_levels.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <mach/jornada720.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/hardware/sa1111.h>

#include "jornada720-common.h"
#include "jornada720-sac.h"
#include "jornada720-uda1344.h"


// ********* Debugging tools **********
#undef DEBUG

#ifdef DEBUG
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif 
// ********* Debugging tools **********

// The UDA1344 chip instance
static struct uda1344 uda_chip;

/* Return a reference to the chip instance */
struct uda1344* uda1344_instance(void) {
	return &uda_chip;
}

/* Synchronize registers of the uda_chip instance with the hardware. We need to
 * mirror it in SW since we can't read data from the chip. */ 
static void uda1344_sync(struct sa1111_dev *devptr) {
	int retries=0;
	int err=0;

__retry:
	err=0;
	sa1111_l3_start(devptr);
	
	if (uda_chip.dirty_flags & UDA_STATUS_DIRTY) {		
		err = sa1111_l3_send_byte(devptr, UDA1344_STATUS, STAT0 | uda_chip.regs.stat0);
		if (err<0) goto __fail;
		uda_chip.dirty_flags &= ~UDA_STATUS_DIRTY;
	}

	if (uda_chip.dirty_flags & UDA_VOLUME_DIRTY) {
		uda_chip.regs.data0_0 = DATA0_VOLUME(uda_chip.volume);
		err = sa1111_l3_send_byte(devptr, UDA1344_DATA,   DATA0 | uda_chip.regs.data0_0);
		if (err<0) goto __fail;
		uda_chip.dirty_flags &= ~UDA_VOLUME_DIRTY;
	}

	if (uda_chip.dirty_flags & UDA_BASS_TREBLE_DIRTY) {
		uda_chip.regs.data0_1 = DATA1_BASS(uda_chip.bass) | DATA1_TREBLE(uda_chip.treble);
		err = sa1111_l3_send_byte(devptr, UDA1344_DATA,   DATA1 | uda_chip.regs.data0_1);
		if (err<0) goto __fail;
		uda_chip.dirty_flags &= ~UDA_BASS_TREBLE_DIRTY;
	}

	if (uda_chip.dirty_flags & UDA_FILTERS_MUTE_DIRTY) {
		uda_chip.regs.data0_2 = ((uda_chip.deemp_mode & 0x03) << 3) | ((uda_chip.mute & 0x01) << 2) | (uda_chip.dsp_mode & 0x03);
		err = sa1111_l3_send_byte(devptr, UDA1344_DATA,   DATA2 | uda_chip.regs.data0_2);
		if (err<0) goto __fail;
		uda_chip.dirty_flags &= ~UDA_FILTERS_MUTE_DIRTY;
	}

	if (uda_chip.dirty_flags & UDA_POWER_DIRTY) {
		err = sa1111_l3_send_byte(devptr, UDA1344_DATA,   DATA3 | uda_chip.regs.data0_3);
		if (err<0) goto __fail;
		uda_chip.dirty_flags &= ~UDA_POWER_DIRTY;
	}

__fail:
	sa1111_l3_end(devptr);
	
	if (err<0 && ++retries<10) {
		goto __retry;
	}

	// Clear dirty flags
	uda_chip.dirty_flags=0;
}

/* Initialize the 1344 with some sensible defaults and turn on power. */
int uda1344_open(struct sa1111_dev *devptr) {
	uda_chip.active = 1;
	uda_chip.volume = 0;
	uda_chip.bass   = 0;
	uda_chip.treble = 0;
	uda_chip.mute   = 0;
	uda_chip.deemp_mode = 0;
	uda_chip.dsp_mode = 0;
	uda_chip.samplerate = 22050;
	uda_chip.dirty_flags = 0;
	uda_chip.regs.stat0   = STAT0_SC_256FS | STAT0_IF_I2S;
	uda_chip.regs.data0_0 = DATA0_VOLUME(0);
	uda_chip.regs.data0_1 = DATA1_BASS(0) | DATA1_TREBLE(0);
	uda_chip.regs.data0_2 = DATA2_DEEMP_NONE | DATA2_FILTER_MAX;
	uda_chip.regs.data0_3 = DATA3_POWER_ON;

	// Enforce full sync
	uda_chip.dirty_flags = (UDA_STATUS_DIRTY | UDA_VOLUME_DIRTY | UDA_BASS_TREBLE_DIRTY | UDA_FILTERS_MUTE_DIRTY | UDA_POWER_DIRTY);

	uda1344_sync(devptr);
	return 0;
}

/* Close the UDA1344 device, in practice this means we deactivate the power */
void uda1344_close(struct sa1111_dev *devptr) {
	uda_chip.active = 0;
	uda_chip.regs.data0_3 = DATA3_POWER_OFF;
	uda_chip.dirty_flags = UDA_POWER_DIRTY;
	uda1344_sync(devptr);
	// Stop L3 clock
	sa1111_l3_end(devptr);
}

/* Setup the samplerate for both the UDA1344 and the SA1111 devices */
void uda1344_set_samplerate(struct sa1111_dev *devptr, long rate) {
	unsigned long flags;
	unsigned int val;

	/*
	 * Samplerates as per Table 7-6 from Intels SA1111 datasheet
	 */
	if (rate >= 48000) {
		rate = 48000;		
 	}	
	else if (rate >= 44100) {
		rate = 44100;		
 	}
	else if (rate >= 32000) {
		rate = 32000;
	}
	else if (rate >= 22050) {
		rate = 22050;
	}
	else if (rate >= 16000) {
		rate = 16000;
	}
	else if (rate >= 11025) {
		rate = 11025;
	}
	else if (rate >= 8000) {
		rate = 8000;
	}
	else {
		rate = 8000;
	}
	uda_chip.samplerate = rate;
	DPRINTK(KERN_INFO "uda1344: SA1111 PLL clock: %d\n", sa1111_pll_clock(devptr));

	// Set the UDA1344 sysclock divider - turns out it is crucial to do this BEFORE
	// reprogramming the SA1111 sysclock... 
	// uda_chip.regs.stat0 &= ~(STAT0_SC_MASK);
	uda_chip.regs.stat0 = 0x00;
	switch (rate) {
		case 8000:
		case 11025:
		case 16000:
		case 22050:
		case 32000:
			uda_chip.regs.stat0 = STAT0_SC_256FS | STAT0_IF_I2S;
			break;
		case 44100:
		case 48000:
			uda_chip.regs.stat0 = STAT0_SC_512FS | STAT0_IF_I2S;
			break;
	}
	// Try to sync all, maybe this avoids the hissing sound 1s into the replay
	uda_chip.dirty_flags = (UDA_STATUS_DIRTY | UDA_VOLUME_DIRTY); 
	uda1344_sync(devptr);

	DPRINTK(KERN_INFO "uda1344: SA1111_SKAUD: %d\n", val);
}

/* Set the volume for UDA1344 codec */
void uda1344_set_volume(struct sa1111_dev *devptr, int volume) {
	// we need to transmogrify the volume setting here
	// so that -63 -> 1....1
	// and       0 -> 0....0
	// i.e. invert the volume, then convert to byte range
	volume = volume * -1;
	uda_chip.volume = volume & 0x3f;
	uda_chip.dirty_flags = UDA_VOLUME_DIRTY;
	uda1344_sync(devptr);
}

/* Get the volume from UDA1344 codec */
int uda1344_get_volume(struct sa1111_dev *devptr) {
	// transmogrify the volume setting
	int volume = uda_chip.volume;
	volume = volume * -1;
	return volume;
}

/* Set the mute for UDA1344 codec (1=mute, 0=unmute) */
extern void uda1344_set_mute(struct sa1111_dev *devptr, int mute) {
	// limit to range 0..15	
	uda_chip.mute = mute & 0x01;
	uda_chip.dirty_flags = UDA_FILTERS_MUTE_DIRTY;
	uda1344_sync(devptr);
}

extern int uda1344_get_mute(struct sa1111_dev *devptr) {
	return uda_chip.mute;
}

/* Set the bass for UDA1344 codec (0 ... 15) */
void uda1344_set_bass(struct sa1111_dev *devptr, int bass) {
	// limit to range 0..15	
	uda_chip.bass = bass & 0x0f;
	uda_chip.dirty_flags = UDA_BASS_TREBLE_DIRTY;
	uda1344_sync(devptr);
}
int uda1344_get_bass(struct sa1111_dev *devptr) {
	return uda_chip.bass;
}

/* Set the treble for UDA1344 codec  (0..3) */
void uda1344_set_treble(struct sa1111_dev *devptr, int treble) {
	// limit to range 0..3	
	uda_chip.treble = treble & 0x03;
	uda_chip.dirty_flags = UDA_BASS_TREBLE_DIRTY;
	uda1344_sync(devptr);
}
int uda1344_get_treble(struct sa1111_dev *devptr) {
	return uda_chip.treble;
}

/* Set the dsp level for UDA1344 codec  (0..3) */
void uda1344_set_dsp(struct sa1111_dev *devptr, int dsp) {
	// limit to range 0..3	
	uda_chip.dsp_mode = dsp & 0x03;
	uda_chip.dirty_flags = UDA_FILTERS_MUTE_DIRTY;
	uda1344_sync(devptr);
}
int uda1344_get_dsp(struct sa1111_dev *devptr){
	return uda_chip.dsp_mode;
}

/* Set the deemphasis level for UDA1344 codec  (0..3) */
void uda1344_set_deemp(struct sa1111_dev *devptr, int de_emp){
	// limit to range 0..3	
	uda_chip.deemp_mode = de_emp & 0x03;
	uda_chip.dirty_flags = UDA_FILTERS_MUTE_DIRTY;
	uda1344_sync(devptr);
}
int uda1344_get_deemp(struct sa1111_dev *devptr){
	return uda_chip.deemp_mode;
}
