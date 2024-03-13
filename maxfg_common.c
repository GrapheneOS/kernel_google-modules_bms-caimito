/*
 * Fuel gauge driver for common
 *
 * Copyright (C) 2023 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of.h>
#include <linux/debugfs.h>
#include "maxfg_common.h"

/* dump FG model data */
void dump_model(struct device *dev, u16 model_start, u16 *data, int count)
{
	int i, j, len;
	char buff[16 * 5 + 1] = {};

	for (i = 0; i < count; i += 16) {

		for (len = 0, j = 0; j < 16; j++)
			len += scnprintf(&buff[len], sizeof(buff) - len,
					 "%04x ", data[i + j]);

		dev_info(dev, "%x: %s\n", i + model_start, buff);
	}
}

int maxfg_get_fade_rate(struct device *dev, int bhi_fcn_count, int *fade_rate, enum gbms_property p)
{
	struct maxfg_eeprom_history hist = { 0 };
	int ret, ratio, i, fcn_sum = 0, fcr_sum = 0;
	u16 hist_idx;

	ret = gbms_storage_read(GBMS_TAG_HCNT, &hist_idx, sizeof(hist_idx));
	if (ret < 0) {
		dev_err(dev, "failed to get history index (%d)\n", ret);
		return -EIO;
	}

	dev_dbg(dev, "%s: hist_idx=%d\n", __func__, hist_idx);

	/* no fade for new battery (less than 30 cycles) */
	if (hist_idx < bhi_fcn_count)
		return 0;

	while (hist_idx >= BATT_MAX_HIST_CNT && bhi_fcn_count > 1) {
		hist_idx--;
		bhi_fcn_count--;
		if (bhi_fcn_count == 1) {
			hist_idx = BATT_MAX_HIST_CNT - 1;
			break;
		}
	}

	for (i = bhi_fcn_count; i ; i--, hist_idx--) {
		ret = gbms_storage_read_data(GBMS_TAG_HIST, &hist,
					     sizeof(hist), hist_idx);

		dev_dbg(dev, "%s: idx=%d hist.fcn=%d (%x) hist.fcr=%d (%x) ret=%d\n",
			     __func__, hist_idx, hist.fullcapnom, hist.fullcapnom,
			     hist.fullcaprep, hist.fullcaprep, ret);

		if (ret < 0 || ret != sizeof(hist))
			return -EINVAL;

		/* hist.fullcapnom = fullcapnom * 800 / designcap */
		fcn_sum += hist.fullcapnom;
		fcr_sum += hist.fullcaprep;
	}

	/* convert from maxfg_eeprom_history to percent */
	ratio = (p == GBMS_PROP_CAPACITY_FADE_RATE_FCR) ? fcr_sum / (bhi_fcn_count * 8)
							: fcn_sum / (bhi_fcn_count * 8);

	/* allow negative value when capacity larger than design */
	*fade_rate = 100 - ratio;

	return 0;
}

static const struct maxfg_reg * maxfg_find_by_index(struct maxfg_regtags *tags, int index)
{
	if (index < 0 || !tags || index >= tags->max)
		return NULL;

	return &tags->map[index];
}

const struct maxfg_reg * maxfg_find_by_tag(struct maxfg_regmap *map, enum maxfg_reg_tags tag)
{
	return maxfg_find_by_index(&map->regtags, tag);
}

int maxfg_reg_read(struct maxfg_regmap *map, enum maxfg_reg_tags tag, u16 *val)
{
	const struct maxfg_reg *reg;
	unsigned int tmp;
	int rtn;

	reg = maxfg_find_by_tag(map, tag);
	if (!reg)
		return -EINVAL;

	rtn = regmap_read(map->regmap, reg->reg, &tmp);
	if (rtn)
		pr_err("Failed to read %x\n", reg->reg);
	else
		*val = tmp;

	return rtn;
}

static int maxfg_reg_read_addr(struct maxfg_regmap *map, enum maxfg_reg_tags tag,
			       u16 *val, u16 *addr)
{
	const struct maxfg_reg *reg;
	unsigned int tmp;
	int rtn;

	reg = maxfg_find_by_tag(map, tag);
	if (!reg)
		return -EINVAL;

	*addr = reg->reg;

	rtn = regmap_read(map->regmap, reg->reg, &tmp);
	if (rtn)
		pr_err("Failed to read %x\n", reg->reg);
	else
		*val = tmp;

	return rtn;
}

