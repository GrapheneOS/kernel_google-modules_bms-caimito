/*
 * Google Battery Management System
 *
 * Copyright (C) 2018 Google Inc.
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

#ifndef __GOOGLE_BMS_H_
#define __GOOGLE_BMS_H_

#include <linux/types.h>
#include <linux/usb/pd.h>
#include "gbms_power_supply.h"
#include "qmath.h"
#include "gbms_storage.h"

struct device_node;

#define GBMS_CHG_TEMP_NB_LIMITS_MAX 10
#define GBMS_CHG_VOLT_NB_LIMITS_MAX 5

struct gbms_chg_profile {
	const char *owner_name;

	int temp_nb_limits;
	s32 temp_limits[GBMS_CHG_TEMP_NB_LIMITS_MAX];
	int volt_nb_limits;
	s32 volt_limits[GBMS_CHG_VOLT_NB_LIMITS_MAX];
	/* Array of constant current limits */
	s32 *cccm_limits;
	/* used to fill table  */
	u32 capacity_ma;

	/* behavior */
	u32 fv_uv_margin_dpct;
	u32 cv_range_accuracy;
	u32 cv_debounce_cnt;
	u32 cv_update_interval;
	u32 cv_tier_ov_cnt;
	u32 cv_tier_switch_cnt;
	/* taper step */
	u32 fv_uv_resolution;
	/* experimental */
	u32 cv_otv_margin;
};

#define WLC_BPP_THRESHOLD_UV	700000
#define WLC_EPP_THRESHOLD_UV	1100000

#define FOREACH_CHG_EV_ADAPTER(S)		\
	S(UNKNOWN), 	\
	S(USB),		\
	S(USB_SDP),	\
	S(USB_DCP),	\
	S(USB_CDP),	\
	S(USB_ACA),	\
	S(USB_C),	\
	S(USB_PD),	\
	S(USB_PD_DRP),	\
	S(USB_PD_PPS),	\
	S(USB_BRICKID),	\
	S(USB_HVDCP),	\
	S(USB_HVDCP3),	\
	S(WLC),		\
	S(WLC_EPP),	\
	S(WLC_SPP),	\

#define CHG_EV_ADAPTER_STRING(s)	#s
#define _CHG_EV_ADAPTER_PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

/* Enums will start with CHG_EV_ADAPTER_TYPE_ */
#define CHG_EV_ADAPTER_ENUM(e)	\
			_CHG_EV_ADAPTER_PRIMITIVE_CAT(CHG_EV_ADAPTER_TYPE_,e)

enum chg_ev_adapter_type_t {
	FOREACH_CHG_EV_ADAPTER(CHG_EV_ADAPTER_ENUM)
};

enum gbms_msc_states_t {
	MSC_NONE = 0,
	MSC_SEED,
	MSC_DSG,
	MSC_LAST,
	MSC_VSWITCH,
	MSC_VOVER,
	MSC_PULLBACK,
	MSC_FAST,
	MSC_TYPE,
	MSC_DLY,	/* in taper */
	MSC_STEADY,	/* in taper */
	MSC_TIERCNTING, /* in taper */
	MSC_RAISE,	/* in taper */
	MSC_WAIT,	/* in taper */
	MSC_RSTC,	/* in taper */
	MSC_NEXT,	/* in taper */
	MSC_NYET,	/* in taper */
	MSC_STATES_COUNT,
};

union gbms_ce_adapter_details {
	uint32_t	v;
	struct {
		uint8_t		ad_type;
		uint8_t		pad;
		uint8_t 	ad_voltage;
		uint8_t 	ad_amperage;
	};
};

struct gbms_ce_stats {
	uint16_t 	voltage_in;
	uint16_t	ssoc_in;
	uint16_t	cc_in;
	uint16_t 	voltage_out;
	uint16_t 	ssoc_out;
	uint16_t	cc_out;
};

struct ttf_tier_stat {
	int16_t soc_in;
	int	cc_in;
	int	cc_total;
	ktime_t	avg_time;
};

struct gbms_ce_tier_stats {
	int8_t		temp_idx;
	uint8_t		vtier_idx;

