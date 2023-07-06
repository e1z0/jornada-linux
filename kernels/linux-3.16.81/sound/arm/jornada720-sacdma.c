/*
 *  linux/arch/arm/mach-sa1100/dma-sa1111.c
 *
 *  Copyright (C) 2000 John Dorsey
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  4 September 2000 - created.
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
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <mach/jornada720.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/hardware/sa1111.h>

#include "jornada720-common.h"
#include "jornada720-sacdma.h"
#include "jornada720-sac.h"


// ********* Debugging tools **********
#undef DEBUG
#undef DEBUG_DMA

#ifdef DEBUG
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif 
// ********* Debugging tools **********


/* DMA channel structure */
typedef struct {
	unsigned int direction;			/* Direction: 0 Play, 1 Rec */
	unsigned int in_use;			/* Device is allocated */
	int running;					/* 1 if DMA is running */
	unsigned int count;				/* Counts DMA starts, used to alternate between engines A and B (even count -> engine A, odd count engine B) */
	dma_buf_t *dma_buffer;			/* buffer currently DMA, can be larger than the max 8kb transfer size */
	dma_block_callback callback; 	/* Callback function when block transferred */
	unsigned int irq_a; 			/* IRQ allocated for engine A */
	unsigned int irq_b; 			/* IRQ allocated for engine B */
} sa1111_sac_dma_t;

// Represents the two SA1111 DMA channels, 0=Play, 1=Record */
static sa1111_sac_dma_t dma_channels[SA1111_SAC_DMA_CHANNELS] = { 
  {
	.direction = SA1111_SAC_XMT_CHANNEL,
	.in_use = 0,
	.running = 0,
	.count = 0,
	.dma_buffer = NULL,
	.callback = NULL,
	.irq_a = 0,
	.irq_b = 0,
  },
  {
	.direction = SA1111_SAC_RCV_CHANNEL,
	.in_use = 0,
	.running = 0,
	.count = 0,
	.dma_buffer = NULL,
	.callback = NULL,	
	.irq_a = 0,
	.irq_b = 0,
  }
};

/* Initialize dma_channels[ch] structure */
static inline void init_dma_ch(unsigned int channel) {

	dma_channels[channel].direction = channel;
	dma_channels[channel].in_use = 0;
	dma_channels[channel].running = 0;
	dma_channels[channel].count = dma_channels[channel].count % 2;
	dma_channels[channel].callback = NULL;
	dma_channels[channel].irq_a = 0;
	dma_channels[channel].irq_b = 0;
	dma_channels[channel].dma_buffer = NULL;

}

/*
 * Control register structure for the SA1111 SAC DMA.
 * As per the datasheet there is no such thing as "reset". Also several
 * register bits are read only.
 * We'll clear some bits and set addresses and counts to zero.
 */
static void init_sa1111_sac_dma(struct sa1111_dev *devptr, int direction) {
	unsigned int val;
	val = 0;
	
	/* Make sure direction is 0|1 */
	if (direction<0 || direction>1) {
		printk(KERN_ERR "sacdma: invalid direction %d\n", direction);
		return -EINVAL;
	}

	if (direction==SA1111_SAC_XMT_CHANNEL) {
		// TX
		sa1111_sac_writereg(devptr, val, SA1111_SADTCS); // Zero control register
		sa1111_sac_writereg(devptr, val, SA1111_SADTSA); // Zero address A register
		sa1111_sac_writereg(devptr, val, SA1111_SADTCA); // Zero count A register
		sa1111_sac_writereg(devptr, val, SA1111_SADTSB); // Zero address B register
		sa1111_sac_writereg(devptr, val, SA1111_SADTCB); // Zero count B register
		DPRINTK(KERN_INFO "sacdma: SA1111 SAC DMA TX registers reset.\n");
	}

	if  (direction==SA1111_SAC_RCV_CHANNEL) {
		//RX
		sa1111_sac_writereg(devptr, val, SA1111_SADRCS);
		sa1111_sac_writereg(devptr, val, SA1111_SADRSA); // Zero address A register
		sa1111_sac_writereg(devptr, val, SA1111_SADRCA); // Zero count A register
		sa1111_sac_writereg(devptr, val, SA1111_SADRSB); // Zero address B register
		sa1111_sac_writereg(devptr, val, SA1111_SADRCB); // Zero count B register
		DPRINTK(KERN_INFO "sacdma: SA1111 SAC DMA RX registers reset.\n");
	}
	
	// Program SACR0 DMA Thresholds
	val = sa1111_sac_readreg(devptr, SA1111_SACR0);
	val = val & 0xFF; //Mask out bits 8-31
	if (direction==SA1111_SAC_XMT_CHANNEL) {
		val = val | (SAC_FIFO_TX_THRESHOLD << 8);    // set TFTH to 7 (transmit fifo threshold 7=intel default)
	}
	if  (direction==SA1111_SAC_RCV_CHANNEL) {
		val = val | (SAC_FIFO_RX_THRESHOLD << 12);   // set RFTH to 7 (receive  fifo threshold 7=intel default)
	}
	sa1111_sac_writereg(devptr, val, SA1111_SACR0);

	DPRINTK(KERN_INFO "sacdma: SA1111 SAC initialized\n");
}

