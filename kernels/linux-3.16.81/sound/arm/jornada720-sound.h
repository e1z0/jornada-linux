/*
 *  jornada720-sound.h
 *
 *  Toplevel include file for jornada720 sound driver.
 *
 *  Copyright (C) 2021 Timo Biesenbach
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef JORNADA720_SND_H
#define JORNADA720_SND_H

#define MAX_PCM_DEVICES		1
#define MAX_PCM_SUBSTREAMS	1
#define MAX_MIDI_DEVICES	0

/* Hardware defauls */
#define MIXER_ADDR_MASTER	0
#define MIXER_ADDR_MIC		2
#define MIXER_ADDR_LAST		4

struct jornada720_model {
	const char *name;
	int (*playback_constraints)(struct snd_pcm_runtime *runtime);
	int (*capture_constraints)(struct snd_pcm_runtime *runtime);
	u64 formats;
	size_t buffer_bytes_max;
	size_t period_bytes_min;
	size_t period_bytes_max;
	unsigned int periods_min;
	unsigned int periods_max;
	unsigned int rates;
	unsigned int rate_min;
	unsigned int rate_max;
	unsigned int channels_min;
	unsigned int channels_max;
};

struct snd_jornada720 {
	struct snd_card *card;
	struct jornada720_model *model;
	struct snd_pcm *pcm;
	struct snd_pcm_hardware pcm_hw;
	spinlock_t mixer_lock;
	int mixer_volume[MIXER_ADDR_LAST+1][1];
	int capture_source[MIXER_ADDR_LAST+1][2];
	//HW pointers
	struct uda1344* pchip_uda1344;
	struct sa1111_dev * pdev_sa1111;
	// The PCM substream we're playing
	struct snd_pcm_substream *substream;
};

#endif