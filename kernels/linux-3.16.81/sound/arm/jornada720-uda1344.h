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
#ifndef JORNADA720_UDA1344_H
#define JORNADA720_UDA1344_H

#include <asm/hardware/sa1111.h>

#define UDA1344_MAX_VOLUME 0
#define UDA1344_MIN_VOLUME -63

#define UDA1344_MAX_BASS 15
#define UDA1344_MIN_BASS 0

#define UDA1344_MAX_TREBLE 3
#define UDA1344_MIN_TREBLE 0

#define UDA1344_MIN_DEEMP 0
#define UDA1344_MAX_DEEMP 3

#define UDA1344_MUTE_OFF 0
#define UDA1344_MUTE_ON  1

#define UDA1344_DCFILT_OFF 0
#define UDA1344_DCFILT_ON  1

#define UDA1344_DSP_FLAT 0
#define UDA1344_DSP_MIN  1
#define UDA1344_DSP_MAX  3

/* UDA134x L3 address and command types */
#define UDA1344_L3ADDR		0x05
#define UDA1344_DATA		(UDA1344_L3ADDR << 2 | 0)
#define UDA1344_STATUS		(UDA1344_L3ADDR << 2 | 2)

struct uda1344_regs {
	unsigned char	stat0;
#define STAT0			0x00
#define STAT0_SC_MASK		(3 << 4)  // System clock
#define STAT0_SC_512FS		(0 << 4)  // Systemclock 512/s
#define STAT0_SC_384FS		(1 << 4)  // Systemclock 384f/s
#define STAT0_SC_256FS		(2 << 4)  // Systemclock 256f/s
#define STAT0_SC_UNUSED		(3 << 4)  // Systemclock unused
#define STAT0_IF_MASK		(7 << 1)
#define STAT0_IF_I2S		(0 << 1)  // Data format I2S
#define STAT0_IF_LSB16		(1 << 1)  // Data LSB justified 16bit
#define STAT0_IF_LSB18		(2 << 1)  // Data LSB justified 18bit
#define STAT0_IF_LSB20		(3 << 1)  // Data LSB justified 20bit
#define STAT0_IF_MSB		(4 << 1)  // Data MSB justified
#define STAT0_IF_LSB16MSB	(5 << 1)  // Data MSB justified 16bit
#define STAT0_IF_LSB18MSB	(6 << 1)  // Data MSB justified 18bit
#define STAT0_IF_LSB20MSB	(7 << 1)  // Data MSB justified 20bit
#define STAT0_DC_FILTER		(1 << 0)  // Enable DC filter
	unsigned char	data0_0;
#define DATA0			0x00
#define DATA0_VOLUME_MASK	0x3f
#define DATA0_VOLUME(x)		(x)
	unsigned char	data0_1;
#define DATA1			0x40
#define DATA1_BASS(x)		((x) << 2)
#define DATA1_BASS_MASK		(15 << 2)
#define DATA1_TREBLE(x)		((x))
#define DATA1_TREBLE_MASK	(3)
	unsigned char	data0_2;
#define DATA2			0x80
#define DATA2_DEEMP_NONE	(0 << 3)
#define DATA2_DEEMP_32KHz	(1 << 3)
#define DATA2_DEEMP_44KHz	(2 << 3)
#define DATA2_DEEMP_48KHz	(3 << 3)
#define DATA2_MUTE			(1 << 2)
#define DATA2_FILTER_FLAT	(0 << 0)
#define DATA2_FILTER_MIN	(1 << 0)
#define DATA2_FILTER_MAX	(3 << 0)
	unsigned char	data0_3;
#define DATA3			0xc0
#define DATA3_POWER_OFF		(0 << 0)
#define DATA3_POWER_DAC		(1 << 0)
#define DATA3_POWER_ADC		(2 << 0)
#define DATA3_POWER_ON		(3 << 0)
};


struct uda1344 {
	struct uda1344_regs regs;
	int		        active;
	unsigned short	volume;
	unsigned short	bass;
	unsigned short	treble;
	unsigned short	mute;
	unsigned short	deemp_mode;
	unsigned short	dsp_mode;
	long			samplerate;
	unsigned short  dirty_flags;
#define UDA_STATUS_DIRTY       (1 << 0)	
#define UDA_VOLUME_DIRTY       (1 << 1)
#define UDA_BASS_TREBLE_DIRTY  (1 << 2)
#define UDA_FILTERS_MUTE_DIRTY (1 << 3)
#define UDA_POWER_DIRTY        (1 << 4)
};

/* Get a reference to the uda_1344 chip singleton */
extern struct uda1344* uda1344_instance(void);

/* Open (initilize) the UDA 1344 codec */
extern int uda1344_open(struct sa1111_dev *devptr);
/* Close (shutdown) the UDA 1344 codec */
extern void uda1344_close(struct sa1111_dev *devptr);

/* Set the samplerate for the UDA 1344 codec */
extern void uda1344_set_samplerate(struct sa1111_dev *devptr, long rate);

/* Set the volume for UDA1344 codec (range -63 ... 0) */
extern void uda1344_set_volume(struct sa1111_dev *devptr, int volume);
extern int uda1344_get_volume(struct sa1111_dev *devptr);

/* Set the mute for UDA1344 codec (1=mute, 0=unmute) */
extern void uda1344_set_mute(struct sa1111_dev *devptr, int mute);
extern int uda1344_get_mute(struct sa1111_dev *devptr);

/* Set the bass for UDA1344 codec (0 ... 15) */
extern void uda1344_set_bass(struct sa1111_dev *devptr, int treble);
extern int uda1344_get_bass(struct sa1111_dev *devptr);

/* Set the treble for UDA1344 codec  (0..3) */
extern void uda1344_set_treble(struct sa1111_dev *devptr, int treble);
extern int uda1344_get_treble(struct sa1111_dev *devptr);

/* Set the dsp level for UDA1344 codec  (0..3) */
extern void uda1344_set_dsp(struct sa1111_dev *devptr, int dsp);
extern int uda1344_get_dsp(struct sa1111_dev *devptr);

/* Set the deemphasis level for UDA1344 codec  (0..3) */
extern void uda1344_set_deemp(struct sa1111_dev *devptr, int de_emp);
extern int uda1344_get_deemp(struct sa1111_dev *devptr);

// From top ifndef
#endif