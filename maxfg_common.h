/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2023 Google, LLC
 *
 * SW Support for MAXFG COMMON
 */

#ifndef MAXFG_COMMON_H_
#define MAXFG_COMMON_H_

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include "gbms_power_supply.h"
#include "google_bms.h"

#define MAX1720X_GAUGE_TYPE	1
#define MAX_M5_GAUGE_TYPE	2

enum maxfg_reg_tags {
	MAXFG_TAG_avgc,
	MAXFG_TAG_cnfg,
	MAXFG_TAG_mmdv,
	MAXFG_TAG_vcel,
	MAXFG_TAG_temp,
	MAXFG_TAG_curr,
	MAXFG_TAG_mcap,
	MAXFG_TAG_avgr,
	MAXFG_TAG_vfsoc,
	MAXFG_TAG_vfocv,
	MAXFG_TAG_tempco,
	MAXFG_TAG_rcomp0,
	MAXFG_TAG_timerh,
	MAXFG_TAG_descap,
	MAXFG_TAG_fcnom,
	MAXFG_TAG_fcrep,
	MAXFG_TAG_msoc,
	MAXFG_TAG_mmdt,
	MAXFG_TAG_mmdc,
	MAXFG_TAG_repsoc,
	MAXFG_TAG_avcap,
	MAXFG_TAG_repcap,
	MAXFG_TAG_fulcap,
	MAXFG_TAG_qh0,
	MAXFG_TAG_qh,
	MAXFG_TAG_dqacc,
	MAXFG_TAG_dpacc,
	MAXFG_TAG_qresd,
	MAXFG_TAG_fstat,
	MAXFG_TAG_learn,
	MAXFG_TAG_filcfg,
	MAXFG_TAG_vfcap,
	MAXFG_TAG_cycles,
	MAXFG_TAG_rslow,

	MAXFG_TAG_BCNT,
	MAXFG_TAG_SNUM,
	MAXFG_TAG_HSTY,
	MAXFG_TAG_BCEA,
	MAXFG_TAG_rset,
	MAXFG_TAG_BRES,
};

enum max17x0x_reg_types {
	GBMS_ATOM_TYPE_MAP = 0,
	GBMS_ATOM_TYPE_REG = 1,
	GBMS_ATOM_TYPE_ZONE = 2,
	GBMS_ATOM_TYPE_SET = 3,
};

#define MAX_HIST_FULLCAP	0x3FF

#pragma pack(1)
struct maxfg_eeprom_history {
	u16 tempco;
	u16 rcomp0;
	u8 timerh;
	unsigned fullcapnom:10;
	unsigned fullcaprep:10;
	unsigned mixsoc:6;
	unsigned vfsoc:6;
	unsigned maxvolt:4;
	unsigned minvolt:4;
	unsigned maxtemp:4;
	unsigned mintemp:4;
	unsigned maxchgcurr:4;
	unsigned maxdischgcurr:4;
};
#pragma pack()

/* Capacity Estimation */
struct gbatt_capacity_estimation {
	const struct maxfg_reg *bcea;
	struct mutex batt_ce_lock;
	struct delayed_work settle_timer;
	int cap_tsettle;
	int cap_filt_length;
	int estimate_state;
	bool cable_in;
	int delta_cc_sum;
	int delta_vfsoc_sum;
	int cap_filter_count;
	int start_cc;
	int start_vfsoc;
};

#define ESTIMATE_DONE		2
#define ESTIMATE_PENDING	1
#define ESTIMATE_NONE		0

#define CE_CAP_FILTER_COUNT	0
#define CE_DELTA_CC_SUM_REG	1
#define CE_DELTA_VFSOC_SUM_REG	2
#define CE_FILTER_COUNT_MAX	15

#define DEFAULT_BATTERY_ID		0
#define DEFAULT_BATTERY_ID_RETRIES	20
#define DUMMY_BATTERY_ID		170

/* this is a map for u16 registers */
#define ATOM_INIT_MAP(...)			\
	.type = GBMS_ATOM_TYPE_MAP,		\
	.size = 2 * sizeof((u8[]){__VA_ARGS__}),\
	.map = (u8[]){__VA_ARGS__}

#define ATOM_INIT_REG16(r)		\
	.type = GBMS_ATOM_TYPE_REG,	\
	.size = 2,			\
	.reg = r

#define ATOM_INIT_ZONE(start, sz)	\
	.type = GBMS_ATOM_TYPE_ZONE,	\
	.size = sz,			\
	.base = start

/* a set has no storage and cannot be used in load/store */
#define ATOM_INIT_SET(...)		\
	.type = GBMS_ATOM_TYPE_SET,	\
	.size = 0,			\
	.map = (u8[]){__VA_ARGS__}

#define ATOM_INIT_SET16(...)		\
	.type = GBMS_ATOM_TYPE_SET,	\
	.size = 0,			\
	.map16 = (u16[]){__VA_ARGS__}

/* multiply by 2 when task period = 351 ms */
static inline int reg_to_micro_amp_h(s16 val, u16 rsense, int lsb)
{
	/* LSB: 5.0μVh/RSENSE ; Rsense LSB is 10μΩ */
	return div_s64((s64) val * 500000, rsense) * lsb;
}