/* Find out if dma for the given direction is finished
 * will figure out the channel (A or B) based on the dma_cnt.
 * 
 * Note: this function will not wait, but only read the control register 
 *       and return immediately.
 */
static int is_done_sa1111_sac_dma(struct sa1111_dev *devptr, int direction) {
	unsigned int val;
	unsigned int REG_CS = SA1111_SADTCS + (direction * DMA_REG_RX_OFS);  // Control register

	// read status register
	val = sa1111_sac_readreg(devptr, REG_CS);
	if (dma_channels[direction].count % 2) {
		// Channel B
		if (val & SAD_CS_DBDB) return 1;
	} 
	else  {
		// Channel A
		if (val & SAD_CS_DBDA) return 1;
	}
	return 0;
}

/* Will program the SA1111 DMA engine to perform one dma cycle from physical address
 * dma_ptr with size bytes for direction (0=play, 1=record) and kick the transfer off.
 *
 * Note: This will not setup the dma interrupt handling.
 */
static int start_sa1111_sac_dma(struct sa1111_dev *devptr, dma_addr_t dma_ptr, size_t size, int direction) {
	DPRINTK(KERN_INFO "sacdma: start_sa1111_sac_dma\n");
	unsigned int val;
	unsigned int REG_CS    = SA1111_SADTCS + (direction * DMA_REG_RX_OFS);  // Control register
	unsigned int REG_ADDR  = SA1111_SADTSA + (direction * DMA_REG_RX_OFS);  // Address register
	unsigned int REG_COUNT = SA1111_SADTCA + (direction * DMA_REG_RX_OFS);  // Count register

	/* Make sure direction is 0|1 */
	if (direction<0 || direction>1) {
		printk(KERN_ERR "sacdma: invalid direction %d\n", direction);
		return -EINVAL;
	}

	/* Read control register */
	val = sa1111_sac_readreg(devptr, REG_CS);

	// we will alternate between channels A and B
	// this might or might not need be done for each direction separately.
	if (++dma_channels[direction].count % 2) {
		// Add offset for channel b registers to address / count regs
		REG_ADDR  += DMA_CH_B;
		REG_COUNT += DMA_CH_B;

		// update control reg value
		val |= SAD_CS_DSTB | SAD_CS_DEN;
		#ifdef DEBUG_DMA
		printk("sacdma: using DMA channel B\n");

		printk("sacdma: using DMA address reg 0x%lxh\n", REG_ADDR);
		printk("sacdma: using DMA count   reg 0x%lxh\n", REG_COUNT);
		printk("sacdma: using DMA control reg 0x%lxh\n", REG_CS);

		printk("sacdma: using DMA address     0x%lxh\n", dma_ptr);
		printk("sacdma: using DMA count       0x%lxh\n", size);
		printk("sacdma: using DMA control     0x%lxh\n", val);
		#endif
		sa1111_sac_writereg(devptr, dma_ptr, REG_ADDR);
		sa1111_sac_writereg(devptr, size, REG_COUNT);
		sa1111_sac_writereg(devptr, val, REG_CS);
	} 
	else {
		REG_ADDR  += DMA_CH_A;
		REG_COUNT += DMA_CH_A;

		// update control reg value
		val |= SAD_CS_DSTA | SAD_CS_DEN;
		#ifdef DEBUG_DMA
		printk(" using DMA channel A\n");

		printk("sacdma: using DMA address reg 0x%lxh\n", REG_ADDR);
		printk("sacdma: using DMA count   reg 0x%lxh\n", REG_COUNT);
		printk("sacdma: using DMA control reg 0x%lxh\n", REG_CS);

		printk("sacdma: using DMA address     0x%lxh\n", dma_ptr);
		printk("sacdma: using DMA count       0x%lxh\n", size);
		printk("sacdma: using DMA control     0x%lxh\n", val);
		#endif
		sa1111_sac_writereg(devptr, dma_ptr, REG_ADDR);
		sa1111_sac_writereg(devptr, size, REG_COUNT);
		sa1111_sac_writereg(devptr, val, REG_CS);
	}

	return 0;
}

