/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include "hisi_sensor.h"
#include "../cci/hisi_cci.h"
#include "sensor_common.h"

#define TRY_TIMES		5

#define DIGITAL_GAIN_CALC_BASE 0x1000
#define DIGITAL_GAIN_BASE 0x100

typedef struct {
	u32 rgain;
	u32 ggain;
	u32 bgain;
} awb_gain_t;

awb_gain_t dgain_base = {
	DIGITAL_GAIN_BASE, DIGITAL_GAIN_BASE, DIGITAL_GAIN_BASE};

#define IMX214_OTP_FEATURE	1
//#define IMX214_OTP_CHECKSUM	1

#ifdef IMX214_OTP_FEATURE
#define MODULE_ID_OTP_SLAVE_ADDR	0xA0
#define WB_OTP_SLAVE_ADDR	0xA0
#define AF_OTP_SLAVE_ADDR	0xAC

/* lsc otp i2c slave address */
#define LSC_OTP_SLAVE_ADDR0	0xA0
#define LSC_OTP_SLAVE_ADDR1	0xA4
#define LSC_OTP_SLAVE_ADDR2	0xA6
#define LSC_OTP_SLAVE_ADDR3	0xA8
#define LSC_OTP_SLAVE_ADDR4	0xAA

/* lsc otp segment begin and end position */
#define LSC_OTP_BEGIN0		0x0D
#define LSC_OTP_END0		0xFF
#define LSC_OTP_BEGIN1		0x00
#define LSC_OTP_END1		0xFF
#define LSC_OTP_BEGIN2		0x00
#define LSC_OTP_END2		0xFF
#define LSC_OTP_BEGIN3		0x00
#define LSC_OTP_END3		0xFF
#define LSC_OTP_BEGIN4		0x00
#define LSC_OTP_END4		0xBD

/* use for otp check sum */
unsigned int otp_sum=0;

typedef enum {
	IMX214_SUNNY = 0,
	IMX214_FOXCONN,
	IMX214_MAX,
} IMX214_MANUFACTURER;

awb_gain_t awb_gain_typical[IMX214_MAX] = {
	{0xc02, 0x6ad, 0xa93}, //sunny
	{0xe56, 0x785, 0xb2a}, //foxconn
	//{0xd70, 0x6ec, 0xa8e}, //foxconn k3 typical, just for test
};
#endif

#define IMX214_GROUP_HOLD_REG				0x0104
#define IMX214_MAX_ANALOG_GAIN				8
#define IMX214_ANA_GAIN_GLOBAL_H			0x0204
#define IMX214_ANA_GAIN_GLOBAL_L			0x0205 //analogue_gain_code_global
#define IMX214_DIG_GAIN_GR_H				0x020E //digital_gain_greenR
#define IMX214_DIG_GAIN_GR_L				0x020F
#define IMX214_DIG_GAIN_R_H					0x0210 //digital_gain_red
#define IMX214_DIG_GAIN_R_L					0x0211
#define IMX214_DIG_GAIN_B_H					0x0212 //digital_gain_blue
#define IMX214_DIG_GAIN_B_L					0x0213
#define IMX214_DIG_GAIN_GB_H				0x0214 //digital_gain_greenB
#define IMX214_DIG_GAIN_GB_L				0x0215
#define IMX214_EXPOSURE_REG_H				0x0202
#define IMX214_EXPOSURE_REG_L				0x0203
#define IMX214_VTS_H				0x0340
#define IMX214_VTS_L				0x0341
#define IMX214_HTS_H				0x0342
#define IMX214_HTS_L				0x0343

struct sensor_power_setting imx214_power_setting[] = {
	{
		.seq_type = SENSOR_AVDD,
		.data = (void*)"main-sensor-avdd",
		.config_val = LDO_VOLTAGE_2P8V,
		.sensor_index = SENSOR_INDEX_INVALID,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_IOVDD,
		.data = (void*)"common-iovdd",
		.config_val = LDO_VOLTAGE_1P8V,
		.sensor_index = SENSOR_INDEX_INVALID,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_DVDD,
		.config_val = LDO_VOLTAGE_1P05V,
		.sensor_index = SENSOR_INDEX_INVALID,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_VCM_AVDD,
		.data = (void*)"cameravcm-vcc",
		.config_val = LDO_VOLTAGE_2P8V,
		.sensor_index = SENSOR_INDEX_INVALID,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_AVDD2,
		.data = (void*)"slave-sensor-avdd",
		.config_val = LDO_VOLTAGE_2P8V,
		.sensor_index = SENSOR_INDEX_INVALID,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_MCLK,
		.sensor_index = SENSOR_INDEX_INVALID,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_VCM_PWDN,
		.config_val = SENSOR_GPIO_LOW,
		.sensor_index = SENSOR_INDEX_INVALID,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_RST,
		.config_val = SENSOR_GPIO_LOW,
		.sensor_index = SENSOR_INDEX_INVALID,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_I2C,
		.sensor_index = SENSOR_INDEX_INVALID,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_SUSPEND,
		.config_val = SENSOR_GPIO_HIGH,
		.sensor_index = SENSOR_INDEX_INVALID,
	},
};