static int maxfg_reg_write(struct maxfg_regmap *map, enum maxfg_reg_tags tag, u16 val)
{
	const struct maxfg_reg *reg;
	unsigned int tmp = val;
	int rtn;

	reg = maxfg_find_by_tag(map, tag);
	if (!reg)
		return -EINVAL;

	rtn = regmap_write(map->regmap, reg->reg, tmp);
	if (rtn)
		pr_err("Failed to write %x\n", reg->reg);

	return rtn;
}

#define REG_HALF_HIGH(reg)     ((reg >> 8) & 0x00FF)
#define REG_HALF_LOW(reg)      (reg & 0x00FF)
int maxfg_collect_history_data(void *buff, size_t size, bool is_por, u16 designcap,
			       struct maxfg_regmap *regmap, struct maxfg_regmap *regmap_debug)
{
	struct maxfg_eeprom_history hist = { 0 };
	u16 data;
	int temp, ret;

	if (is_por)
		return -EINVAL;

	ret = maxfg_reg_read(regmap_debug, MAXFG_TAG_tempco, &data);
	if (ret)
		return ret;

	hist.tempco = data;

	ret = maxfg_reg_read(regmap_debug, MAXFG_TAG_rcomp0, &data);
	if (ret)
		return ret;

	hist.rcomp0 = data;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_timerh, &data);
	if (ret)
		return ret;

	/* Convert LSB from 3.2hours(192min) to 5days(7200min) */
	hist.timerh = data * 192 / 7200;

	if (!designcap) {
		ret = maxfg_reg_read(regmap, MAXFG_TAG_descap, &designcap);
		if (ret)
			return ret;
	}

	/* multiply by 100 to convert from mAh to %, LSB 0.125% */
	ret = maxfg_reg_read(regmap, MAXFG_TAG_fcnom, &data);
	if (ret)
		return ret;

	temp = (int)data * 800 / (int)designcap;
	hist.fullcapnom = temp > MAX_HIST_FULLCAP ? MAX_HIST_FULLCAP : temp;

	/* multiply by 100 to convert from mAh to %, LSB 0.125% */
	ret = maxfg_reg_read(regmap, MAXFG_TAG_fcrep, &data);
	if (ret)
		return ret;

	temp = (int)data * 800 / (int)designcap;
	hist.fullcaprep = temp > MAX_HIST_FULLCAP ? MAX_HIST_FULLCAP : temp;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_msoc, &data);
	if (ret)
		return ret;

	/* Convert LSB from 1% to 2% */
	hist.mixsoc = REG_HALF_HIGH(data) / 2;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_vfsoc, &data);
	if (ret)
		return ret;

	/* Convert LSB from 1% to 2% */
	hist.vfsoc = REG_HALF_HIGH(data) / 2;


	ret = maxfg_reg_read(regmap, MAXFG_TAG_mmdv, &data);
	if (ret)
		return ret;

	/* LSB is 20mV, store values from 4.2V min */
	hist.maxvolt = (REG_HALF_HIGH(data) * 20 - 4200) / 20;
	/* Convert LSB from 20mV to 10mV, store values from 2.5V min */
	hist.minvolt = (REG_HALF_LOW(data) * 20 - 2500) / 10;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_mmdt, &data);
	if (ret)
		return ret;

	/* Convert LSB from 1degC to 3degC, store values from 25degC min */
	hist.maxtemp = ((s8)REG_HALF_HIGH(data) - 25) / 3;
	/* Convert LSB from 1degC to 3degC, store values from -20degC min */
	hist.mintemp = ((s8)REG_HALF_LOW(data) + 20) / 3;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_mmdc, &data);
	if (ret)
		return ret;

	/* Convert LSB from 0.08A to 0.5A */
	hist.maxchgcurr = (s8)REG_HALF_HIGH(data) * 8 / 50;
	hist.maxdischgcurr = (s8)REG_HALF_LOW(data) * 8 / 50;

	memcpy(buff, &hist, sizeof(hist));
	return (size_t)sizeof(hist);
}

/* resistance and impedance ------------------------------------------------ */

int maxfg_read_resistance_avg(u16 RSense)
{
	u16 ravg;
	int ret = 0;

	ret = gbms_storage_read(GBMS_TAG_RAVG, &ravg, sizeof(ravg));
	if (ret < 0)
		return ret;

	return reg_to_resistance_micro_ohms(ravg, RSense);
}

int maxfg_read_resistance_raw(struct maxfg_regmap *map)
{
	u16 data;
	int ret;

	ret = maxfg_reg_read(map, MAXFG_TAG_rslow, &data);
	if (ret < 0)
		return ret;

	return data;
}