	int16_t		soc_in;		/* 8.8 */
	uint16_t	cc_in;
	uint16_t	cc_total;

	uint16_t	time_fast;
	uint16_t	time_taper;
	uint16_t	time_other;

	int16_t		temp_in;
	int16_t		temp_min;
	int16_t		temp_max;

	int16_t		ibatt_min;
	int16_t		ibatt_max;

	uint16_t	icl_min;
	uint16_t	icl_max;

	int64_t		icl_sum;
	int64_t		temp_sum;
	int64_t		ibatt_sum;
	uint32_t 	sample_count;

	uint16_t 	msc_cnt[MSC_STATES_COUNT];
	uint32_t 	msc_elap[MSC_STATES_COUNT];
};

#define GBMS_STATS_TIER_COUNT	3
#define GBMS_SOC_STATS_LEN	101

/* time to full */

/* collected in charging event */
struct ttf_soc_stats {
	int ti[GBMS_SOC_STATS_LEN];		/* charge tier at each soc */
	int cc[GBMS_SOC_STATS_LEN];		/* coulomb count at each soc */
	ktime_t elap[GBMS_SOC_STATS_LEN];	/* time spent at soc */
};

/* reference data for soc estimation  */
struct ttf_adapter_stats {
	u32 *soc_table;
	u32 *elap_table;
	int table_count;
};

/* updated when the device publish the charge stats
 * NOTE: soc_stats and tier_stats are only valid for the given chg_profile
 * since tier, coulumb count and elap time spent at each SOC depends on the
 * maximum amout of current that can be pushed to the battery.
 */
struct batt_ttf_stats {
	ktime_t ttf_fake;

	struct ttf_soc_stats soc_ref;	/* gold: soc->elap,cc */
	int ref_temp_idx;
	int ref_watts;

	struct ttf_soc_stats soc_stats; /* rolling */
	struct ttf_tier_stat tier_stats[GBMS_STATS_TIER_COUNT];

	struct logbuffer *ttf_log;
};

struct gbms_charging_event {
	union gbms_ce_adapter_details	adapter_details;

	/* profile used for this charge event */
	const struct gbms_chg_profile *chg_profile;
	/* charge event and tier tracking */
	struct gbms_ce_stats		charging_stats;
	struct gbms_ce_tier_stats	tier_stats[GBMS_STATS_TIER_COUNT];
	/* soc tracking for time to full */
	struct ttf_soc_stats soc_stats;
	int last_soc;

	ktime_t first_update;
	ktime_t last_update;
	uint32_t chg_sts_qual_time;
	uint32_t chg_sts_delta_soc;
};

#define GBMS_CCCM_LIMITS(profile, ti, vi) \
	profile->cccm_limits[(ti * profile->volt_nb_limits) + vi]

/* newgen charging */
#define GBMS_CS_FLAG_BUCK_EN    (1 << 0)
#define GBMS_CS_FLAG_DONE       (1 << 1)
#define GBMS_CS_FLAG_CC       	(1 << 2)
#define GBMS_CS_FLAG_CV       	(1 << 3)
#define GBMS_CS_FLAG_ILIM       (1 << 4)
#define GBMS_CS_FLAG_NOCOMP     (1 << 5)

union gbms_charger_state {
	uint64_t v;
	struct {
		uint8_t flags;
		uint8_t pad;
		uint8_t chg_status;
		uint8_t chg_type;
		uint16_t vchrg;
		uint16_t icl;
	} f;
};

int gbms_init_chg_profile_internal(struct gbms_chg_profile *profile,
			  struct device_node *node, const char *owner_name);
#define gbms_init_chg_profile(p, n) \
	gbms_init_chg_profile_internal(p, n, KBUILD_MODNAME)

void gbms_init_chg_table(struct gbms_chg_profile *profile, u32 capacity);

void gbms_free_chg_profile(struct gbms_chg_profile *profile);

void gbms_dump_raw_profile(const struct gbms_chg_profile *profile, int scale);
#define gbms_dump_chg_profile(profile) gbms_dump_raw_profile(profile, 1000)