#ifdef IMX214_OTP_FEATURE
int imx214otp_read_reg(struct hisi_sensor_ctrl_t *s_ctrl, u16 reg, u8 *val)
{
	i2c_t *i2c_info = &s_ctrl->sensor->sensor_info->otp_i2c_config;
	int rc = 0;
	rc = isp_read_sensor_byte(i2c_info, reg, (u16*)val);
	otp_sum += (u8)(*val);
	return rc;
}

static int imx214_init_otp_i2c_info(struct hisi_sensor_ctrl_t *s_ctrl)
{
	struct hisi_sensor_info *sensor_info = s_ctrl->sensor->sensor_info;

	sensor_info->otp_i2c_config.index = sensor_info->i2c_config.index;
	sensor_info->otp_i2c_config.speed = sensor_info->i2c_config.speed;
	sensor_info->otp_i2c_config.addr_bits = I2C_8BIT;
	sensor_info->otp_i2c_config.val_bits = I2C_8BIT;
	return 0;
}

#ifdef IMX214_OTP_CHECKSUM
static int imx214_read_otp_id(struct hisi_sensor_ctrl_t *s_ctrl)
{
	u8 year, month, date, code, version;

	s_ctrl->sensor->sensor_info->otp_i2c_config.addr = MODULE_ID_OTP_SLAVE_ADDR;

	imx214otp_read_reg(s_ctrl, 0x00, &year);
	imx214otp_read_reg(s_ctrl, 0x01, &month);
	imx214otp_read_reg(s_ctrl, 0x02, &date);
	imx214otp_read_reg(s_ctrl, 0x03, &code);
	imx214otp_read_reg(s_ctrl, 0x04, &version);

	cam_info("%s year=0x%x, month=0x%x, date=0x%x, code=0x%x, version=0x%x",
		__func__, year, month, date, code, version);
	return 0;
}
#endif

static int imx214_read_otp_wb(struct hisi_sensor_ctrl_t *s_ctrl)
{
	struct hisi_sensor_awb_otp *awb_otp = &s_ctrl->sensor->sensor_otp.awb_otp;
	u8 r_gain_l=0, r_gain_h=0, g_gain_l=0, g_gain_h=0, b_gain_l=0, b_gain_h=0;
	u8 iso_h=0, iso_l=0;
	unsigned int camif_id = s_ctrl->sensor->sensor_info->camif_id;

	s_ctrl->sensor->sensor_info->otp_i2c_config.addr = WB_OTP_SLAVE_ADDR;

	imx214otp_read_reg(s_ctrl, 0x05, &iso_h);
	imx214otp_read_reg(s_ctrl, 0x06, &iso_l);
	imx214otp_read_reg(s_ctrl, 0x07, &r_gain_h);
	imx214otp_read_reg(s_ctrl, 0x08, &r_gain_l);
	imx214otp_read_reg(s_ctrl, 0x09, &g_gain_h);
	imx214otp_read_reg(s_ctrl, 0x0a, &g_gain_l);
	imx214otp_read_reg(s_ctrl, 0x0b, &b_gain_h);
	imx214otp_read_reg(s_ctrl, 0x0c, &b_gain_l);

	awb_otp->rgain = (r_gain_h << 8) + r_gain_l;
	awb_otp->ggain = (g_gain_h << 8) + g_gain_l;
	awb_otp->bgain = (b_gain_h << 8) + b_gain_l;

	cam_notice("%s r_gain 0x%x, g_gain 0x%x, b_gain=0x%x",
		__func__, awb_otp->rgain, awb_otp->ggain, awb_otp->bgain);

	/* because awb otp will apply on sensor, upload value will be golden */
	awb_otp->rg_h = (awb_gain_typical[camif_id].rgain >> 8) & 0xff;
	awb_otp->rg_l = awb_gain_typical[camif_id].rgain & 0xff;
	awb_otp->bg_h = (awb_gain_typical[camif_id].bgain >> 8) & 0xff;
	awb_otp->bg_l = awb_gain_typical[camif_id].bgain & 0xff;
	awb_otp->gbgr_h = (awb_gain_typical[camif_id].ggain >> 8) & 0xff;
	awb_otp->gbgr_l = awb_gain_typical[camif_id].ggain & 0xff;;

	awb_otp->awb_otp_succeed = 1;
	return 0;
}