int maxfg_read_resistance(struct maxfg_regmap *map, u16 RSense)
{
	int rslow;

	rslow = maxfg_read_resistance_raw(map);
	if (rslow < 0)
		return rslow;

	return reg_to_resistance_micro_ohms(rslow, RSense);
}

/* ----------------------------------------------------------------------- */

/* will return error if the value is not valid  */
int maxfg_health_get_ai(struct device *dev, int bhi_acim, u16 RSense)
{
	u16 act_impedance, act_timerh;
	int ret;

	if (bhi_acim != 0)
		return bhi_acim;

	/* read both and recalculate for compatibility */
	ret = gbms_storage_read(GBMS_TAG_ACIM, &act_impedance, sizeof(act_impedance));
	if (ret < 0)
		return -EIO;

	ret = gbms_storage_read(GBMS_TAG_THAS, &act_timerh, sizeof(act_timerh));
	if (ret < 0)
		return -EIO;

	/* need to get starting impedance (if qualified) */
	if (act_impedance == 0xffff || act_timerh == 0xffff)
		return -EINVAL;

	/* not zero, not negative */
	bhi_acim = reg_to_resistance_micro_ohms(act_impedance, RSense);

	/* TODO: correct impedance with timerh */

	dev_info(dev, "%s: bhi_acim =%d act_impedance=%x act_timerh=%x\n",
		 __func__, bhi_acim, act_impedance, act_timerh);

	return bhi_acim;
}

/* Capacity Estimation functions*/
static int batt_ce_regmap_read(struct maxfg_regmap *map, const struct maxfg_reg *bcea, u32 reg, u16 *data)
{
	int err;
	u16 val;

	if (!bcea)
		return -EINVAL;

	err = REGMAP_READ(map, bcea->map[reg], &val);
	if (err)
		return err;

	switch(reg) {
	case CE_DELTA_CC_SUM_REG:
	case CE_DELTA_VFSOC_SUM_REG:
		*data = val;
		break;
	case CE_CAP_FILTER_COUNT:
		val = val & 0x0F00;
		*data = val >> 8;
		break;
	default:
		break;
	}

	return err;
}

int batt_ce_load_data(struct maxfg_regmap *map, struct gbatt_capacity_estimation *cap_esti)
{
	u16 data;
	const struct maxfg_reg *bcea = cap_esti->bcea;

	cap_esti->estimate_state = ESTIMATE_NONE;
	if (batt_ce_regmap_read(map, bcea, CE_DELTA_CC_SUM_REG, &data) == 0)
		cap_esti->delta_cc_sum = data;
	else
		cap_esti->delta_cc_sum = 0;

	if (batt_ce_regmap_read(map, bcea, CE_DELTA_VFSOC_SUM_REG, &data) == 0)
		cap_esti->delta_vfsoc_sum = data;
	else
		cap_esti->delta_vfsoc_sum = 0;

	if (batt_ce_regmap_read(map, bcea, CE_CAP_FILTER_COUNT, &data) == 0)
		cap_esti->cap_filter_count = data;
	else
		cap_esti->cap_filter_count = 0;
	return 0;
}

void batt_ce_dump_data(const struct gbatt_capacity_estimation *cap_esti, struct logbuffer *log)
{
	logbuffer_log(log, "cap_filter_count: %d"
			    " start_cc: %d"
			    " start_vfsoc: %d"
			    " delta_cc_sum: %d"
			    " delta_vfsoc_sum: %d"
			    " state: %d"
			    " cable: %d",
			    cap_esti->cap_filter_count,
			    cap_esti->start_cc,
			    cap_esti->start_vfsoc,
			    cap_esti->delta_cc_sum,
			    cap_esti->delta_vfsoc_sum,
			    cap_esti->estimate_state,
			    cap_esti->cable_in);
}

static int batt_ce_regmap_write(struct maxfg_regmap *map,
				const struct maxfg_reg *bcea,
				u32 reg, u16 data)
{
	int err = -EINVAL;
	u16 val;

	if (!bcea)
		return -EINVAL;

	switch(reg) {
	case CE_DELTA_CC_SUM_REG:
	case CE_DELTA_VFSOC_SUM_REG:
		err = REGMAP_WRITE(map, bcea->map[reg], data);
		break;
	case CE_CAP_FILTER_COUNT:
		err = REGMAP_READ(map, bcea->map[reg], &val);
		if (err)
			return err;
		val = val & 0xF0FF;
		if (data > CE_FILTER_COUNT_MAX)
			val = val | 0x0F00;
		else
			val = val | (data << 8);
		err = REGMAP_WRITE(map, bcea->map[reg], val);
		break;
	default:
		break;
	}

	return err;
}

