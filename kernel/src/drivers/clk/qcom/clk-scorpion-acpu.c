// SPDX-License-Identifier: GPL-2.0
/*
 * CPU clocks of the two Scorpion cores of the Qualcomm MSM8660/APQ8060 (HP TouchPad).
 *
 * Each core has its own SCPLL (a PLL with a calibration table and a frequency state machine) selected by the
 * core clock mux of its ACC block. The sequence comes from the vendor driver (arch/arm/mach-msm/acpuclock-8x60.c,
 * scpll_change_freq()): frequency = 2 * 27 MHz * L, a "complex slew" moves the PLL to a new L without stopping
 * the core.
 *
 * Only SCPLL to SCPLL changes between 432 and 1188 MHz are implemented. The core voltage is set by cpufreq-dt through
 * the OPP table (cpu-supply = the SAW regulator of the core, msm8660-saw-regulator) before a raise and after a
 * drop. The other sources (PLL8) are not handled.
 *
 * L2 (vendor l2_freq_tbl_v2 of the TouchPad, compute_l2_speed/set_l2_speed): the L2 has its own SCPLL, programmed like
 * the cores, and runs at the rate of the fastest core. Before the L2 goes up (and after it goes down) this driver
 * moves what the vendor ties to the L2 level:
 *  - vdd_dig (PM8058 S1 through the RPM): 1.2 V at L2 1188 MHz, 1.1 V below. The vendor goes down to 1.0 V up to
 *    648 MHz, but there it also counts the votes of the other drivers (GPU, display); here nobody else votes, so
 *    1.1 V is the floor;
 *  - EBI1 bandwidth (RPM EBI1 clock): vendor levels 1336 / 2008 / 2480 MB/s (L2 <= 864, 918-1134, 1188 MHz) at 8 bytes
 *    per cycle, 167 / 251 / 310 MHz.
 * vdd_mem (PM8058 S0) is held at the vendor maximum, 1.2 V, in the DT: it must stay above vdd_sc, and cpufreq-dt
 * raises vdd_sc before the clock change, so it cannot follow the L2 without a second regulator in cpufreq.
 */
#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define NUM_CORES		2

/* ACC block of each core */
#define ACC_CLK_SEL		0x08
#define ACC_CLK_SEL_CORE_SRC	GENMASK(2, 1)
#define ACC_SRC_SCPLL		1

/* SCPLL of each core, offsets from the global clock controller registers */
#define SCPLL_BASE(n)		(0x3200 + (n) * 0x100)
#define SCPLL_CTL		0x04
#define SCPLL_CTL_MODE		GENMASK(2, 0)
#define SCPLL_CTL_L		GENMASK(8, 3)
#define SCPLL_STATUS		0x10
#define SCPLL_STATUS_SLEWING	BIT(0)
#define SCPLL_FSM_CTL_EXT	0x24
#define SCPLL_FSM_L		GENMASK(8, 3)
#define SCPLL_FSM_MODE		GENMASK(2, 0)

#define SCPLL_MODE_NORMAL	7
#define SCPLL_COMPLEX_SLEW	7

#define SCPLL_REF_HZ		(2 * 27000000UL)
#define L_MIN			8	/* 432 MHz: the bootloader calibrated L = 8 .. 28 */
#define L_MAX			22	/* 1188 MHz: HP top step (acpu_freq_tbl_1188mhz); the bootloader calibrated SCPLL up to L 28 */

#define POLL_TIMEOUT_US		10000

/* L2: SCPLL index 2; source select SPSS_L2_CLK_SEL in the apps GCC (0x02082000, third reg), 1 = SCPLL */
#define L2_ID			2
#define APPS_GCC_L2_CLK_SEL	0x38
#define L2_SRC_MASK		GENMASK(1, 0)
#define L2_SRC_SCPLL		1

#define VDD_DIG_NOMINAL_UV	1100000
#define VDD_DIG_HIGH_UV		1200000
#define L2_HIGH_HZ		1188000000UL

struct acpu_clk {
	struct clk_hw hw;
	struct acpu_drv *drv;
	unsigned int id;
	void __iomem *acc;
};

struct acpu_drv {
	struct device *dev;
	struct regmap *gcc;
	struct acpu_clk cores[NUM_CORES];
	unsigned long core_hz[NUM_CORES];	/* last rate set (or found at probe) */
	unsigned long l2_hz;			/* 0: L2 not on its SCPLL, left alone */
	struct regulator *vdd_dig;
	struct clk *ebi1;
};

#define to_acpu_clk(_hw)	container_of(_hw, struct acpu_clk, hw)

static bool acpu_on_scpll(struct acpu_clk *c)
{
	return FIELD_GET(ACC_CLK_SEL_CORE_SRC, readl(c->acc + ACC_CLK_SEL)) == ACC_SRC_SCPLL;
}

