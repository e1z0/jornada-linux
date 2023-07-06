/*
 * arch/arm/mach-sa1100/jornada720_apm.c
 *
 * HP Jornada 710/720/728 battery detection (apm) platform driver
 *
 * Copyright (C) 2007,2008 Kristoffer Ericson <Kristoffer.Ericson@gmail.com>
 *  Copyright (C) 2006 Filip Zyzniewski <filip.zyzniewski@tefnet.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 (or later) as
 * published by the Free Software Foundation.
 */
#include <linux/io.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/apm-emulation.h>
#include <mach/hardware.h>
#include <mach/jornada720.h>
MODULE_AUTHOR("Filip Zyzniewski <filip.zyzniewski@tefnet.pl>, Kristoffer Ericson <kristoffer.ericson@gmail.com>");
MODULE_DESCRIPTION("HP Jornada 710/720/728 battery status reporting");
MODULE_LICENSE("GPL");
/**
 * voltage && detection
 */
#define max_voltage 670					/* max voltage without ac power */
#define min_voltage 430					/* min voltage */
#define ac_connected()   ((GPLR & GPIO_GPIO4) ? 0 : 1)	/* is ac connected? */
#define ac_charging()    (! (GPLR & GPIO_GPIO26) )	/* are we charging? */
/**
 * battery calculations
 */
#define main_diff        (max_voltage - min_voltage)	/* just to keep defines small */
#define main_ac_coeff    100 / 105			/* correcting battery values when with ac */
#define main_divisor     (main_diff * main_diff) / 100	/* battery power to percent */
#define main_lin_corr    (main_diff * main_diff) / 2	/* adjusting for non-linearity */
int jornada720_apm_get_battery_raw(int battnum)
{
	unsigned char low_byte, high_byte, msb;
	jornada_ssp_start();
	/* if ssp connection fails we bail out */
	if(jornada_ssp_inout(GETBATTERYDATA) < 0) {
	    printk(KERN_WARNING "APM: Failed trying to aquire battery data \n");
	    jornada_ssp_end();
	    return -1;
	}
	low_byte  = jornada_ssp_inout(TXDUMMY);		/* backup battery value */
	high_byte = jornada_ssp_inout(TXDUMMY);		/* main battery value */
	msb = jornada_ssp_inout(TXDUMMY);		/* status */
	jornada_ssp_end();
	/* main battery */
	if (battnum) {
		if ((msb & 0x03) == 0x03) return -1;	/* main battery absent */
		return ((msb & 0x03) << 8) + low_byte;	/* wrapping values */
	}
	/* backup battery */
	else {
		if ((msb & 0x0c) == 0x00) return -1;	/* backup battery abset */
		return ((msb & 0x0c) << 6) + high_byte;	/* wrapping values */
	}
}
int jornada720_apm_get_battery(int battnum)
{
	int ret = jornada720_apm_get_battery_raw(battnum);
	if (ret == -1)
		return ret;
	/* main battery only, cannot calculate backup battery */
	if (battnum) {
		ret -= min_voltage;			/* we want 0 for fully drained battery */
		/* trying to get values more linear */
		ret *= ret;
		if (ret > 37000)
		    ret = ret * 3/2 - main_lin_corr;
		else
		    ret = ret * 7/10;
		ret /= main_divisor;			/* 0-100% range */
		if (ac_connected())			/* adjusting for ac fluctuations */
		    ret = ret * main_ac_coeff;
		if (ret > 100) 
		    ret = 100;			/* should never report above 100% */
	}
	return ret;
}
static void jornada720_apm_get_power_status(struct apm_power_info *info)
{
	info->battery_life = jornada720_apm_get_battery(1); /* main battery */
	if (info->battery_life == -1) {
		info->battery_status = APM_BATTERY_STATUS_NOT_PRESENT;
		info->battery_flag = APM_BATTERY_FLAG_NOT_PRESENT;
	} else if (info->battery_life < 30) {
		info->battery_status = APM_BATTERY_STATUS_LOW;
		info->battery_flag = APM_BATTERY_FLAG_LOW;
	} else if (info->battery_life < 5) {
		info->battery_status = APM_BATTERY_STATUS_CRITICAL;
		info->battery_flag = APM_BATTERY_FLAG_CRITICAL;
	} else {
		info->battery_status = APM_BATTERY_STATUS_HIGH;
		info->battery_flag = APM_BATTERY_FLAG_HIGH;
	}
	if (ac_charging())
		info->battery_status = APM_BATTERY_STATUS_CHARGING;
	info->ac_line_status = ac_connected();
}
static int jornada720_apm_probe(struct platform_device *pdev)
{
	printk(KERN_INFO "jornada720_apm: Initializing\n");
	
	apm_get_power_status = jornada720_apm_get_power_status;
	return 0;
}
static int jornada720_apm_remove(struct platform_device *pdev)
{
	if (apm_get_power_status == jornada720_apm_get_power_status)
		apm_get_power_status=NULL;
	return 0;
}
static struct platform_driver jornada720_apm_driver = {
	.driver = {
		.name	= "jornada_apm",
	},
	.probe	= jornada720_apm_probe,
	.remove = jornada720_apm_remove,
};
static int __init jornada720_apm_init(void)
{
	return platform_driver_register(&jornada720_apm_driver);
}
static void __exit jornada720_apm_exit(void) {
	platform_driver_unregister(&jornada720_apm_driver);
}
module_init(jornada720_apm_init);
module_exit(jornada720_apm_exit);