/* call holding &cap_esti->batt_ce_lock */
void batt_ce_store_data(struct maxfg_regmap *map, struct gbatt_capacity_estimation *cap_esti)
{
	if (cap_esti->cap_filter_count <= CE_FILTER_COUNT_MAX) {
		batt_ce_regmap_write(map, cap_esti->bcea,
					  CE_CAP_FILTER_COUNT,
					  cap_esti->cap_filter_count);
	}

	batt_ce_regmap_write(map, cap_esti->bcea,
				  CE_DELTA_VFSOC_SUM_REG,
				  cap_esti->delta_vfsoc_sum);
	batt_ce_regmap_write(map, cap_esti->bcea,
				  CE_DELTA_CC_SUM_REG,
				  cap_esti->delta_cc_sum);
}

/* call holding &cap_esti->batt_ce_lock */
void batt_ce_stop_estimation(struct gbatt_capacity_estimation *cap_esti, int reason)
{
	cap_esti->estimate_state = reason;
	cap_esti->start_vfsoc = 0;
	cap_esti->start_cc = 0;
}

int maxfg_health_write_ai(u16 act_impedance, u16 act_timerh)
{
	int ret;

	ret = gbms_storage_write(GBMS_TAG_ACIM, &act_impedance, sizeof(act_impedance));
	if (ret < 0)
		return -EIO;

	ret = gbms_storage_write(GBMS_TAG_THAS, &act_timerh, sizeof(act_timerh));
	if (ret < 0)
		return -EIO;

	return 0;
}

int maxfg_reg_log_data(struct maxfg_regmap *map, struct maxfg_regmap *map_debug, char *buf)
{
	u16 vfsoc, avcap, repcap, fullcap, fullcaprep, fullcapnom, qh0, qh, dqacc, dpacc, fstat;
	u16 qresidual, rcomp0, cycles, learncfg, tempco, filtercfg, mixcap, vfremcap, vcell, ibat;
	u16 vfsoc_addr, avcap_addr, repcap_addr, fullcap_addr, fullcaprep_addr, fullcapnom_addr;
	u16 qh0_addr, qh_addr, dqacc_addr, dpacc_addr, fstat_addr, qresidual_addr, rcomp0_addr;
	u16 cycles_addr, learncfg_addr, tempco_addr, filtercfg_addr, mixcap_addr, vfremcap_addr;
	u16 vcell_addr, ibat_addr;
	int ret, len;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_vfsoc, &vfsoc, &vfsoc_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_avcap, &avcap, &avcap_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_repcap, &repcap, &repcap_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_fulcap, &fullcap, &fullcap_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_fcrep, &fullcaprep, &fullcaprep_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_fcnom, &fullcapnom, &fullcapnom_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_qh0, &qh0, &qh0_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_qh, &qh, &qh_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_dqacc, &dqacc, &dqacc_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_dpacc, &dpacc, &dpacc_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_qresd, &qresidual, &qresidual_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_fstat, &fstat, &fstat_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_learn, &learncfg, &learncfg_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map_debug, MAXFG_TAG_tempco, &tempco, &tempco_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map_debug, MAXFG_TAG_filcfg, &filtercfg, &filtercfg_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_mcap, &mixcap, &mixcap_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_vfcap, &vfremcap, &vfremcap_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_vcel, &vcell, &vcell_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_curr, &ibat, &ibat_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map_debug, MAXFG_TAG_rcomp0, &rcomp0, &rcomp0_addr);
	if (ret < 0)
		return ret;

	ret = maxfg_reg_read_addr(map, MAXFG_TAG_cycles, &cycles, &cycles_addr);
	if (ret < 0)
		return ret;

	len = scnprintf(&buf[0], PAGE_SIZE, "%02X:%04X %02X:%04X %02X:%04X %02X:%04X"
			" %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X"
			" %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X"
			" %02X:%04X %02X:%04X %02X:%04X %02X:%04X %02X:%04X",
			vfsoc_addr, vfsoc, avcap_addr, avcap, repcap_addr, repcap,
			fullcap_addr, fullcap, fullcaprep_addr, fullcaprep,
			fullcapnom_addr, fullcapnom, qh0_addr, qh0, qh_addr, qh, dqacc_addr, dqacc,
			dpacc_addr, dpacc, qresidual_addr, qresidual, fstat_addr, fstat,
			learncfg_addr, learncfg, tempco_addr, tempco, filtercfg_addr, filtercfg,
			mixcap_addr, mixcap, vfremcap_addr, vfremcap, vcell_addr, vcell,
			ibat_addr , ibat, rcomp0_addr, rcomp0, cycles_addr, cycles);

	return len;
}