static int stop_sa1111_sac_dma(struct sa1111_dev *devptr, int direction) {
	// we can't stop the hardware, so just set running to 0
	dma_channels[direction].running=0;
	return 0;
}

/* Simplistic handler routine to handle the SA1111 Audio DMA Done interrupts.
 * Will be called when one period of data has been DMA'ed
 * If dma is running for the direction and we're not finished yet (dma_ptr < start - size), then
 * 	 - start a new DMA transfer with the next block
 *   - call the registered callback (will be sth. to update the ALSA audio layer)
 */
static irqreturn_t sa1111_dma_irqhandler(int irq, void *devptr)  {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;
	
	#ifdef DEBUG_DMA
	DPRINTK(KERN_INFO "sacdma: sa1111_dma_irqhandler called for irq: %d\n", irq);
	#endif

	if (dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_start == NULL ||
		dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_ptr == NULL ||
		dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->size == 0
	) {
		printk(KERN_ERR "sacdma: sa1111_dma_irqhandler called with invalid values!");
	}

	switch (FROM_SA1111_IRQ(irq, devptr)) {
		case AUDXMTDMADONEA: 
		case AUDXMTDMADONEB:

			// Advance ptr by the period size played
			dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_ptr += dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->period_size;

			// Don't restart DMA if not running
			if (dma_channels[SA1111_SAC_XMT_CHANNEL].running) {

				// Play remaining buffer...
				if (dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_ptr < (dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_start + dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->size)
				) {

					size_t adjustment=0;
					// See if we overshoot the buffer
					if ((dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_ptr + dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->period_size) > 
						(dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_start + dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->size)) 
					{
							// Calculate adjustment to not overshoot
							adjustment = (dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_ptr + dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->period_size) - (dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_start + dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->size);
							// printk(KERN_ERR "sacdma: Adjusting for remaining buffer size by %d bytes\n", adjustment);
					}

					// Kick off next DMA and trigger callback
					// spin_lock_irqsave(&sachip->lock, flags);
					start_sa1111_sac_dma(devptr, dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_ptr, 
					                             dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->period_size - adjustment,
												 dma_channels[SA1111_SAC_XMT_CHANNEL].direction);	
					// spin_unlock_irqrestore(&sachip->lock, flags);
					
					if (dma_channels[SA1111_SAC_XMT_CHANNEL].callback != NULL)
						dma_channels[SA1111_SAC_XMT_CHANNEL].callback(dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer, STATE_RUNNING);
				} 
				// if end of buffer reached, replay from start if loop flag set
				else {
					// Count loops...
					dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->loop_count++;

					// Start next loop if loop flag set
					if (dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->loop) {
						dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_ptr = dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_start;
						
						// Kick off next DMA and trigger callback
						// spin_lock_irqsave(&sachip->lock, flags);
						start_sa1111_sac_dma(devptr, dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_ptr, 
						                             dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->period_size, 
													 dma_channels[SA1111_SAC_XMT_CHANNEL].direction);						
						// spin_unlock_irqrestore(&sachip->lock, flags);

						if (dma_channels[SA1111_SAC_XMT_CHANNEL].callback != NULL)
							dma_channels[SA1111_SAC_XMT_CHANNEL].callback(dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer, STATE_LOOPING);
					}
				}	
			}
		break;
		
		case AUDRCVDMADONEA: 
		case AUDRCVDMADONEB: 
			// to be done
		break;
	}
	return IRQ_HANDLED;
}

