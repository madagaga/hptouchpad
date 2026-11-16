// SPDX-License-Identifier: GPL-2.0
/*
 * Core voltage (vdd_sc) of the two Scorpion cores of the Qualcomm MSM8660/APQ8060 (HP TouchPad).
 *
 * Each core is supplied by its own PM8901 FTSMPS (S0 for core 0, S1 for core 1), driven through the SAW block of
 * the core: the SAW sends the code written in its VCTL register to the PMIC and reports the PMIC answer in STS.
 * The sequence comes from the vendor kernels (arch/arm/mach-msm/spm.c msm_spm_set_vdd(), saw-regulator.c and the
 * msm_spm_data[] init values of board-msm8x60.c).
 *
 * First step: the probe only reads the registers (the bootloader leaves the SAW in its reset state). The SPM init
 * and the voltage writes are done by hand through debugfs (<debugfs>/msm8660-saw/<name>/{regs,init,vctl_uv}) until
 * the PMIC answer is understood; the regulator is registered for later use by cpufreq.
 */
#include <linux/bitfield.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define SAW_AVS_CTL		0x04
#define SAW_VCTL		0x08
#define SAW_VCTL_DATA		GENMASK(7, 0)
#define SAW_STS			0x0c
#define SAW_STS_PMIC_DATA	GENMASK(17, 10)
#define SAW_STS_PMIC_STATE	GENMASK(21, 20)
#define SAW_STS_PMIC_IDLE	0
#define SAW_CFG			0x10
#define SAW_SPM_CTL		0x14
#define SAW_SPM_SLP_TMR_DLY	0x18
#define SAW_SPM_WAKE_TMR_DLY	0x1c
#define SAW_SPM_PMIC_CTL	0x20
#define SAW_SLP_CLK_EN		0x24
#define SAW_SLP_HSFS_PRECLMP_EN	0x28
#define SAW_SLP_HSFS_POSTCLMP_EN 0x2c
#define SAW_SLP_CLMP_EN		0x30
#define SAW_SLP_RST_EN		0x34
#define SAW_SPM_MPM_CFG		0x38
#define SAW_NUM_REGS		15

/* FTSMPS band 2: 700..1400 mV in 12.5 mV steps, code = 0x80 | step */
#define FTSMPS_BAND_MASK	GENMASK(7, 6)
#define FTSMPS_BAND_2		2
#define FTSMPS_VPROG		GENMASK(5, 0)
#define FTSMPS_BAND2_MIN_UV	700000
#define FTSMPS_BAND2_STEP_UV	12500
#define FTSMPS_BAND2_N		57

/*
 * The slew rate (uV/us) is the vendor value. The vendor vctl_timeout_us (50) is not enough here: right after the
 * VCTL write, STS can still report idle with the previous code (2026-11-04: asked 0x99, read 0x8f, 0x99 a moment
 * later), so wait for idle *and* the new code.
 */
#define SAW_VCTL_TIMEOUT_US	5000	/* seen > 500 us on SAW0 (2026-11-04) */
#define SAW_SLEW_UV_PER_US	1250

struct saw_init {
	u32 offset;
	u32 value[2];		/* core 0, core 1 */
};

/* msm_spm_data[] of board-msm8x60.c (SoC v2 and later); AVS stays as the vendor code leaves it at probe */
static const struct saw_init saw_init_values[] = {
	{ SAW_AVS_CTL,			{ 0x586020ff, 0x586020ff } },
	{ SAW_CFG,			{ 0x1c, 0x1c } },
	{ SAW_SPM_CTL,			{ 0x68, 0x68 } },
	{ SAW_SPM_SLP_TMR_DLY,		{ 0x0c0cffff, 0x0c0cffff } },
	{ SAW_SPM_WAKE_TMR_DLY,		{ 0x78780fff, 0x78780fff } },
	{ SAW_SLP_CLK_EN,		{ 0x01, 0x13 } },
	{ SAW_SLP_HSFS_PRECLMP_EN,	{ 0x07, 0x07 } },
	{ SAW_SLP_HSFS_POSTCLMP_EN,	{ 0x00, 0x00 } },
	{ SAW_SLP_CLMP_EN,		{ 0x01, 0x01 } },
	{ SAW_SLP_RST_EN,		{ 0x00, 0x00 } },
	{ SAW_SPM_MPM_CFG,		{ 0x00, 0x00 } },
};