/* learning parameters */
#define MAX_FG_LEARNING_CONFIG_NORMAL_REGS 8
#define MAX_FG_LEARNING_CONFIG_DEBUG_REGS 2

static enum maxfg_reg_tags fg_learnign_param[] ={
	/* from normal regmap */
	MAXFG_TAG_fcnom,
	MAXFG_TAG_dpacc,
	MAXFG_TAG_dqacc,
	MAXFG_TAG_fcrep,
	MAXFG_TAG_repsoc,
	MAXFG_TAG_msoc,
	MAXFG_TAG_vfsoc,
	MAXFG_TAG_fstat,

	/* from debug_regmap */
	MAXFG_TAG_rcomp0,
	MAXFG_TAG_tempco,
};

void maxfg_init_fg_learn_capture_config(struct maxfg_capture_config *config,
					struct maxfg_regmap *regmap,
					struct maxfg_regmap *debug_regmap)
{
	if (!config) {
		pr_err("no config for logging FG learn\n");
		return;
	}

	scnprintf(&config->name[0], MAX_FG_CAPTURE_CONFIG_NAME_MAX, "FG Learning Parameters");
	config->normal.tag = &fg_learnign_param[0];
	config->normal.reg_cnt = MAX_FG_LEARNING_CONFIG_NORMAL_REGS;
	config->normal.regmap = regmap;

	config->debug.tag = &fg_learnign_param[MAX_FG_LEARNING_CONFIG_NORMAL_REGS];
	config->debug.reg_cnt = MAX_FG_LEARNING_CONFIG_DEBUG_REGS;
	config->debug.regmap = debug_regmap;

	config->data_size = (config->normal.reg_cnt + config->debug.reg_cnt) * sizeof(u16);
}

static inline int maxfg_read_registers(struct maxfg_capture_regs *regs, u16 *buffer)
{
	int ret, idx;

	for (idx = 0; idx < regs->reg_cnt; idx++) {
		ret = maxfg_reg_read(regs->regmap, regs->tag[idx], &buffer[idx]);
		if (ret < 0) {
			pr_err("failed to reg_tag(%u) %d\n", regs->tag[idx], ret);
			return ret;
		}
	}

	return 0;
}

int maxfg_alloc_capture_buf(struct maxfg_capture_buf *buf, int slots)
{
	if ((slots & (slots-1)) || !buf || !buf->config.data_size || !slots)
		return -EINVAL;

	buf->slots = 0;
	buf->cb.buf = kzalloc(buf->config.data_size * slots, GFP_KERNEL);
	if (!buf->cb.buf)
		return -ENOMEM;

	buf->cb.head = 0;
	buf->cb.tail = 0;
	buf->slots = slots;
	buf->latest_entry = NULL;

	mutex_init(&buf->cb_wr_lock);
	mutex_init(&buf->cb_rd_lock);

	return 0;
}

void maxfg_clear_capture_buf(struct maxfg_capture_buf *buf)
{
	int head, tail;

	if (!buf || !buf->cb.buf)
		return;

	mutex_lock(&buf->cb_wr_lock);
	mutex_lock(&buf->cb_rd_lock);

	head = buf->cb.head;
	tail = buf->cb.tail;

	if (CIRC_CNT(head, tail, buf->slots)) {
		head = (head + 1) & (buf->slots - 1);

		smp_wmb();

		/* make buffer empty by (head == tail) while preserving latest_entry as a seed */
		WRITE_ONCE(buf->cb.head, head);
		WRITE_ONCE(buf->cb.tail, head);
	}

	mutex_unlock(&buf->cb_rd_lock);
	mutex_unlock(&buf->cb_wr_lock);
}

void maxfg_free_capture_buf(struct maxfg_capture_buf *buf)
{
	if (!buf || !buf->cb.buf ){
		pr_err("Invalid maxfg_capture_buf\n");
		return;
	}

	if (buf->cb.buf && buf->slots > 0)
		kfree(buf->cb.buf);

	mutex_destroy(&buf->cb_wr_lock);
	mutex_destroy(&buf->cb_rd_lock);

	buf->cb.buf = NULL;
	buf->slots = 0;
}