static unsigned long acpu_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct acpu_clk *c = to_acpu_clk(hw);
	unsigned int ctl;

	if (!acpu_on_scpll(c))
		return 0;

	if (regmap_read(c->drv->gcc, SCPLL_BASE(c->id) + SCPLL_CTL, &ctl))
		return 0;

	return FIELD_GET(SCPLL_CTL_L, ctl) * SCPLL_REF_HZ;
}

static int acpu_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	unsigned long l = clamp(req->rate / SCPLL_REF_HZ, (unsigned long)L_MIN, (unsigned long)L_MAX);

	req->rate = l * SCPLL_REF_HZ;

	return 0;
}

static int scpll_slew(struct regmap *gcc, unsigned int id, unsigned int l)
{
	unsigned int base = SCPLL_BASE(id);
	unsigned int val;
	int ret;

	ret = regmap_write(gcc, base + SCPLL_FSM_CTL_EXT,
			   FIELD_PREP(SCPLL_FSM_L, l) | FIELD_PREP(SCPLL_FSM_MODE, SCPLL_COMPLEX_SLEW));
	if (ret)
		return ret;

	ret = regmap_write(gcc, base + SCPLL_CTL, SCPLL_MODE_NORMAL);
	if (ret)
		return ret;

	/* the switch starts: the PLL reports the new L, then it stops slewing */
	ret = regmap_read_poll_timeout_atomic(gcc, base + SCPLL_CTL, val, FIELD_GET(SCPLL_CTL_L, val) == l,
					      1, POLL_TIMEOUT_US);
	if (ret)
		return ret;

	return regmap_read_poll_timeout_atomic(gcc, base + SCPLL_STATUS, val, !(val & SCPLL_STATUS_SLEWING),
					       1, POLL_TIMEOUT_US);
}

static unsigned long l2_ebi1_hz(unsigned long l2_hz)
{
	if (l2_hz >= L2_HIGH_HZ)
		return 310000000;
	if (l2_hz > 864000000)
		return 251000000;
	return 167000000;
}

/* supplies and bus for an L2 rate; called before a raise and after a drop */
static int l2_votes(struct acpu_drv *drv, unsigned long l2_hz)
{
	int uv = l2_hz >= L2_HIGH_HZ ? VDD_DIG_HIGH_UV : VDD_DIG_NOMINAL_UV;
	int ret;

	ret = regulator_set_voltage(drv->vdd_dig, uv, VDD_DIG_HIGH_UV);
	if (ret) {
		dev_err(drv->dev, "vdd_dig %d uV: %d\n", uv, ret);
		return ret;
	}
	ret = clk_set_rate(drv->ebi1, l2_ebi1_hz(l2_hz));
	if (ret)
		dev_err(drv->dev, "EBI1 %lu Hz: %d\n", l2_ebi1_hz(l2_hz), ret);
	return ret;
}

static int acpu_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct acpu_clk *c = to_acpu_clk(hw);
	struct acpu_drv *drv = c->drv;
	unsigned int l = rate / SCPLL_REF_HZ;
	unsigned long l2_new;
	int ret;

	if (l < L_MIN || l > L_MAX || !acpu_on_scpll(c))
		return -EINVAL;

	/* the L2 follows the fastest core (vendor compute_l2_speed) */
	l2_new = max(rate, drv->core_hz[!c->id]);
	if (drv->l2_hz && l2_new > drv->l2_hz) {
		ret = l2_votes(drv, l2_new);
		if (ret)
			return ret;
	}

	ret = scpll_slew(drv->gcc, c->id, l);
	if (ret)
		return ret;
	drv->core_hz[c->id] = rate;

	/* vendor order: the core first, then the L2 */
	if (drv->l2_hz && l2_new != drv->l2_hz) {
		ret = scpll_slew(drv->gcc, L2_ID, l2_new / SCPLL_REF_HZ);
		if (ret) {
			dev_err(drv->dev, "L2 to %lu Hz: %d\n", l2_new, ret);
			return ret;
		}
		if (l2_new < drv->l2_hz)
			l2_votes(drv, l2_new);	/* a failed lowering only costs power */
		drv->l2_hz = l2_new;
	}
	return 0;
}

static const struct clk_ops acpu_clk_ops = {
	.recalc_rate = acpu_recalc_rate,
	.determine_rate = acpu_determine_rate,
	.set_rate = acpu_set_rate,
};

/*
 * The bootloader leaves the second core on the slow bus clock although its SCPLL is already calibrated and in
 * normal mode: the core runs at half the speed of the first one. Put it on its SCPLL like the first core.
 */