struct saw_vreg {
	struct device *dev;
	void __iomem *base;
	u32 core;
	struct regulator_desc desc;
	struct regulator_dev *rdev;
	struct dentry *debugfs;
};

static void saw_dump(struct saw_vreg *vreg, const char *when)
{
	u32 sts = readl(vreg->base + SAW_STS);

	dev_info(vreg->dev, "%s: AVS_CTL %08x VCTL %08x STS %08x (pmic state %lu data %02lx) CFG %08x SPM_CTL %08x PMIC_CTL %08x\n",
		 when, readl(vreg->base + SAW_AVS_CTL), readl(vreg->base + SAW_VCTL), sts,
		 FIELD_GET(SAW_STS_PMIC_STATE, sts), FIELD_GET(SAW_STS_PMIC_DATA, sts),
		 readl(vreg->base + SAW_CFG), readl(vreg->base + SAW_SPM_CTL),
		 readl(vreg->base + SAW_SPM_PMIC_CTL));
}

static void saw_spm_init(struct saw_vreg *vreg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(saw_init_values); i++)
		writel(saw_init_values[i].value[vreg->core], vreg->base + saw_init_values[i].offset);
	/* the vendor code writes the shadow registers in order and relies on the barrier of the last write */
	mb();
}

/* msm_spm_set_vdd(): write the code, wait for the PMIC to be idle again, check its answer. Runs on the SAW's core. */
static int saw_write_code_local(struct saw_vreg *vreg, u8 code)
{
	u32 vctl, sts;
	int timeout = SAW_VCTL_TIMEOUT_US;

	vctl = readl(vreg->base + SAW_VCTL);
	vctl = (vctl & ~SAW_VCTL_DATA) | code;
	writel(vctl, vreg->base + SAW_VCTL);
	mb();

	for (;;) {
		sts = readl(vreg->base + SAW_STS);
		if (FIELD_GET(SAW_STS_PMIC_STATE, sts) == SAW_STS_PMIC_IDLE &&
		    FIELD_GET(SAW_STS_PMIC_DATA, sts) == code)
			return 0;
		if (timeout <= 0)
			break;
		usleep_range(10, 20);
		timeout -= 10;
	}

	/*
	 * Going down, a late answer is harmless: the core keeps a higher voltage than it needs for a while
	 * (2026-11-05: 1 of 400 drops still read the old code after 5 ms). Failing would abort the cpufreq change.
	 */
	if (code < FIELD_GET(SAW_STS_PMIC_DATA, sts)) {
		dev_dbg(vreg->dev, "code %02x: PMIC still at %02lx after %d us (lowering, ignored)\n",
			code, FIELD_GET(SAW_STS_PMIC_DATA, sts), SAW_VCTL_TIMEOUT_US);
		return 0;
	}
	dev_err(vreg->dev, "code %02x: PMIC state %lu data %02lx after %d us (STS %08x, written from CPU%d)\n",
		code, FIELD_GET(SAW_STS_PMIC_STATE, sts), FIELD_GET(SAW_STS_PMIC_DATA, sts),
		SAW_VCTL_TIMEOUT_US, sts, raw_smp_processor_id());
	return -ETIMEDOUT;
}

struct saw_write_req {
	struct saw_vreg *vreg;
	u8 code;
};

static long saw_write_fn(void *arg)
{
	struct saw_write_req *req = arg;

	return saw_write_code_local(req->vreg, req->code);
}

/*
 * The vendor cpufreq driver always runs acpuclk_set_rate() on the core being changed (queue_work_on(policy->cpu)),
 * so a SAW is only ever written by its own core. cpufreq-dt calls us from any core: with cross-core writes about 1 of
 * 400 requests was not taken (2026-11-05, STS idle with the old code after 5 ms). Do the write on the SAW's core.
 */