static int imx214_read_otp_af(struct hisi_sensor_ctrl_t *s_ctrl)
{
	struct hisi_sensor_af_otp *af_otp = &s_ctrl->sensor->sensor_otp.af_otp;
	u8 start_code_h = 0, start_code_l = 0, end_code_h = 0, end_code_l = 0;

	s_ctrl->sensor->sensor_info->otp_i2c_config.addr = AF_OTP_SLAVE_ADDR;

	imx214otp_read_reg(s_ctrl, 0x00, &start_code_h);
	imx214otp_read_reg(s_ctrl, 0x01, &start_code_l);
	imx214otp_read_reg(s_ctrl, 0x02, &end_code_h);
	imx214otp_read_reg(s_ctrl, 0x03, &end_code_l);

	cam_info("%s start_code_h=0x%x, start_code_l=0x%x, end_code_h=0x%x, end_code_l=0x%x",
		__func__, start_code_h, start_code_l, end_code_h, end_code_l);

	af_otp->start_code = ((start_code_h<<8) | start_code_l);
	af_otp->end_code = ((end_code_h<<8) | end_code_l);

	if ((af_otp->start_code == 0xffff) || (af_otp->end_code == 0xffff)
		|| (af_otp->end_code == 0x0)) {
		af_otp->af_otp_succeed = 0;
	} else {
		af_otp->af_otp_succeed = 1;
	}

	return 0;
}

#ifdef IMX214_OTP_CHECKSUM
static int imx214_read_otp_lsc(struct hisi_sensor_ctrl_t *s_ctrl)
{
	int reg;
	u8 val;

	s_ctrl->sensor->sensor_info->otp_i2c_config.addr = LSC_OTP_SLAVE_ADDR0;
	for (reg=LSC_OTP_BEGIN0; reg<=LSC_OTP_END0; reg++) {
		imx214otp_read_reg(s_ctrl, reg, &val);
	}

	s_ctrl->sensor->sensor_info->otp_i2c_config.addr = LSC_OTP_SLAVE_ADDR1;
	for (reg=LSC_OTP_BEGIN1; reg<=LSC_OTP_END1; reg++) {
		imx214otp_read_reg(s_ctrl, reg, &val);
	}

	s_ctrl->sensor->sensor_info->otp_i2c_config.addr = LSC_OTP_SLAVE_ADDR2;
	for (reg=LSC_OTP_BEGIN2; reg<=LSC_OTP_END2; reg++) {
		imx214otp_read_reg(s_ctrl, reg, &val);
	}

	s_ctrl->sensor->sensor_info->otp_i2c_config.addr = LSC_OTP_SLAVE_ADDR3;
	for (reg=LSC_OTP_BEGIN3; reg<=LSC_OTP_END3; reg++) {
		imx214otp_read_reg(s_ctrl, reg, &val);
	}

	s_ctrl->sensor->sensor_info->otp_i2c_config.addr = LSC_OTP_SLAVE_ADDR4;
	for (reg=LSC_OTP_BEGIN4; reg<=LSC_OTP_END4; reg++) {
		imx214otp_read_reg(s_ctrl, reg, &val);
	}
	return 0;
}
#endif