static void acpu_select_scpll(struct acpu_clk *c)
{
	unsigned int ctl;
	u32 sel;

	if (acpu_on_scpll(c))
		return;

	if (regmap_read(c->drv->gcc, SCPLL_BASE(c->id) + SCPLL_CTL, &ctl) ||
	    FIELD_GET(SCPLL_CTL_MODE, ctl) != SCPLL_MODE_NORMAL)
		return;

	sel = readl(c->acc + ACC_CLK_SEL);
	sel &= ~ACC_CLK_SEL_CORE_SRC;
	sel |= FIELD_PREP(ACC_CLK_SEL_CORE_SRC, ACC_SRC_SCPLL);
	writel(sel, c->acc + ACC_CLK_SEL);
}

static int acpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *data;
	struct acpu_drv *drv;
	unsigned int i;
	int ret;

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	data = devm_kzalloc(dev, struct_size(data, hws, NUM_CORES), GFP_KERNEL);
	if (!drv || !data)
		return -ENOMEM;

	drv->dev = dev;
	drv->gcc = syscon_regmap_lookup_by_phandle(dev->of_node, "qcom,gcc");
	if (IS_ERR(drv->gcc))
		return dev_err_probe(dev, PTR_ERR(drv->gcc), "cannot get the clock controller registers\n");

	drv->vdd_dig = devm_regulator_get(dev, "vdd-dig");
	if (IS_ERR(drv->vdd_dig))
		return dev_err_probe(dev, PTR_ERR(drv->vdd_dig), "no vdd-dig supply\n");
	drv->ebi1 = devm_clk_get_enabled(dev, "ebi1");
	if (IS_ERR(drv->ebi1))
		return dev_err_probe(dev, PTR_ERR(drv->ebi1), "no ebi1 clock\n");
	ret = regulator_enable(drv->vdd_dig);
	if (ret)
		return dev_err_probe(dev, ret, "cannot enable vdd-dig\n");

	data->num = NUM_CORES;

	for (i = 0; i < NUM_CORES; i++) {
		struct acpu_clk *c = &drv->cores[i];
		struct clk_init_data init = {
			.ops = &acpu_clk_ops,
			.flags = CLK_GET_RATE_NOCACHE,
		};

		c->drv = drv;
		c->id = i;
		c->acc = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(c->acc))
			return PTR_ERR(c->acc);

		init.name = devm_kasprintf(dev, GFP_KERNEL, "acpu%u_clk", i);
		if (!init.name)
			return -ENOMEM;

		acpu_select_scpll(c);

		c->hw.init = &init;
		ret = devm_clk_hw_register(dev, &c->hw);
		if (ret)
			return ret;

		data->hws[i] = &c->hw;
		drv->core_hz[i] = clk_hw_get_rate(&c->hw);
	}

	/* L2: managed only if the bootloader left it on its SCPLL in normal mode (432 MHz on the TouchPad) */
	{
		unsigned int sel = 0, ctl = 0;

		void __iomem *apps_gcc = devm_platform_ioremap_resource(pdev, NUM_CORES);

		if (IS_ERR(apps_gcc))
			return dev_err_probe(dev, PTR_ERR(apps_gcc), "no apps GCC registers\n");
		sel = readl(apps_gcc + APPS_GCC_L2_CLK_SEL);
		if (FIELD_GET(L2_SRC_MASK, sel) == L2_SRC_SCPLL &&
		    !regmap_read(drv->gcc, SCPLL_BASE(L2_ID) + SCPLL_CTL, &ctl) &&
		    FIELD_GET(SCPLL_CTL_MODE, ctl) == SCPLL_MODE_NORMAL) {
			drv->l2_hz = FIELD_GET(SCPLL_CTL_L, ctl) * SCPLL_REF_HZ;
			/* votes for the highest of the L2 now and what the cores will ask first */
			ret = l2_votes(drv, max3(drv->l2_hz, drv->core_hz[0], drv->core_hz[1]));
			if (ret)
				return ret;
		}
		dev_info(dev, "L2 %lu MHz (sel %08x ctl %08x)%s, cores %lu/%lu MHz\n", drv->l2_hz / 1000000, sel, ctl,
			 drv->l2_hz ? "" : ": not on its SCPLL, left alone", drv->core_hz[0] / 1000000,
			 drv->core_hz[1] / 1000000);
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, data);
}

static const struct of_device_id acpu_of_match[] = {
	{ .compatible = "qcom,msm8660-acpu" },
	{ }
};
MODULE_DEVICE_TABLE(of, acpu_of_match);

static struct platform_driver acpu_driver = {
	.probe = acpu_probe,
	.driver = {
		.name = "scorpion-acpu-clk",
		.of_match_table = acpu_of_match,
	},
};
module_platform_driver(acpu_driver);

MODULE_DESCRIPTION("Qualcomm MSM8660 Scorpion CPU clock driver");
MODULE_LICENSE("GPL");
