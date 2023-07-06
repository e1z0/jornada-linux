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

// ********* Debugging tools **********
#undef DEBUG

#ifdef DEBUG
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif 
// ********* Debugging tools **********


#define AUDIO_CLK_BASE		561600

// SA1111 Sound Controller register write interface. Non-locking.
void         sa1111_sac_writereg(struct sa1111_dev *devptr, unsigned int val, u32 reg) {
	sa1111_writel(val, devptr->mapbase + reg);
}

// SA1111 Sound Controller register read interface.  Non-locking.
unsigned int sa1111_sac_readreg(struct sa1111_dev *devptr, u32 reg) {
	return sa1111_readl(devptr->mapbase + reg);
}

// Power up mic and speaker amps.  Non-locking.
static void sa1111_enable_amps(void) {
	PPSR &= ~(PPC_LDD3 | PPC_LDD4); // 5/6 are not leds
	PPDR |= PPC_LDD3 | PPC_LDD4;
	PPSR |= PPC_LDD4; /* enable speaker */
	PPSR |= PPC_LDD3; /* enable microphone */
	DPRINTK(KERN_INFO "sac: SA1111 speaker/mic pre-amps enabled\n");
}

// Power down mic and speaker amps.  Non-locking.
static void sa1111_disable_amps(void) {
	PPSR &= ~(PPC_LDD3 | PPC_LDD4); // 5/6 are not leds
	PPDR |= PPC_LDD3 | PPC_LDD4;
	PPSR &= ~PPC_LDD4; /* enable speaker */
	PPSR &= ~PPC_LDD3; /* enable microphone */
	DPRINTK(KERN_INFO "sac: SA1111 speaker/mic pre-amps disabled\n");
}

/* Enable the I2S clock. Non-locking. */
static void sa1111_enable_i2s_clock(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned int val; 

	val = sa1111_readl(sachip->base + SA1111_SKPCR);
	val|= SKPCR_I2SCLKEN;
	sa1111_writel(val, sachip->base + SA1111_SKPCR);
	DPRINTK(KERN_INFO "sac: SA1111 I2S clock enabled\n");
}

/* Disable the I2S clock. Non-locking. */
static void sa1111_disable_i2s_clock(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned int val; 

	val = sa1111_readl(sachip->base + SA1111_SKPCR);
	val&= ~(SKPCR_I2SCLKEN);
	sa1111_writel(val, sachip->base + SA1111_SKPCR);
	DPRINTK(KERN_INFO "sac: SA1111 I2S clock enabled\n");
}

/* Enable the L3 bus clock interface. Non-locking. */
static void sa1111_enable_l3_clock(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned int val;

	val = sa1111_readl(sachip->base + SA1111_SKPCR);
	val|= SKPCR_L3CLKEN;
	sa1111_writel(val, sachip->base + SA1111_SKPCR);
	DPRINTK(KERN_INFO "sac: SA1111 L3 clock enabled\n");
}

/* Disable the L3 bus clock interface. Non-locking. */
static void sa1111_disable_l3_clock(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned int val; 

	sa1111_sac_writereg(devptr, 0x00, SA1111_SACR1);
	DPRINTK(KERN_INFO "sac: SA1111 L3 interface enabled\n");

	val = sa1111_readl(sachip->base + SA1111_SKPCR);
	val&= ~SKPCR_L3CLKEN;
	sa1111_writel(val, sachip->base + SA1111_SKPCR);
	DPRINTK(KERN_INFO "sac: SA1111 L3 clock enabled\n");
}

/* Enable the SAC L3 interface. Non-locking. */
static void sa1111_enable_l3(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned int val;

	sa1111_sac_writereg(devptr, SACR1_L3EN, SA1111_SACR1);
	DPRINTK(KERN_INFO "sac: SA1111 L3 interface enabled\n");
}

/* Disable the SAC L3 interface. Non-locking. */
static void sa1111_disable_l3(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned int val; 

	sa1111_sac_writereg(devptr, 0x00, SA1111_SACR1);
	DPRINTK(KERN_INFO "sac: SA1111 L3 interface disabled\n");
}