/* newgen charging: charge profile */
int gbms_msc_temp_idx(const struct gbms_chg_profile *profile, int temp);
int gbms_msc_voltage_idx(const struct gbms_chg_profile *profile, int vbatt);
int gbms_msc_round_fv_uv(const struct gbms_chg_profile *profile,
			   int vtier, int fv_uv);

/* newgen charging: charger flags  */
uint8_t gbms_gen_chg_flags(int chg_status, int chg_type);
/* newgen charging: read/gen charger state  */
int gbms_read_charger_state(union gbms_charger_state *chg_state,
			    struct power_supply *chg_psy);

/* debug/print */
const char *gbms_chg_type_s(int chg_type);
const char *gbms_chg_status_s(int chg_status);
const char *gbms_chg_ev_adapter_s(int adapter);

/* Votables */
#define VOTABLE_MSC_CHG_DISABLE	"MSC_CHG_DISABLE"
#define VOTABLE_MSC_PWR_DISABLE	"MSC_PWR_DISABLE"
#define VOTABLE_MSC_INTERVAL	"MSC_INTERVAL"
#define VOTABLE_MSC_FCC		"MSC_FCC"
#define VOTABLE_MSC_FV		"MSC_FV"

/* Binned cycle count */
#define GBMS_CCBIN_CSTR_SIZE	(GBMS_CCBIN_BUCKET_COUNT * 6 + 2)

int gbms_cycle_count_sscan_bc(u16 *ccount, int bcnt, const char *buff);
int gbms_cycle_count_cstr_bc(char *buff, size_t size,
					const u16 *ccount, int bcnt);

#define gbms_cycle_count_sscan(cc, buff) \
	gbms_cycle_count_sscan_bc(cc, GBMS_CCBIN_BUCKET_COUNT, buff)

#define gbms_cycle_count_cstr(buff, size, cc)	\
	gbms_cycle_count_cstr_bc(buff, size, cc, GBMS_CCBIN_BUCKET_COUNT)


/* Time to full */
int ttf_soc_cstr(char *buff, int size, const struct ttf_soc_stats *soc_stats,
		 int start, int end);

int ttf_soc_estimate(ktime_t *res,
		     const struct batt_ttf_stats *stats,
		     const struct gbms_charging_event *ce_data,
		     qnum_t soc, qnum_t last);

void ttf_soc_init(struct ttf_soc_stats *dst);

int ttf_tier_cstr(char *buff, int size, const struct ttf_tier_stat *t_stat);

int ttf_tier_estimate(ktime_t *res,
		      const struct batt_ttf_stats *ttf_stats,
		      int temp_idx, int vbatt_idx,
		      int capacity, int full_capacity);

int ttf_stats_init(struct batt_ttf_stats *stats,
		   struct device *device,
		   int capacity_ma);

void ttf_stats_update(struct batt_ttf_stats *stats,
	 	      struct gbms_charging_event *ce_data,
		      bool force);

int ttf_stats_cstr(char *buff, int size, const struct batt_ttf_stats *stats,
		   bool verbose);

int ttf_stats_sscan(struct batt_ttf_stats *stats,
		    const char *buff, size_t size);

struct batt_ttf_stats *ttf_stats_dup(struct batt_ttf_stats *dst,
				     const struct batt_ttf_stats *src);

void ttf_log(const struct batt_ttf_stats *stats, const char *fmt, ...);

ssize_t ttf_dump_details(char *buf, int max_size,
			 const struct batt_ttf_stats *ttf_stats,
			 int last_soc);

/*
 * Charger modes
 *
 */

enum gbms_charger_modes {
	GBMS_CHGR_MODE_CHGR_DC	= 0x20,

	GBMS_USB_BUCK_ON	= 0x30,
	GBMS_USB_OTG_ON 	= 0x31,
	GBMS_USB_OTG_FRS_ON	= 0x32,

	GBMS_CHGR_MODE_WLC_TX	= 0x40,
};

#define GBMS_MODE_VOTABLE "CHARGER_MODE"

#endif  /* __GOOGLE_BMS_H_ */