/* divide by 2 when task period = 351 ms */
static inline s16 micro_amp_h_to_reg(int val, u16 rsense, int lsb)
{
	/* LSB: 5.0μVh/RSENSE ; Rsense LSB is 10μΩ */
	return div_s64((s64)(val / lsb) * rsense, 500000);
}

static inline int reg_to_micro_volt(u16 val)
{
	/* LSB: 0.078125mV */
	return div_u64((u64) val * 78125, 1000);
}

static inline int reg_to_resistance_micro_ohms(s16 val, u16 rsense)
{
	/* LSB: 1/4096 Ohm */
	return div_s64((s64) val * 1000 * rsense, 4096);
}

#define NB_REGMAP_MAX 256

struct maxfg_reglog {
	u16 data[NB_REGMAP_MAX];
	DECLARE_BITMAP(valid, NB_REGMAP_MAX);
	int errors[NB_REGMAP_MAX];
	int count[NB_REGMAP_MAX];
};

struct maxfg_reg {
	int type;
	int size;
	union {
		unsigned int base;
		unsigned int reg;
		const u16 *map16;
		const u8 *map;
	};
};

struct maxfg_regtags {
	const struct maxfg_reg *map;
	unsigned int max;
};

struct maxfg_regmap {
	struct regmap *regmap;
	struct maxfg_regtags regtags;
	struct maxfg_reglog *reglog;
};

static inline int maxfg_regmap_read(const struct maxfg_regmap *map,
				    unsigned int reg,
				    u16 *val,
				    const char *name)
{
	int rtn;
	unsigned int tmp;

	if (!map->regmap) {
		pr_err("Failed to read %s, no regmap\n", name);
		return -EIO;
	}

	rtn = regmap_read(map->regmap, reg, &tmp);
	if (rtn)
		pr_err("Failed to read %s\n", name);
	else
		*val = tmp;

	return rtn;
}

#define REGMAP_READ(regmap, what, dst) \
	maxfg_regmap_read(regmap, what, dst, #what)

static inline int maxfg_regmap_write(const struct maxfg_regmap *map,
				     unsigned int reg,
				     u16 data,
				     const char *name)
{
	int rtn;

	if (!map->regmap) {
		pr_err("Failed to write %s, no regmap\n", name);
		return -EIO;
	}

	rtn = regmap_write(map->regmap, reg, data);
	if (rtn)
		pr_err("Failed to write %s\n", name);

#ifdef CONFIG_MAX1720X_REGLOG_LOG
	max17x0x_reglog_log(map->reglog, reg, data, rtn);
#endif
	return rtn;
}

#define REGMAP_WRITE(regmap, what, value) \
	maxfg_regmap_write(regmap, what, value, #what)

#define WAIT_VERIFY	(10 * USEC_PER_MSEC) /* 10 msec */
static inline int maxfg_regmap_writeverify(const struct maxfg_regmap *map,
				           unsigned int reg,
					   u16 data,
					   const char *name)
{
	int tmp, ret, retries;

	if (!map->regmap) {
		pr_err("Failed to write %s, no regmap\n", name);
		return -EINVAL;
	}

	for (retries = 3; retries > 0; retries--) {
		ret = regmap_write(map->regmap, reg, data);
		if (ret < 0)
			continue;

		usleep_range(WAIT_VERIFY, WAIT_VERIFY + 100);

		ret = regmap_read(map->regmap, reg, &tmp);
		if (ret < 0)
			continue;

		if (tmp == data)
			return 0;
	}

	return -EIO;
}

#define REGMAP_WRITE_VERIFY(regmap, what, value) \
	maxfg_regmap_writeverify(regmap, what, value, #what)

/* dump FG model data */
void dump_model(struct device *dev, u16 model_start, u16 *data, int count);
int maxfg_get_fade_rate(struct device *dev, int bhi_fcn_count, int *fade_rate);
const struct maxfg_reg * maxfg_find_by_tag(struct maxfg_regmap *map, enum maxfg_reg_tags tag);
int maxfg_reg_read(struct maxfg_regmap *map, enum maxfg_reg_tags tag, u16 *val);
int maxfg_collect_history_data(void *buff, size_t size, bool is_por, u16 designcap,
			       struct maxfg_regmap *regmap, struct maxfg_regmap *regmap_debug);
int maxfg_read_resistance_avg(u16 RSense);
int maxfg_read_resistance_raw(struct maxfg_regmap *map);
int maxfg_read_resistance(struct maxfg_regmap *map, u16 RSense);
int maxfg_health_get_ai(struct device *dev, int bhi_acim, u16 RSense);
int batt_ce_load_data(struct maxfg_regmap *map, struct gbatt_capacity_estimation *cap_esti);
void batt_ce_dump_data(const struct gbatt_capacity_estimation *cap_esti, struct logbuffer *log);
void batt_ce_store_data(struct maxfg_regmap *map, struct gbatt_capacity_estimation *cap_esti);
void batt_ce_stop_estimation(struct gbatt_capacity_estimation *cap_esti, int reason);
int maxfg_health_write_ai(u16 act_impedance, u16 act_timerh);
int maxfg_reg_log_data(struct maxfg_regmap *map, struct maxfg_regmap *map_debug, char *buf);
#endif  // MAXFG_COMMON_H_