int maxfg_capture_registers(struct maxfg_capture_buf *buf)
{
	struct maxfg_capture_config *config = &buf->config;
	u16* reg_val;
	void* latest_entry;
	int head, tail, ret;

	mutex_lock(&buf->cb_wr_lock);

	head = buf->cb.head;
	tail = READ_ONCE(buf->cb.tail);

	/* if buffer is full, drop the last entry */
	if (CIRC_SPACE(head, tail, buf->slots) == 0) {
		mutex_lock(&buf->cb_rd_lock);
		WRITE_ONCE(buf->cb.tail, (tail + 1) & (buf->slots - 1));
		mutex_unlock(&buf->cb_rd_lock);
	}

	reg_val = (u16*)&buf->cb.buf[head * buf->config.data_size];
	latest_entry = reg_val;

	ret = maxfg_read_registers(&config->normal, reg_val);
	if (ret < 0) {
		mutex_unlock(&buf->cb_wr_lock);
		return ret;
	}

	reg_val += config->normal.reg_cnt;

	ret = maxfg_read_registers(&config->debug, reg_val);
	if (ret < 0) {
		mutex_unlock(&buf->cb_wr_lock);
		return ret;
	}

	smp_wmb();
	WRITE_ONCE(buf->cb.head, (head + 1) & (buf->slots - 1));

	buf->latest_entry = latest_entry;
	mutex_unlock(&buf->cb_wr_lock);

	return 0;
}

int maxfg_capture_to_cstr(struct maxfg_capture_config *config, u16* reg_val,
				 char* str_buf, int buf_len)
{
	const struct maxfg_reg *fg_reg;
	int len = 0, reg_idx = 0;

	for (reg_idx = 0; reg_idx < config->normal.reg_cnt && len < buf_len; reg_idx++) {
		fg_reg = maxfg_find_by_tag(config->normal.regmap, config->normal.tag[reg_idx]);
		if (!fg_reg)
			return len;

		len += scnprintf(&str_buf[len], buf_len - len, "%02X:%04X ", fg_reg->reg,
				 reg_val[reg_idx]);
	}

	reg_val += config->normal.reg_cnt;

	for (reg_idx = 0; reg_idx < config->debug.reg_cnt && len < buf_len; reg_idx++) {
		fg_reg = maxfg_find_by_tag(config->debug.regmap, config->debug.tag[reg_idx]);
		if (!fg_reg)
			return len;

		len += scnprintf(&str_buf[len], buf_len - len, "%02X:%04X ", fg_reg->reg,
				 reg_val[reg_idx]);
	}

	return len;
}

int maxfg_show_captured_buffer(struct maxfg_capture_buf *buf, char *str_buf, int buf_len)
{
	struct maxfg_capture_config *config = &buf->config;
	u16* reg_val;
	int head, tail, count, to_end, idx, rt;

	if (!buf)
		return -EINVAL;

	mutex_lock(&buf->cb_rd_lock);

	head = READ_ONCE(buf->cb.head);
	tail = buf->cb.tail;

	count = CIRC_CNT(head, tail, buf->slots);
	rt = scnprintf(&str_buf[0], buf_len, "%s (%d):\n", config->name, count);

	if (count == 0)
		goto maxfg_show_captured_buffer_exit;

	to_end = CIRC_CNT_TO_END(head, tail, buf->slots);

	for (idx = 0; idx < to_end && rt < buf_len; idx++) {
		reg_val = (u16*)&buf->cb.buf[(tail+idx) * buf->config.data_size];
		rt += maxfg_capture_to_cstr(config, reg_val, &str_buf[rt], buf_len - rt);
		rt += scnprintf(&str_buf[rt], buf_len - rt, "\n");
	}

	count -= idx;

	for (idx = 0; idx < count && rt < buf_len; idx++) {
		reg_val = (u16*)&buf->cb.buf[idx * buf->config.data_size];
		rt += maxfg_capture_to_cstr(config, reg_val, &str_buf[rt], buf_len - rt);
		rt += scnprintf(&str_buf[rt], buf_len - rt, "\n");
	}

maxfg_show_captured_buffer_exit:
	mutex_unlock(&buf->cb_rd_lock);

	return rt;
}