/* Setup interrupt handling for the transfer completion events from SA1111
 * Note: - request_irq will enable the interrupt handling
 *       - IRQ numbers are relative to the sa1111 chip irq_base, 
 *         thats what the TO/FROM SA1111_IRQ macro is for
 *       - device_driver is a unique "cookie" for free_irq to identify the IRQ
 */
static int sa1111_dma_irqrequest(struct sa1111_dev *devptr, unsigned int direction) {
	DPRINTK(KERN_ERR "sacdma: sa1111_irqrequest\n");

	unsigned int irqa, irqb;
	int err;

	/* Make sure direction is 0|1 */
	if (direction<0 || direction>1) {
		printk(KERN_ERR "sacdma: invalid direction %d\n", direction);
		return -EINVAL;
	}

	// Prevent double allocation
	if (dma_channels[direction].irq_a==0) {
		irqa = TO_SA1111_IRQ(AUDXMTDMADONEA + direction, devptr);
		err = request_irq(irqa, sa1111_dma_irqhandler, 0, SA1111_DRIVER_NAME(devptr), devptr);
		if (err) {
			printk(KERN_ERR "sacdma: unable to request IRQ %d for DMA channel %d (A)\n", irqa, direction);
			return err;
		}
		dma_channels[direction].irq_a = irqa;
	}

	// Prevent double allocation
	if (dma_channels[direction].irq_b==0) {
		irqb = TO_SA1111_IRQ(AUDXMTDMADONEB + direction, devptr);
		err = request_irq(irqb, sa1111_dma_irqhandler, 0, SA1111_DRIVER_NAME(devptr), devptr);
		if (err) {
			printk(KERN_ERR "sacdma: unable to request IRQ %d for DMA channel %d (B)\n", irqb, direction);
			free_irq(irqa, devptr);
			return err;
		}
		dma_channels[direction].irq_b = irqb;
	}

	return 0;
}

/* Release the IRQ handlers for the given direction */
static void sa1111_dma_irqrelease(struct sa1111_dev *devptr, unsigned int direction) {
	DPRINTK(KERN_INFO "sacdma: sa1111_dma_irqrelease\n");

	/* Make sure direction is 0|1 */
	if (direction<0 || direction>1) {
		printk(KERN_ERR "sacdma: invalid direction %d\n", direction);
		return -EINVAL;
	}

	if (dma_channels[direction].irq_a!=0) {
		DPRINTK(KERN_INFO "sacdma: sa1111_dma_irqrelease irq_a:  0x%xh\n", dma_channels[direction].irq_a);
		DPRINTK(KERN_INFO "sacdma: sa1111_dma_irqrelease devptr: 0x%lxh\n", devptr);
		free_irq(dma_channels[direction].irq_a, devptr);
		dma_channels[direction].irq_a = 0;
	}

	if (dma_channels[direction].irq_b!=0) {
		DPRINTK(KERN_INFO "sacdma: sa1111_dma_irqrelease irq_b:  0x%xh\n", dma_channels[direction].irq_a);
		DPRINTK(KERN_INFO "sacdma: sa1111_dma_irqrelease devptr: 0x%lxh\n", devptr);
		free_irq(dma_channels[direction].irq_b, devptr);
		dma_channels[direction].irq_b = 0;
	}

	DPRINTK(KERN_INFO "sacdma: sa1111_dma_irqrelease done\n");
}

// PUBLIC Interface