static int saw_write_code(struct saw_vreg *vreg, u8 code)
{
	struct saw_write_req req = { .vreg = vreg, .code = code };

	/*
	 * No cpus_read_lock(): cpufreq may already hold it and a pending hotplug writer would deadlock a nested read.
	 * Nothing offlines the cores here; an offline core (its SAW idle) is written locally.
	 */
	if (cpu_online(vreg->core) && vreg->core != raw_smp_processor_id())
		return work_on_cpu(vreg->core, saw_write_fn, &req);
	return saw_write_code_local(vreg, code);
}

static int saw_get_voltage_sel(struct regulator_dev *rdev)
{
	struct saw_vreg *vreg = rdev_get_drvdata(rdev);
	u32 code = FIELD_GET(SAW_STS_PMIC_DATA, readl(vreg->base + SAW_STS));

	/* not a band 2 code: the SAW was never used since reset, the voltage is unknown */
	if (FIELD_GET(FTSMPS_BAND_MASK, code) != FTSMPS_BAND_2)
		return -ENOTRECOVERABLE;
	return FIELD_GET(FTSMPS_VPROG, code);
}

static int saw_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	struct saw_vreg *vreg = rdev_get_drvdata(rdev);

	return saw_write_code(vreg, FIELD_PREP(FTSMPS_BAND_MASK, FTSMPS_BAND_2) | sel);
}

static const struct regulator_ops saw_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= saw_get_voltage_sel,
	.set_voltage_sel	= saw_set_voltage_sel,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
};