/*
 * data in prev_val follows the order of fg_learnign_param[]
 *  prev_val[0]: fcnom
 *  prev_val[1]: dpacc
 *  prev_val[2]: dqacc
 *  prev_val[7]: fstat
*/
bool maxfg_ce_relaxed(struct maxfg_regmap *regmap, const u16 relax_mask, const u16* prev_val)
{
	u16 fstat, fcnom, dqacc, dpacc;
	int ret;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_fstat, &fstat);
	if (ret < 0)
		return false;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_fcnom, &fcnom);
	if (ret < 0)
		return false;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_dpacc, &dpacc);
	if (ret < 0)
		return false;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_dqacc, &dqacc);
	if (ret < 0)
		return false;

	/*
	 * log when relaxed state changes, when fcnom, dpacc, dqacc change
	 * TODO: b/326639382
	 *  - log only when dpacc, dqacc or fcnom change and simply
	 *    count the relaxation event otherwise.
	 */
	return (fstat & relax_mask) != (prev_val[7] & relax_mask) ||
		dpacc != prev_val[1] || dqacc != prev_val[2] ||
		fcnom != prev_val[0];
}

#define  MAXFG_DR_VFSOC_DELTA_DEFAULT	0
#define  MAXFG_DR_TEMP_MIN_DEFAULT	150
#define  MAXFG_DR_TEMP_MAX_DEFAULT	350
#define  MAXFG_DR_RELAX_INVALID		0xffff
#define  MAXFG_DR_VFOCV_MV_INHIB_MIN_DEFAULT	3900
#define  MAXFG_DR_VFOCV_MV_INHIB_MAX_DEFAULT	4200

bool maxfg_is_relaxed(struct maxfg_regmap *regmap, u16 *fstat, u16 mask)
{
	return maxfg_reg_read(regmap, MAXFG_TAG_fstat, fstat) == 0 &&
		(*fstat & mask);
}

bool maxfg_dynrel_check(struct maxfg_dynrel_state *dr_state,
			struct maxfg_regmap *regmap)
{
	u16 delta_vfsoc;
	int ret;

	/* alternative: set this to 0 and qual on temp, vfocv */
	if (!dr_state->vfsoc_delta)
		return true;

	ret = maxfg_reg_read(regmap, MAXFG_TAG_temp, &dr_state->temp_last);
	if (ret < 0 || dr_state->temp_last < dr_state->temp_qual.min ||
		dr_state->temp_last > dr_state->temp_qual.max)
		return false;

	/* exclude */
	ret = maxfg_reg_read(regmap, MAXFG_TAG_vfocv, &dr_state->vfocv_last);
	if (ret < 0 || (dr_state->vfocv_last >= dr_state->vfocv_inhibit.min &&
			dr_state->vfocv_last <= dr_state->vfocv_inhibit.max))
		return false;

	/* always for logging */
	ret = maxfg_reg_read(regmap, MAXFG_TAG_vfsoc, &dr_state->vfsoc_last);
	if (ret < 0)
		return false;

	if (dr_state->vfsoc_rel == MAXFG_DR_RELAX_INVALID)
		return true;

	delta_vfsoc = abs(dr_state->vfsoc_last - dr_state->vfsoc_rel);
	if (delta_vfsoc < dr_state->vfsoc_delta)
		return false;

	return true;
}

int maxfg_dynrel_mark_relax(struct maxfg_dynrel_state *dr_state,
			    struct maxfg_regmap *regmap)
{
	int ret;

	/* needs vfsoc for next round */
	ret = maxfg_reg_read(regmap, MAXFG_TAG_vfsoc, &dr_state->vfsoc_rel);
	if (ret < 0)
		return -EIO;
	ret = maxfg_reg_read(regmap, MAXFG_TAG_temp, &dr_state->temp_rel);
	if (ret < 0)
		dr_state->temp_rel = 0xffff;
	ret = maxfg_reg_read(regmap, MAXFG_TAG_vfocv, &dr_state->vfocv_rel);
	if (ret < 0)
		dr_state->vfocv_rel = 0xffff;

	return 0;
}

int maxfg_dynrel_relaxcfg(struct maxfg_dynrel_state *dr_state,
			  struct maxfg_regmap *regmap, bool enable)
{
	const u16 value = enable ?
		dr_state->relcfg_allow : dr_state->relcfg_inhibit;
	u16 temp;
	int ret;

	ret = maxfg_reg_write(regmap, MAXFG_TAG_relaxcfg, value);
	if (ret < 0)
		return -EIO;
	ret = maxfg_reg_read(regmap, MAXFG_TAG_relaxcfg, &temp);
	if (ret < 0)
		return -EIO;
	if (temp != value)
		return -EINVAL;

	return 0;
}

void maxfg_dynrel_init_sysfs(struct maxfg_dynrel_state *dr_state,
			     struct dentry *de)
{
	if (!de)
		return;