/* Reset the SAC and enable it. Non-locking. */
static void sa1111_reset_enable_sac(struct sa1111_dev *devptr) {
	unsigned int val; 

	/* Activate and reset the Serial Audio Controller */
	val = sa1111_sac_readreg(devptr, SA1111_SACR0);
	val |= (SACR0_ENB | SACR0_RST);
	sa1111_sac_writereg(devptr, val, SA1111_SACR0);
	mdelay(5);
	val = sa1111_sac_readreg(devptr, SA1111_SACR0);
	val &= ~SACR0_RST;
	sa1111_sac_writereg(devptr, val, SA1111_SACR0);
	DPRINTK(KERN_INFO "sac: SA1111 SAC reset and enabled\n");
}

/* Disable the SAC. Non-locking. */
static void sa1111_disable_sac(struct sa1111_dev *devptr) {
	unsigned int val; 
	/* De-Activate the Serial Audio Controller */
	val = sa1111_sac_readreg(devptr, SA1111_SACR0);
	val &= ~SACR0_ENB;
	sa1111_sac_writereg(devptr, val, SA1111_SACR0);
	DPRINTK(KERN_INFO "sac: SA1111 SAC disabled\n");
}

// Calculates the sysclock divider based on the samplerate; this might have rounding errors.
static inline long sa1111_clkdiv_calc(long samplerate) {
	return ((AUDIO_CLK_BASE + samplerate/2)/samplerate);
}

// Returns the clock divider based on the table given in Intels SA1111 manual (+estimate for 48khz)
static inline long sa1111_clkdiv_tab(long samplerate) {
	long clk_div=70;

	if (samplerate >= 48000) {
		clk_div = 11;
 	}
	else if (samplerate >= 44100) {
		clk_div = 12;	
 	}
	else if (samplerate >= 32000) {
		clk_div = 18;
	}
	else if (samplerate >= 22050) {
		clk_div = 25;
	}
	else if (samplerate >= 16000) {
		clk_div = 35;
	}
	else if (samplerate >= 11025) {
		clk_div = 51;
	}
	else if (samplerate >= 8000) {
		clk_div = 70;
	}
	else {
		clk_div = 70;
	}
	return clk_div;
}

/// ********** PUBLIC INTERFACE *************************

/* Sets a new audio samplerate on the SAC chip level. Will disable I2S clock, write the 
 * SKAUD register and re-start clock. Locking. */
void sa1111_audio_setsamplerate(struct sa1111_dev *devptr, long rate) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned int clk_div;
	unsigned long flags;

	spin_lock_irqsave(&sachip->lock, flags);

	sa1111_disable_i2s_clock(devptr);

	// Set new sampling rate
	clk_div = sa1111_clkdiv_tab(rate);
	sa1111_writel(clk_div - 1, sachip->base + SA1111_SKAUD);

	sa1111_enable_i2s_clock(devptr);
	spin_unlock_irqrestore(&sachip->lock, flags);
}

/* Will initialize the SA1111 and powerup pre-amps and select I2S protocol. Locking. */
void sa1111_audio_init(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;
	unsigned int val; 

	DPRINTK(KERN_INFO "sac: SA1111 init...");
	DPRINTK(KERN_INFO "sac: SA1111 device id: %d\n", devptr->devid);
	DPRINTK(KERN_INFO "sac: SA1111 chip base: 0x%lxh\n", sachip->base);
	DPRINTK(KERN_INFO "sac: SA1111 SAC  base: 0x%lxh\n", devptr->mapbase);

	// Make sure only one thread is in the critical section below.
	spin_lock_irqsave(&sachip->lock, flags);
	
	// deselect AC Link
	sa1111_select_audio_mode(devptr, SA1111_AUDIO_I2S);
	DPRINTK(KERN_INFO "sac: SA1111 I2S protocol enabled\n");

	sa1111_reset_enable_sac(devptr);
	sa1111_enable_l3(devptr);
	sa1111_enable_l3_clock(devptr);
	sa1111_enable_i2s_clock(devptr);
	sa1111_enable_amps();
	
	spin_unlock_irqrestore(&sachip->lock, flags);
	DPRINTK(KERN_INFO "sac: done init.\n");
}

