/*
 *  jornada720-common.h
 *
 *  Common definitions for jornada720 sounddriver
 *
 *  Copyright (C) 2021 Timo Biesenbach
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef JORNADA720_COMMON_H
#define JORNADA720_COMMON_H

// ******** Configuration switches ********
// Startup sound, undef below to disable
#undef STARTUP_CHIME

// Disable sleep ability for now
#undef CONFIG_PM_SLEEP

// Fixed samplerate
// #define RATE_FIXED
#undef RATE_FIXED

// ********* Common constants **********
// One DMA transfer is up to 8kb, 0x800(?) defined in sa1111.h, 8176 (no accurate: 1ffc = (1<<13)-1 ~& 0x03) (max as per intel datasheet)
#define MIN_DMA_BLOCK_SIZE (64)
#define MAX_DMA_BLOCK_SIZE (8176)

#define MIN_NUM_PERIODS (2)
#define MAX_NUM_PERIODS (8)

// Buffer sizes for dma allocation
// #define MIN_BUFFER_SIZE		(16*1024)
// #define MAX_BUFFER_SIZE		(64*1024)

#define MIN_BUFFER_SIZE		(MIN_NUM_PERIODS * MAX_DMA_BLOCK_SIZE)
#define MAX_BUFFER_SIZE		(MAX_NUM_PERIODS * MAX_DMA_BLOCK_SIZE)

#endif