#ifdef IMX214_OTP_CHECKSUM
static int imx214_otp_checksum(struct hisi_sensor_ctrl_t *s_ctrl)
{
	u8 otp_check_sum;
	u8 calc_check_sum;
	int rc = 0;

	/* calc otp data check sum */
	calc_check_sum = (otp_sum % 255) + 1;

	/* read check sum from otp */
	s_ctrl->sensor->sensor_info->otp_i2c_config.addr = AF_OTP_SLAVE_ADDR;
	imx214otp_read_reg(s_ctrl, 0x04, &otp_check_sum);
	cam_info("%s otp check sum=0x%x.", __func__, otp_check_sum);

	/* check sum */
	if (calc_check_sum != otp_check_sum) {
		cam_err("%s failed to check sum(sum[0x%x] != checksum[0x%x]).",
			__func__, calc_check_sum, otp_check_sum);
		rc = -1;
	} else {
		cam_info("%s succeed to check sum.", __func__);
		rc = 0;
	}

	return rc;
}
#endif

#ifdef IMX214_OTP_CHECKSUM
void imx214_clean_otp_flag(struct hisi_sensor_ctrl_t *s_ctrl)
{
	struct hisi_sensor_otp *sensor_otp = &s_ctrl->sensor->sensor_otp;

	sensor_otp->awb_otp.awb_otp_succeed = 0;
	sensor_otp->af_otp.af_otp_succeed = 0;
}
#endif

static int imx214_read_otp(struct hisi_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;

	cam_info("%s enter.", __func__);

	imx214_init_otp_i2c_info(s_ctrl);

#ifdef IMX214_OTP_CHECKSUM
	imx214_read_otp_id(s_ctrl);

	imx214_read_otp_lsc(s_ctrl);
#endif
	imx214_read_otp_wb(s_ctrl);

	imx214_read_otp_af(s_ctrl);

#ifdef IMX214_OTP_CHECKSUM
	rc = imx214_otp_checksum(s_ctrl);
	if (rc < 0) {
		cam_err("%s failed check sum for otp.", __func__);
		imx214_clean_otp_flag(s_ctrl);
	}
#endif
	cam_info("%s exit.", __func__);
	return rc;
}

static int imx214_get_otp_af(struct hisi_sensor_ctrl_t *s_ctrl, void *data)
{
	struct sensor_cfg_data *cdata = (struct sensor_cfg_data *)data;
	int rc = 0;

	if (s_ctrl->sensor->sensor_otp.af_otp.af_otp_succeed) {
		memcpy(&cdata->cfg.af_otp, &s_ctrl->sensor->sensor_otp.af_otp,
			sizeof(struct hisi_sensor_af_otp));
		rc = 0;
	} else {
		cam_err("%s failed to get otp af.", __func__);
		memset(&cdata->cfg.af_otp, 0, sizeof(struct hisi_sensor_af_otp));
		rc = -1;
	}

	return rc;
}

static int imx214_calc_digital_gain_base(struct hisi_sensor_ctrl_t *s_ctrl, awb_gain_t *dgain)
{
	struct hisi_sensor_awb_otp *awb_otp = &(s_ctrl->sensor->sensor_otp.awb_otp);
	unsigned int camif_id = s_ctrl->sensor->sensor_info->camif_id;
	unsigned int rg_ratio, bg_ratio;
	unsigned int rg_ratio_typical, bg_ratio_typical;

	if (camif_id >= IMX214_MAX) {
		cam_err("%s camif_id %d error.", __func__, camif_id);
		return -1;
	}

	if(0 == awb_otp->rgain || 0 == awb_otp->ggain || 0 == awb_otp->bgain) {
		cam_err("%s otp value error.", __func__);
		return -1;
	}

	rg_ratio = DIGITAL_GAIN_CALC_BASE * awb_otp->rgain / awb_otp->ggain;
	bg_ratio = DIGITAL_GAIN_CALC_BASE * awb_otp->bgain / awb_otp->ggain;

	rg_ratio_typical = DIGITAL_GAIN_CALC_BASE * awb_gain_typical[camif_id].rgain
		/ awb_gain_typical[camif_id].ggain;
	bg_ratio_typical = DIGITAL_GAIN_CALC_BASE * awb_gain_typical[camif_id].bgain
		/ awb_gain_typical[camif_id].ggain;

	/* set init value */
	dgain->rgain = DIGITAL_GAIN_CALC_BASE;
	dgain->ggain = DIGITAL_GAIN_CALC_BASE;
	dgain->bgain = DIGITAL_GAIN_CALC_BASE;

	if (rg_ratio > rg_ratio_typical && bg_ratio > bg_ratio_typical) {
		dgain->ggain = DIGITAL_GAIN_CALC_BASE;
		dgain->rgain = DIGITAL_GAIN_CALC_BASE * rg_ratio / rg_ratio_typical;
		dgain->bgain = DIGITAL_GAIN_CALC_BASE * bg_ratio / bg_ratio_typical;
	} else {
		/* select smaller divider as base 0x100 */
		if (rg_ratio * bg_ratio_typical < bg_ratio * rg_ratio_typical) {
			dgain->rgain = DIGITAL_GAIN_CALC_BASE;
			dgain->ggain = DIGITAL_GAIN_CALC_BASE * rg_ratio_typical / rg_ratio;
			dgain->bgain = dgain->ggain * bg_ratio / bg_ratio_typical;
		} else {
			dgain->bgain = DIGITAL_GAIN_CALC_BASE;
			dgain->ggain = DIGITAL_GAIN_CALC_BASE * bg_ratio_typical / bg_ratio;
			dgain->rgain = dgain->ggain * rg_ratio / rg_ratio_typical;
		}
	}

	/* change CALC_BASE to BASE for more accurate */
	dgain->rgain = (dgain->rgain + 0x8) / 0x10;
	dgain->ggain = (dgain->ggain + 0x8) / 0x10;
	dgain->bgain = (dgain->bgain + 0x8) / 0x10;

	cam_notice("%s RGB otp[0x%x,0x%x,0x%x],dgain_base[0x%x,0x%x,0x%x]",
		__func__, awb_otp->rgain, awb_otp->ggain, awb_otp->bgain,
		dgain->rgain, dgain->ggain, dgain->bgain);

	return 0;
}
#endif

