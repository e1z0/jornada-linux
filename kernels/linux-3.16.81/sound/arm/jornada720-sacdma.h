/*
 * Definitions shared between dma-sa1100.c and dma-sa1111.c
 * (C) 2000 Nicolas Pitre <nico@cam.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
// #include <linux/config.h>
#ifndef JORNADA720_SACDMA_H
#define JORNADA720_SACDMA_H

#define SA1111_SAC_DMA_CHANNELS 2
#define SA1111_SAC_XMT_CHANNEL  0
#define SA1111_SAC_RCV_CHANNEL  1

#define DMA_REG_RX_OFS 0x14
#define DMA_CH_A   0x00
#define DMA_CH_B   0x08

// See section 7.4 in datasheet
#define SAC_FIFO_RX_THRESHOLD 0x06
#define SAC_FIFO_TX_THRESHOLD 0x06

// callback playback / record state
#define STATE_FINISHED  0    // Playback of given buffer finished
#define STATE_RUNNING	1    // Playback of given buffer running
#define STATE_LOOPING	2    // If in loop mode, indicate on cycle has been completed

typedef unsigned int dma_addr_t;

/* represents a single dma buffer for either recording or capture
 * exposed to the outside */
typedef struct dma_buf_s {
	size_t 		size;		        		/* buffer size */
	size_t 		period_size;				/* Period size, hopefully in range >=4k */
	void*		virt_addr;      			/* virtual buffer address */
	dma_addr_t 	dma_start;	    			/* starting DMA address */
	dma_addr_t 	dma_ptr;		    		/* next DMA pointer to use */
	struct snd_jornada720* snd_jornada720; 	/* jornada720 sounddevice for use in callback */
	bool		loop;						/* Play continously? */
	int			loop_count;					/* # of loops played */
} dma_buf_t;

/* function to call when DMA_BLOCK_SIZE portion of dma_buffer is transferred */
typedef void (*dma_block_callback)(dma_buf_t *dma_buffer, int state);

/* Allocate resources for PCM playback / recording */
extern  int sa1111_dma_alloc(struct sa1111_dev *devptr);

/* Release resources for PCM playback / recording */
extern  int sa1111_dma_release(struct sa1111_dev *devptr);

/* Playback the data from dma_ptr with size bytes on the sa1111 device and call the callback function each DMA_BLOCK_SIZE bytes */
extern  int sa1111_dma_playback(struct sa1111_dev *devptr, dma_buf_t *dma_buffer, dma_block_callback callback);

/* Stop playback on the sa1111 device*/
extern  int sa1111_dma_playstop(struct sa1111_dev *devptr, dma_buf_t *dma_buffer);

/* Record sound into the data buffer from dma_ptr with size bytes on the sa1111 device and call the callback function each DMA_BLOCK_SIZE bytes */
// extern  int sa1111_dma_record(struct sa1111_dev *devptr, dma_buf_t *dma_buffer, dma_block_callback callback);

// From top ifndef
#endif