	debugfs_create_u32("dr_vsoc_delta", 0644, de, &dr_state->vfsoc_delta);
	debugfs_create_u16("dr_relcfg_inhibit", 0644, de, &dr_state->relcfg_inhibit);
	debugfs_create_u16("dr_relcfg_allow", 0644, de, &dr_state->relcfg_allow);
	debugfs_create_bool("dr_monitor", 0644, de, &dr_state->monitor);
}

void maxfg_dynrel_init(struct maxfg_dynrel_state *dr_state,
		       struct device_node *node)
{
	u32 value;
	int ret;

	dr_state->vfsoc_rel = MAXFG_DR_RELAX_INVALID;

	ret = of_property_read_u32(node, "maxfg,dr_vfsoc_delta", &value);
	if (ret < 0)
		value = MAXFG_DR_VFSOC_DELTA_DEFAULT;
	dr_state->vfsoc_delta = percentage_to_reg(value);

	ret = of_property_read_u32(node, "maxfg,dr_min_deci_temp_c", &value);
	if (ret < 0)
		value = MAXFG_DR_TEMP_MIN_DEFAULT;
	dr_state->temp_qual.min = deci_deg_cel_to_reg(value);
	ret = of_property_read_u32(node, "maxfg,dr_max_deci_temp_c", &value);
	if (ret < 0)
		value = MAXFG_DR_TEMP_MAX_DEFAULT;
	dr_state->temp_qual.max = deci_deg_cel_to_reg(value);

	ret = of_property_read_u32(node, "maxfg,vfocv_inhibit_min_mv", &value);
	if (ret < 0)
		value = MAXFG_DR_VFOCV_MV_INHIB_MIN_DEFAULT;
	dr_state->vfocv_inhibit.min = micro_volt_to_reg(value * 1000);
	ret = of_property_read_u32(node, "maxfg,vfocv_inhibit_max_mv", &value);
	if (ret < 0)
		value = MAXFG_DR_VFOCV_MV_INHIB_MAX_DEFAULT;
	dr_state->vfocv_inhibit.max = micro_volt_to_reg(value * 1000);
}

void maxfg_dynrel_log_cfg(struct logbuffer *mon, struct device *dev,
			  const struct maxfg_dynrel_state *dr_state)
{
	gbms_logbuffer_devlog(mon, dev, LOGLEVEL_INFO, 0, LOGLEVEL_INFO,
		"dynrel_cfg temp=%d,%d vfocv=%d,%d delta=%d cfg=%x,%x\n",
		reg_to_deci_deg_cel(dr_state->temp_qual.min),
		reg_to_deci_deg_cel(dr_state->temp_qual.max),
		reg_to_micro_volt(dr_state->vfocv_inhibit.min) / 1000,
		reg_to_micro_volt(dr_state->vfocv_inhibit.max) / 1000,
		reg_to_percentage(dr_state->vfsoc_delta),
		dr_state->relcfg_allow,
		dr_state->relcfg_inhibit);
}

static void maxfg_dynrel_log__(struct logbuffer *mon, struct device *dev,
			       const struct maxfg_dynrel_state *dr_state,
			        u16 mark, u16 vfocv, u16 vfsoc, u16 temp)
{
	int vfsoc_rel;

	if (dr_state->vfsoc_rel == MAXFG_DR_RELAX_INVALID) {
		vfsoc_rel = -1;
	} else {
		vfsoc_rel = reg_to_micro_volt(dr_state->vfsoc_rel) / 1000;
	}

	gbms_logbuffer_devlog(mon, dev, LOGLEVEL_INFO, 0, LOGLEVEL_INFO,
			"dynrel mark=%x allowed=%d lrel=%d, temp=%d vfocv=%d vfsoc=%d",
			mark, dr_state->relax_allowed, vfsoc_rel,
			reg_to_deci_deg_cel(temp),
			reg_to_micro_volt(vfocv) / 1000,
			reg_to_percentage(vfsoc));
}


void maxfg_dynrel_log_rel(struct logbuffer *mon, struct device *dev, u16 mark,
			  const struct maxfg_dynrel_state *dr_state)
{
	maxfg_dynrel_log__(mon, dev, dr_state, mark, dr_state->vfocv_rel,
			   dr_state->vfsoc_rel, dr_state->temp_rel);
}

void maxfg_dynrel_log(struct logbuffer *mon, struct device *dev, u16 mark,
		      const struct maxfg_dynrel_state *dr_state)
{
	maxfg_dynrel_log__(mon, dev, dr_state, mark, dr_state->vfocv_last,
			   dr_state->vfsoc_last, dr_state->temp_last);
}