int imx214_match_id(struct hisi_sensor_ctrl_t *s_ctrl)
{
	int ret = 0;
	u16 id_h = 0;
	u16 id_l = 0;
	u16 sensor_id = 0;
	int camif_id = -1;
	struct hisi_sensor_info *sensor_info = s_ctrl->sensor->sensor_info;
	int try = 0;

	cam_info( "%s, sensor_chipid:0x%x\n",
		__func__, sensor_info->sensor_chipid);

	ret = hisi_sensor_power_up(s_ctrl);
	if(ret) {
		cam_err("sensor[%s] power up failed.", sensor_info->name);
		ret = -1;
		goto out;
	}

	while(try ++ < TRY_TIMES) {
		camdrv_msleep(1);
		/* check sensor id */
		isp_read_sensor_byte(&sensor_info->i2c_config, 0x0016, &id_h);
		isp_read_sensor_byte(&sensor_info->i2c_config, 0x0017, &id_l);

		sensor_id = id_h << 8 | id_l;

		cam_notice( "sensor id:  0x%x", sensor_id);
		if (sensor_id == sensor_info->sensor_chipid) {
			break;
		}
	}

	cam_notice("try times: %d", try);

	if(try >= TRY_TIMES) {
		goto out;
	}

	if(0 == sensor_info->gpios[FSIN].gpio) {
		cam_err("%s gpio type[FSIN] is not actived.", __func__);
		ret = -1;
		goto out;
	}

	ret = gpio_request(sensor_info->gpios[FSIN].gpio, "camif_id");
	if(ret < 0) {
		cam_err("failed to request gpio[%d]", sensor_info->gpios[FSIN].gpio);
		goto out;
	}
	ret = gpio_direction_input(sensor_info->gpios[FSIN].gpio);
	if(ret < 0) {
		cam_err("failed to control gpio[%d]", sensor_info->gpios[FSIN].gpio);
		goto out_gpio;
	}

	ret = gpio_get_value(sensor_info->gpios[FSIN].gpio);
	if(ret < 0) {
		cam_err("failed to get gpio[%d]", sensor_info->gpios[FSIN].gpio);
		goto out_gpio;
	} else {
		camif_id = ret;
		cam_notice("%s camif id = %d.", __func__, camif_id);
	}

	if (camif_id != sensor_info->camif_id) {
		cam_notice("%s camera[%s] module is not match.", __func__, sensor_info->name);
		ret = -1;
	} else {
		cam_notice("%s camera[%s] match successfully.", __func__, sensor_info->name);
		ret = 0;
	}

	if (0 == ret) {
		#ifdef IMX214_OTP_FEATURE
		imx214_read_otp(s_ctrl);
		imx214_calc_digital_gain_base(s_ctrl, &dgain_base);
		#endif
	}

out_gpio:
	gpio_free(sensor_info->gpios[FSIN].gpio);
out:
	hisi_sensor_power_down(s_ctrl);
	return ret;
}