/* Will de-initialize the SA1111 and its I2S and L3 hardware. Locking. */
void sa1111_audio_shutdown(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;
	unsigned int val; 

	// Make sure only one thread is in the critical section below.
	spin_lock_irqsave(&sachip->lock, flags);
	
	sa1111_disable_amps();
	sa1111_disable_i2s_clock(devptr);
	sa1111_disable_l3_clock(devptr);
	sa1111_disable_l3(devptr);
	sa1111_disable_sac(devptr);

	spin_unlock_irqrestore(&sachip->lock, flags);

	DPRINTK(KERN_INFO "sac: done shutdown.\n");
}

/* Enable the L3 bus clock. Locking. */
void sa1111_l3_clockenable(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;

	spin_lock_irqsave(&sachip->lock, flags);
	sa1111_enable_l3_clock(devptr);
	spin_unlock_irqrestore(&sachip->lock, flags);
}

/* Stop the L3 bus clock. Locking.*/
void sa1111_l3_clockdisable(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;

	spin_lock_irqsave(&sachip->lock, flags);
	sa1111_disable_l3_clock(devptr);
	spin_unlock_irqrestore(&sachip->lock, flags);
}

/* Prepare a l3 transmission by (enables L3 + clock).  Locking. */
void sa1111_l3_start(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;

	spin_lock_irqsave(&sachip->lock, flags);
	sa1111_enable_l3(devptr);
	sa1111_enable_l3_clock(devptr);
	spin_unlock_irqrestore(&sachip->lock, flags);
}

/* End a l3 transmission (disables L3 + clock). Locking.*/
void sa1111_l3_end(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;

	spin_lock_irqsave(&sachip->lock, flags);
	sa1111_disable_l3_clock(devptr);
	sa1111_disable_l3(devptr);
	spin_unlock_irqrestore(&sachip->lock, flags);
}

/* Start I2S clock.  Locking. */
void sa1111_i2s_start(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;

	spin_lock_irqsave(&sachip->lock, flags);
	sa1111_enable_i2s_clock(devptr);
	spin_unlock_irqrestore(&sachip->lock, flags);
}

/* End a l3 transmission (stops L3 clock, disables L3). Locking.*/
void sa1111_i2s_end(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;

	spin_lock_irqsave(&sachip->lock, flags);
	sa1111_disable_i2s_clock(devptr);
	spin_unlock_irqrestore(&sachip->lock, flags);
}

/* Send a byte via SA1111-L3. Will return -1 if transmission unsuccessful. Locking.*/
int sa1111_l3_send_byte(struct sa1111_dev *devptr, unsigned char addr, unsigned char dat) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;

	int i=0;
	int err=0;
	unsigned int SASCR;
	unsigned int SACR1;
	
	// Make sure only one thread is in the critical section below.
	spin_lock_irqsave(&sachip->lock, flags);

	sa1111_sac_writereg(devptr, 0, SA1111_L3_CAR);
	sa1111_sac_writereg(devptr, 0, SA1111_L3_CDR);
	mdelay(1);
	SASCR = SASCR_DTS|SASCR_RDD;
	sa1111_sac_writereg(devptr, SASCR, SA1111_SASCR);
	sa1111_sac_writereg(devptr, addr,  SA1111_L3_CAR);
	sa1111_sac_writereg(devptr, dat,   SA1111_L3_CDR);

	// Wait for L3 to come back in 200ms
	while (((sa1111_sac_readreg(devptr, SA1111_SASR0) & SASR0_L3WD) == 0) && (i < 200)) {
		mdelay(1);
		i++;
	}
	// If still not confirmed, raise error flag
	if (((sa1111_sac_readreg(devptr, SA1111_SASR0) & SASR0_L3WD) == 0)) {
		DPRINTK(KERN_INFO "sac: L3 timeout.\n");
		err=-1;
	}
	
	SASCR = SASCR_DTS|SASCR_RDD;
	sa1111_sac_writereg(devptr, SASCR, SA1111_SASCR);
	
	// Give up the lock
	spin_unlock_irqrestore(&sachip->lock, flags);

	// Wait 16msec before next transfer (uda1344 L3 is limited to 64f/s)
	// mdelay(16);
	return err;
}
