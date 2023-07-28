/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google, LLC
 *
 */

#ifndef MAX77779_CHARGER_H_
#define MAX77779_CHARGER_H_

#include "max77779_usecase.h"
#include "max777x9_bcl.h"

#define MAX77779_COP_SENSE_RESISTOR_VAL 2 /* 2mOhm */
#define MAX7779_COP_WARN_THRESHOLD 105 /* Percentage */

struct max77779_chgr_data {
	struct device *dev;

	struct power_supply *psy;
	struct power_supply *wcin_psy;
	struct power_supply *chgin_psy;

	struct power_supply *wlc_psy;
	struct power_supply *fg_psy;
	struct regmap *regmap;

	struct gvotable_election *mode_votable;
	struct max77779_usecase_data uc_data;
	struct delayed_work mode_rerun_work;

	struct gvotable_election *dc_icl_votable;
	struct gvotable_election *dc_suspend_votable;

	bool charge_done;
	bool chgin_input_suspend;
	bool wcin_input_suspend;
	bool thm2_sts;

	int irq_gpio;
	int irq_int;
	bool irq_disabled;

	struct i2c_client *pmic_i2c_client;

	struct dentry *de;

	atomic_t insel_cnt;
	bool insel_clear;	/* when set, irq clears CHGINSEL_MASK */

	atomic_t early_topoff_cnt;

	struct mutex io_lock;
	bool resume_complete;
	bool init_complete;
	struct wakeup_source *usecase_wake_lock;

	int fship_dtls;
	bool online;
	bool wden;

	/* Force to change FCCM mode during OTG at high battery voltage */
	bool otg_changed;

	/* debug interface, register to read or write */
	u32 debug_reg_address;

	int chg_term_voltage;
	int chg_term_volt_debounce;
};
#endif