int imx214_ioctl(struct hisi_sensor_ctrl_t *s_ctrl, void *data)
{
	struct sensor_cfg_data *cdata = (struct sensor_cfg_data*)data;
	int   rc = 0;

	cam_debug("%s enter.\n", __func__);

	switch (cdata->cfgtype) {
	case CFG_SENSOR_SET_VTS:
		cam_info("%s set vts.\n", __func__);
		break;
	case CFG_SENSOR_GET_OTP_AWB:
		memcpy(&cdata->cfg.awb_otp, &s_ctrl->sensor->sensor_otp.awb_otp,
			sizeof(struct hisi_sensor_awb_otp));
		break;
	case CFG_SENSOR_GET_OTP_VCM:
		#ifdef IMX214_OTP_FEATURE
		rc = imx214_get_otp_af(s_ctrl, data);
		#endif
		break;
	case CFG_SENSOR_GET_MIRROR_FLIP:
		cdata->data = s_ctrl->sensor->sensor_info->mirror_flip_disable;
		break;

	default:
		rc = -EFAULT;
		break;
	}

	return rc;
}

int imx214_set_expo_gain(struct hisi_sensor_t *sensor,
	u32 expo, u16 gain, bool stream_off_mode)
{
	int rc = 0;
	u32 analog_gain = 0;
	u32 digital_gain = 0x100;
	u32 digital_gain_r, digital_gain_g, digital_gain_b;
	int wait = SCCB_BUS_MUTEX_NOWAIT;

	if (stream_off_mode == true)
		isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
			0x100, 0x00, 0x00, wait);

	rc = isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_GROUP_HOLD_REG, 0x01, 0x00, wait);
	if (rc < 0) {
		return rc;
	}

	expo >>= 4;
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_EXPOSURE_REG_H, (expo >> 8) & 0xff, 0x00, wait);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_EXPOSURE_REG_L, expo & 0xff, 0x00, wait);

	if (gain == 0)
		goto out;
	/*
	 * gain = (analog_gain * digital_gain)
	 * digital_gain = digital_h + digital_l / 256;
	 */
	if (gain > IMX214_MAX_ANALOG_GAIN * 16) {
		analog_gain = IMX214_MAX_ANALOG_GAIN * 16;
		digital_gain = (gain * 0x100) / analog_gain;
	} else {
		analog_gain = gain;
		digital_gain = 0x100;
	}

	/* re-calculate digital gain */
	digital_gain_r = digital_gain * dgain_base.rgain / 0x100;
	digital_gain_g = digital_gain * dgain_base.ggain / 0x100;
	digital_gain_b = digital_gain * dgain_base.bgain / 0x100;

	/* write analog gain */
	analog_gain = 512 - (512 * 16) / analog_gain;
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_ANA_GAIN_GLOBAL_H, (analog_gain >> 8) & 0xff, 0x00, wait);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_ANA_GAIN_GLOBAL_L, analog_gain & 0xff, 0x00, wait);

	/* write digital gain */
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_DIG_GAIN_GR_H, (digital_gain_g >> 8) & 0xff, 0x00, wait);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_DIG_GAIN_GR_L, digital_gain_g & 0xff, 0x00, wait);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_DIG_GAIN_R_H, (digital_gain_r >> 8) & 0xff, 0x00, wait);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_DIG_GAIN_R_L, digital_gain_r & 0xff, 0x00, wait);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_DIG_GAIN_B_H, (digital_gain_b >> 8) & 0xff, 0x00, wait);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_DIG_GAIN_B_L, digital_gain_b & 0xff, 0x00, wait);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_DIG_GAIN_GB_H, (digital_gain_g >> 8) & 0xff, 0x00, wait);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_DIG_GAIN_GB_L, digital_gain_g & 0xff, 0x00, wait);

out:
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_GROUP_HOLD_REG, 0x00, 0x00, wait);//group hold

	if (stream_off_mode == true)
		isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
			0x100, 0x01, 0x00, wait);

	return rc;
}

int imx214_set_hts(struct hisi_sensor_t *sensor, u16 hts)
{
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_HTS_H, (hts >> 8) & 0xff, 0x00, SCCB_BUS_MUTEX_WAIT);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_HTS_L, hts & 0xff, 0x00, SCCB_BUS_MUTEX_WAIT);

	return 0;
}