static int saw_regs_show(struct seq_file *s, void *unused)
{
	struct saw_vreg *vreg = s->private;
	int i;

	for (i = 0; i < SAW_NUM_REGS; i++)
		seq_printf(s, "%02x: %08x\n", i * 4, readl(vreg->base + i * 4));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(saw_regs);

static int saw_init_set(void *data, u64 val)
{
	struct saw_vreg *vreg = data;

	if (val != 1)
		return -EINVAL;
	saw_dump(vreg, "before init");
	saw_spm_init(vreg);
	saw_dump(vreg, "after init");
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(saw_init_fops, NULL, saw_init_set, "%llu\n");

/* direct write, bypassing the regulator core: for the first tests only, limited to the DT constraints */
static int saw_vctl_uv_set(void *data, u64 val)
{
	struct saw_vreg *vreg = data;
	const struct regulation_constraints *c = vreg->rdev->constraints;
	int ret;
	u32 uv;

	if (val < c->min_uV || val > c->max_uV)
		return -EINVAL;
	uv = val;
	if ((uv - FTSMPS_BAND2_MIN_UV) % FTSMPS_BAND2_STEP_UV)
		return -EINVAL;
	ret = saw_write_code(vreg, FIELD_PREP(FTSMPS_BAND_MASK, FTSMPS_BAND_2) |
			     ((uv - FTSMPS_BAND2_MIN_UV) / FTSMPS_BAND2_STEP_UV));
	saw_dump(vreg, "after vctl");
	return ret;
}

static int saw_vctl_uv_get(void *data, u64 *uv)
{
	struct saw_vreg *vreg = data;
	int sel = saw_get_voltage_sel(vreg->rdev);

	*uv = sel < 0 ? 0 : FTSMPS_BAND2_MIN_UV + sel * FTSMPS_BAND2_STEP_UV;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(saw_vctl_uv_fops, saw_vctl_uv_get, saw_vctl_uv_set, "%llu\n");

/*
 * A SAW that does not hold a band 2 code makes the voltage unreadable (-ENOTRECOVERABLE) and the regulator core refuses
 * the regulator, so cpufreq-dt never probes and the cores stay at the boot frequency. Seen 2026-11-08 on SAW0 after a
 * restart: STS data 0xfd, AVS_CTL 0x10241bff. Put the highest allowed voltage (safe at any frequency, cpufreq lowers it
 * afterwards); if the SAW does not take it, apply the vendor SPM init first.
 */
static int saw_boot_code(struct saw_vreg *vreg, const struct regulation_constraints *c)
{
	u32 code = FIELD_GET(SAW_STS_PMIC_DATA, readl(vreg->base + SAW_STS));
	u8 safe;
	int ret;

	if (FIELD_GET(FTSMPS_BAND_MASK, code) == FTSMPS_BAND_2)
		return 0;
	if (!c->max_uV || c->max_uV < FTSMPS_BAND2_MIN_UV)
		return -EINVAL;
	safe = FIELD_PREP(FTSMPS_BAND_MASK, FTSMPS_BAND_2) |
	       min_t(u32, DIV_ROUND_UP(c->max_uV - FTSMPS_BAND2_MIN_UV, FTSMPS_BAND2_STEP_UV), FTSMPS_BAND2_N - 1);
	dev_warn(vreg->dev, "PMIC data %02x is not a band 2 code: writing %02x (%d uV)\n", code, safe, c->max_uV);
	ret = saw_write_code(vreg, safe);
	if (ret) {
		saw_spm_init(vreg);
		ret = saw_write_code(vreg, safe);
	}
	saw_dump(vreg, ret ? "boot code failed" : "after boot code");
	return ret;
}

static int saw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regulator_config config = { };
	struct saw_vreg *vreg;
	struct dentry *root;

	vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;
	vreg->dev = dev;
	platform_set_drvdata(pdev, vreg);

	vreg->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vreg->base))
		return PTR_ERR(vreg->base);

	if (of_property_read_u32(dev->of_node, "qcom,cpu-index", &vreg->core) || vreg->core > 1)
		return dev_err_probe(dev, -EINVAL, "missing or bad qcom,cpu-index\n");

	saw_dump(vreg, "boot state");

	vreg->desc.name = devm_kasprintf(dev, GFP_KERNEL, "saw%u", vreg->core);
	vreg->desc.owner = THIS_MODULE;
	vreg->desc.type = REGULATOR_VOLTAGE;
	vreg->desc.ops = &saw_ops;
	vreg->desc.min_uV = FTSMPS_BAND2_MIN_UV;
	vreg->desc.uV_step = FTSMPS_BAND2_STEP_UV;
	vreg->desc.n_voltages = FTSMPS_BAND2_N;
	vreg->desc.ramp_delay = SAW_SLEW_UV_PER_US;

	config.dev = dev;
	config.driver_data = vreg;
	config.of_node = dev->of_node;
	config.init_data = of_get_regulator_init_data(dev, dev->of_node, &vreg->desc);
	if (!config.init_data)
		return -ENOMEM;
	if (saw_boot_code(vreg, &config.init_data->constraints))
		return dev_err_probe(dev, -EIO, "cannot set a known voltage\n");

	vreg->rdev = devm_regulator_register(dev, &vreg->desc, &config);
	if (IS_ERR(vreg->rdev))
		return dev_err_probe(dev, PTR_ERR(vreg->rdev), "regulator registration failed\n");

	root = debugfs_lookup("msm8660-saw", NULL);
	if (!root)
		root = debugfs_create_dir("msm8660-saw", NULL);
	vreg->debugfs = debugfs_create_dir(vreg->desc.name, root);
	debugfs_create_file("regs", 0444, vreg->debugfs, vreg, &saw_regs_fops);
	debugfs_create_file("init", 0200, vreg->debugfs, vreg, &saw_init_fops);
	debugfs_create_file("vctl_uv", 0600, vreg->debugfs, vreg, &saw_vctl_uv_fops);
	return 0;
}

static void saw_remove(struct platform_device *pdev)
{
	struct saw_vreg *vreg = platform_get_drvdata(pdev);

	if (vreg)
		debugfs_remove_recursive(vreg->debugfs);
}

static const struct of_device_id saw_of_match[] = {
	{ .compatible = "qcom,msm8660-saw-regulator" },
	{ }
};
MODULE_DEVICE_TABLE(of, saw_of_match);

static struct platform_driver saw_driver = {
	.probe = saw_probe,
	.remove = saw_remove,
	.driver = {
		.name = "msm8660-saw-regulator",
		.of_match_table = saw_of_match,
	},
};
module_platform_driver(saw_driver);

MODULE_DESCRIPTION("MSM8660 SAW core voltage regulator");
MODULE_LICENSE("GPL");
