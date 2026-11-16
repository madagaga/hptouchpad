// SPDX-License-Identifier: GPL-2.0
/*
 * PWM driver for the LPG (low power generator) channels of the Qualcomm PM8058, used for the backlight of the
 * HP TouchPad.
 *
 * The register layout comes from the vendor driver (drivers/misc/pmic8058-pwm.c of the 3.4 kernel). Only the
 * constant duty mode is implemented (look-up table bypassed, 9-bit resolution). The 8 channels share the
 * registers LPG_CTL(0..6): the channel is selected with LPG_BANK_SEL first. A channel output is switched on in
 * LPG_BANK_EN and by LPG_CTL(0).
 *
 * Output period = 512 * pre_divide * 2^m / clock, with the clock 1 kHz, 32.768 kHz or 19.2 MHz,
 * pre_divide 2, 3 or 5 and m 0..7.
 */
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

#define LPG_NUM_CHANNELS	8

#define LPG_CTL(n)		(0x13c + (n))
#define LPG_BANK_SEL		0x143
#define LPG_BANK_EN		0x144

#define LPG_CTL0_OUTPUT_EN	BIT(3)
#define LPG_CTL0_PWM_EN		BIT(2)

#define LPG_CTL1_BYPASS_LUT	BIT(6)

#define LPG_CTL4_VALUE_BIT8	BIT(7)
#define LPG_CTL4_CLK_SHIFT	5
#define LPG_CTL4_DIV_SHIFT	3

#define LPG_CTL5_SIZE_9_BIT	BIT(0)

#define LPG_RESOLUTION		512	/* 9 bit */
#define LPG_MAX_VALUE		(LPG_RESOLUTION - 1)
#define LPG_M_MAX		7

static const unsigned int lpg_clk_rates[] = { 1000, 32768, 19200000 };	/* clock select 1..3 */
static const unsigned int lpg_pre_div[] = { 2, 3, 5 };			/* predivide field 0..2 */

struct pm8058_lpg {
	struct regmap *map;
	struct mutex lock;	/* LPG_BANK_SEL selects the channel for the following register accesses */
	u8 bank_en;
};

static u64 lpg_hw_period_ns(unsigned int clk, unsigned int div, unsigned int m)
{
	u64 ticks = (u64)LPG_RESOLUTION * lpg_pre_div[div] << m;

	return div_u64(ticks * NSEC_PER_SEC, lpg_clk_rates[clk]);
}

/*
 * Largest hardware period that does not exceed the requested one. The shortest one is 53.3 us (19.2 MHz,
 * pre-divide 2, 9 bit): when the request is shorter, as the 50 us (20 kHz) of the TouchPad backlight, use it anyway
 * like the vendor driver does (it picks the closest period); the duty cycle is a ratio and stays correct.
 */
static void lpg_pick_period(u64 period_ns, unsigned int *clk, unsigned int *div, unsigned int *m)
{
	u64 best = 0, shortest = U64_MAX, hw;
	unsigned int c, d, e;

	for (c = 0; c < ARRAY_SIZE(lpg_clk_rates); c++) {
		for (d = 0; d < ARRAY_SIZE(lpg_pre_div); d++) {
			for (e = 0; e <= LPG_M_MAX; e++) {
				hw = lpg_hw_period_ns(c, d, e);
				if (hw <= period_ns && hw > best) {
					best = hw;
					*clk = c;
					*div = d;
					*m = e;
				}
				if (!best && hw < shortest) {
					shortest = hw;
					*clk = c;
					*div = d;
					*m = e;
				}
			}
		}
	}
}

static int pm8058_lpg_apply(struct pwm_chip *chip, struct pwm_device *pwm, const struct pwm_state *state)
{
	struct pm8058_lpg *lpg = pwmchip_get_drvdata(chip);
	unsigned int id = pwm->hwpwm;
	unsigned int clk = 0, div = 0, m = 0;
	u64 period = state->period;
	u32 value;
	int ret;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	mutex_lock(&lpg->lock);

	ret = regmap_write(lpg->map, LPG_BANK_SEL, id);
	if (ret)
		goto out;

	if (!state->enabled) {
		ret = regmap_write(lpg->map, LPG_CTL(0), 0);
		if (ret)
			goto out;

		lpg->bank_en &= ~BIT(id);
		ret = regmap_write(lpg->map, LPG_BANK_EN, lpg->bank_en);
		goto out;
	}

	lpg_pick_period(period, &clk, &div, &m);

	value = div64_u64(min(state->duty_cycle, period) * LPG_RESOLUTION, period);
	value = min_t(u32, value, LPG_MAX_VALUE);

	/* LPG_CTL(5) .. LPG_CTL(1) first, LPG_CTL(0) (output enable) last, as the vendor driver does */
	ret = regmap_write(lpg->map, LPG_CTL(5), LPG_CTL5_SIZE_9_BIT);
	if (ret)
		goto out;

	ret = regmap_write(lpg->map, LPG_CTL(4), ((value >> 1) & LPG_CTL4_VALUE_BIT8) |
			   ((clk + 1) << LPG_CTL4_CLK_SHIFT) | (div << LPG_CTL4_DIV_SHIFT) | m);
	if (ret)
		goto out;

	ret = regmap_write(lpg->map, LPG_CTL(3), value & 0xff);
	if (ret)
		goto out;

	ret = regmap_write(lpg->map, LPG_CTL(2), 0);
	if (ret)
		goto out;

	ret = regmap_write(lpg->map, LPG_CTL(1), LPG_CTL1_BYPASS_LUT);
	if (ret)
		goto out;

	lpg->bank_en |= BIT(id);
	ret = regmap_write(lpg->map, LPG_BANK_EN, lpg->bank_en);
	if (ret)
		goto out;

	/* LPG_BANK_SEL is still the channel */
	ret = regmap_write(lpg->map, LPG_CTL(0), LPG_CTL0_OUTPUT_EN | LPG_CTL0_PWM_EN);
out:
	mutex_unlock(&lpg->lock);

	return ret;
}

static const struct pwm_ops pm8058_lpg_ops = {
	.apply = pm8058_lpg_apply,
};

static int pm8058_lpg_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct pm8058_lpg *lpg;
	struct regmap *map;

	map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!map)
		return dev_err_probe(&pdev->dev, -ENODEV, "no regmap from the PM8058\n");

	chip = devm_pwmchip_alloc(&pdev->dev, LPG_NUM_CHANNELS, sizeof(*lpg));
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	lpg = pwmchip_get_drvdata(chip);
	lpg->map = map;
	mutex_init(&lpg->lock);

	chip->ops = &pm8058_lpg_ops;

	return devm_pwmchip_add(&pdev->dev, chip);
}

static const struct of_device_id pm8058_lpg_of_match[] = {
	{ .compatible = "qcom,pm8058-lpg" },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8058_lpg_of_match);

static struct platform_driver pm8058_lpg_driver = {
	.driver = {
		.name = "pm8058-lpg",
		.of_match_table = pm8058_lpg_of_match,
	},
	.probe = pm8058_lpg_probe,
};
module_platform_driver(pm8058_lpg_driver);

MODULE_DESCRIPTION("Qualcomm PM8058 LPG PWM driver");
MODULE_LICENSE("GPL");