/* Allocate resources for PCM playback / recording */
int sa1111_dma_alloc(struct sa1111_dev *devptr) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;	
	int err=0;

	spin_lock_irqsave(&sachip->lock, flags);

	// Initialize data structs (needs to happen here to not wipe out irq#s)
	init_dma_ch(SA1111_SAC_XMT_CHANNEL);
	init_dma_ch(SA1111_SAC_RCV_CHANNEL);

	// Request and init Interrupt Handlers
	err = sa1111_dma_irqrequest(devptr, SA1111_SAC_XMT_CHANNEL);
	if (err < 0) {
		printk(KERN_ERR "sacdma: unable to request IRQs for TX channel, error %d\n", err);
		goto __fail;
	}

	// Request and init Interrupt Handlers
	err = sa1111_dma_irqrequest(devptr, SA1111_SAC_RCV_CHANNEL);
	if (err < 0) {
		printk(KERN_ERR "sacdma: unable to request IRQs for RX channel, error %d\n", err);
		sa1111_dma_irqrelease(devptr, SA1111_SAC_XMT_CHANNEL);
		goto __fail;
	}

	// Initialize SAC Hardware
	init_sa1111_sac_dma(devptr, SA1111_SAC_XMT_CHANNEL);
	init_sa1111_sac_dma(devptr, SA1111_SAC_RCV_CHANNEL);

__fail:
	spin_unlock_irqrestore(&sachip->lock, flags);
	return err;
}

/* Release resources for PCM playback / recording */
int sa1111_dma_release(struct sa1111_dev *devptr) {
	DPRINTK(KERN_INFO "sacdma: sa1111_dma_release devptr: 0x%lxh\n", devptr);
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;

	spin_lock_irqsave(&sachip->lock, flags);

	sa1111_dma_irqrelease(devptr, SA1111_SAC_XMT_CHANNEL);
	sa1111_dma_irqrelease(devptr, SA1111_SAC_RCV_CHANNEL);

	spin_unlock_irqrestore(&sachip->lock, flags);
	return 0;
}

int sa1111_dma_playback(struct sa1111_dev *devptr, dma_buf_t *dma_buffer, dma_block_callback callback) {
	DPRINTK(KERN_INFO "sacdma: sa1111_dma_playback\n");
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;
		
	int err=0;

	if (dma_buffer==NULL) {
		printk(KERN_ERR "sacdma: sa1111_dma_playback failed: dma_buffer is NULL.\n");
		return -EINVAL;		
	}

	if (dma_buffer->dma_ptr == NULL) {
		printk(KERN_ERR "sacdma: sa1111_dma_playback failed: dma_buffer->dma_ptr is NULL.\n");
		return -EINVAL;		
	}

	if (dma_buffer->size==0) {
		printk(KERN_ERR "sacdma: sa1111_dma_playback failed: dma_buffer->size = 0.\n");
		return -EINVAL;		
	}
	
	if (dma_channels[SA1111_SAC_XMT_CHANNEL].running) {
		printk(KERN_ERR "sacdma: sa1111_dma_playback failed: SA1111_SAC_XMT_CHANNEL DMA already running.\n");
		return -EINVAL;
	}

	// spin_lock_irqsave(&sachip->lock, flags);

	dma_channels[SA1111_SAC_XMT_CHANNEL].callback = callback;
	dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer = dma_buffer;
	dma_channels[SA1111_SAC_XMT_CHANNEL].running = 1;

	err = start_sa1111_sac_dma(devptr, 
				dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->dma_ptr, 
				dma_channels[SA1111_SAC_XMT_CHANNEL].dma_buffer->period_size, 
				SA1111_SAC_XMT_CHANNEL);
	
	if (err<0) {
		printk(KERN_ERR "sacdma: sa1111_dma_playback failed: start_sa1111_sac_dma() returned error.\n");
		dma_channels[SA1111_SAC_XMT_CHANNEL].running=0;
		stop_sa1111_sac_dma(devptr, SA1111_SAC_XMT_CHANNEL);
	}

	// spin_unlock_irqrestore(&sachip->lock, flags);
	return err;
}

/* Stop playback, will however complete current period */
int sa1111_dma_playstop(struct sa1111_dev *devptr, dma_buf_t *dma_buffer) {
	struct sa1111 *sachip = get_sa1111_base_drv(devptr);
	unsigned long flags;
	spin_lock_irqsave(&sachip->lock, flags);

	stop_sa1111_sac_dma(devptr, SA1111_SAC_XMT_CHANNEL);
	
	// Wait to finish
	int timeout=0;
	while (!is_done_sa1111_sac_dma(devptr, SA1111_SAC_XMT_CHANNEL) && timeout<1000) {
		udelay(10);
	}

	spin_unlock_irqrestore(&sachip->lock, flags);
	return 0;
}