int imx214_set_vts(struct hisi_sensor_t *sensor, u16 vts)
{
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_VTS_H, (vts >> 8) & 0xff, 0x00, SCCB_BUS_MUTEX_WAIT);
	isp_write_sensor_byte(&sensor->sensor_info->i2c_config,
		IMX214_VTS_L, vts & 0xff, 0x00, SCCB_BUS_MUTEX_WAIT);

	return 0;
}

struct hisi_sensor_fn_t imx214_func_tbl = {
	.sensor_config = hisi_sensor_config,
	.sensor_power_up = hisi_sensor_power_up,
	.sensor_power_down = hisi_sensor_power_down,
	.sensor_i2c_read = hisi_sensor_i2c_read,
	.sensor_i2c_write = hisi_sensor_i2c_write,
	.sensor_i2c_read_seq = hisi_sensor_i2c_read_seq,
	.sensor_i2c_write_seq = hisi_sensor_i2c_write_seq,
	.sensor_ioctl = imx214_ioctl,
	.sensor_match_id = imx214_match_id,
	.sensor_set_expo_gain = imx214_set_expo_gain,
	.sensor_apply_expo_gain = hisi_sensor_apply_expo_gain,
	.sensor_suspend_eg_task = hisi_sensor_suspend_eg_task,
	.sensor_set_hts = imx214_set_hts,
	.sensor_set_vts = imx214_set_vts,
};

static int32_t imx214_sensor_probe(struct platform_device *pdev)
{
	int32_t rc = -1;
	struct hisi_sensor_t *sensor = NULL;

	cam_info("%s pdev name %s\n", __func__, pdev->name);

	sensor = (struct hisi_sensor_t*)kzalloc(sizeof(struct hisi_sensor_t),
						GFP_KERNEL);
	if (NULL == sensor) {
		cam_err("%s failed line %d\n", __func__, __LINE__);
		return -ENOMEM;
	}

	sensor->func_tbl = &imx214_func_tbl;
	sensor->power_setting_array.power_setting = imx214_power_setting;
	sensor->power_setting_array.size = ARRAY_SIZE(imx214_power_setting);

	if (pdev->dev.of_node) {
		rc = hisi_sensor_get_dt_data(pdev, sensor);
		if (rc < 0) {
			cam_err("%s failed line %d\n", __func__, __LINE__);
			goto imx214_sensor_probe_fail;
		}
	} else {
		cam_err("%s imx214 of_node is NULL.\n", __func__);
		goto imx214_sensor_probe_fail;
	}

	rc = hisi_sensor_add(sensor);
	if (rc < 0) {
		cam_err("%s fail to add sensor into sensor array.\n", __func__);
		goto imx214_sensor_probe_fail;
	}
	if (!dev_get_drvdata(&pdev->dev)) {
		dev_set_drvdata(&pdev->dev, (void *)sensor);
	}
	return rc;
imx214_sensor_probe_fail:
	cam_err("%s error exit.\n", __func__);
	kfree(sensor);
	sensor = NULL;
	return rc;
}

static const struct of_device_id hisi_imx214_dt_match[] = {
	{.compatible = "hisi,imx214_sunny"},
	{.compatible = "hisi,imx214_foxconn"},
	{}
};
MODULE_DEVICE_TABLE(of, hisi_imx214_dt_match);
static struct platform_driver imx214_platform_driver = {
	.driver = {
		.name = "imx214",
		.owner = THIS_MODULE,
		.of_match_table = hisi_imx214_dt_match,
	},
};

static int32_t imx214_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	const struct of_device_id *match;

	match = of_match_device(hisi_imx214_dt_match, &pdev->dev);
	cam_notice("%s compatible=%s.\n", __func__, match->compatible);
	rc = imx214_sensor_probe(pdev);
	return rc;
}

static int __init imx214_module_init(void)
{
	int rc = 0;
	cam_debug("%s:%d\n", __func__, __LINE__);

	rc = platform_driver_probe(&imx214_platform_driver,
		imx214_platform_probe);
	if (rc < 0) {
		cam_notice("%s platform_driver_probe error.\n", __func__);
	}
	return rc;
}

static void __exit imx214_module_exit(void)
{
	platform_driver_unregister(&imx214_platform_driver);
}

MODULE_AUTHOR("HISI");
module_init(imx214_module_init);
module_exit(imx214_module_exit);
MODULE_LICENSE("GPL");
