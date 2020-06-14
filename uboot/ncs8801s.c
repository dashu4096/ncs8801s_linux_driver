/*
 * (C) Copyright 2017
 * coder.hans@gmail.com
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#include <asm/arch/rkplat.h>
#include <errno.h>
#include <i2c.h>
#include <lcd.h>
#include <linux/rk_screen.h>
#include <fdtdec.h>
#include <fdt_support.h>

#include "ncs8801s.h"

DECLARE_GLOBAL_DATA_PTR;

#define NCS8801S_I2C_CLOCK 200000

static struct ncs8801s ncs8801s;

static int ncs8801s_write(uchar addr, uchar reg, uchar val) {
	int	retry = 2;
	int	ret = -1;

	while(retry--) {
		if (!i2c_write(addr, reg, 1, &val, 1)) {
			ret = 0;
			break;
		}
		udelay(10);
	}

	if (ret)
		printf("%s: failed to write register %#x\n", __func__, reg);
	return ret;
}

static int ncs8801s_write_list(uchar addr, const struct reg_data *list) {
	int len = 0;
	if (addr == NCS8801S_ID1_ADDR) {
		len = (ncs8801s.screen_w == 1920) ? ID1_1920_1080_REG_LEN : ID1_1366_768_REG_LEN;
	} else if (addr == NCS8801S_ID2_ADDR) {
		len = (ncs8801s.screen_w == 1920) ? ID2_1920_1080_REG_LEN : ID2_1366_768_REG_LEN;
	} else {
		len = ID3_1920_1080_REG_LEN;
	}

	//printf("len:%d\n",len);
	while(len--) {
		if (ncs8801s_write(addr, list->reg, list->val) < 0) {
			printf("ncs8801s write err addr:0x%02x reg:%02x,val:%02x\n",addr,list->reg,list->val);
		}
		list++;
	}
	return 0;
}

static int ncs8801s_parse_dt(const void* blob) {
	int ret;
	int node = fdt_node_offset_by_compatible(blob, 0, "newcosemi,ncs8801s");
	u32 bus, addr;

	if (node < 0) {
		printf("can't find dts node for ncs8801s\n");
		return -ENODEV;
	}

	if (!fdt_device_is_available(blob, node)) {
		printf("nsc8801s is disabled\n");
		return -1;
	}
		
	ncs8801s.screen_w = fdtdec_get_int(blob, node, "screen-w", 1920);
	ncs8801s.screen_h = fdtdec_get_int(blob, node, "screen-h", 1080);
	fdtdec_decode_gpio(blob, node, "rst_gpio", &ncs8801s.rst_pin);
	fdtdec_decode_gpio(blob, node, "pwd_gpio", &ncs8801s.pwd_pin);
	if (gpio_is_valid(ncs8801s.rst_pin.gpio) && gpio_is_valid(ncs8801s.pwd_pin.gpio)) {
		//reset chip
		gpio_direction_output(ncs8801s.pwd_pin.gpio, 0);
		gpio_direction_output(ncs8801s.rst_pin.gpio, 0);
		udelay(60);
		gpio_direction_output(ncs8801s.rst_pin.gpio, 1);
		udelay(60);
	}

	ret = fdt_get_i2c_info(blob, node, &bus, &addr);
	if (ret < 0) {
		printf("ncs8801s get fdt i2c failed\n");
		return ret;
	}
	//printf("ncs8801s bus:%d addr:%x\n", bus, addr);
	//init bus
	i2c_set_bus_num(bus);
	i2c_init(NCS8801S_I2C_CLOCK, 0);
	ret = i2c_probe(addr);
	if (ret < 0)
		return -ENODEV;

	return 0;
}

int ncs8801s_init(void) {
	int ret;

	if (!gd->fdt_blob)
		return -1;
	ret = ncs8801s_parse_dt(gd->fdt_blob);
	if (ret < 0){
		return -1;
	}
	//write registers
	if (ncs8801s.screen_w == 1920 && ncs8801s.screen_h == 1080) {
		ncs8801s_write_list(NCS8801S_ID1_ADDR, id1_1920_1080_regs);
		ncs8801s_write_list(NCS8801S_ID2_ADDR, id2_1920_1080_regs);
		ncs8801s_write(NCS8801S_ID1_ADDR, 0x0f, 0x0);
		ncs8801s_write_list(NCS8801S_ID3_ADDR, id3_1920_1080_regs);
		//B156HTN need
		ncs8801s_write(NCS8801S_ID1_ADDR, 0x71, 0x9);
	} else {
		ncs8801s_write_list(NCS8801S_ID1_ADDR, id1_1366_768_regs);
		ncs8801s_write_list(NCS8801S_ID2_ADDR, id2_1366_768_regs);
		ncs8801s_write(NCS8801S_ID1_ADDR, 0x0f, 0x0);
	}
	return 0;
}
