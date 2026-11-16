// SPDX-License-Identifier: GPL-2.0-only
/*
 * Legacy power-up of the VIDC 1080p core of the HP TouchPad (MSM8660).
 *
 * The MSM8660 mmcc driver of this tree has no VCODEC clocks, no VED power domain and there is no interconnect provider, so the MMSS
 * clock controller registers are written directly (values of the vendor clock-8x60.c / footswitch-8x60.c and of registers read under
 * webOS while its codec ran) and the RPM fabric arbitration is the snapshot of the webOS request registers.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/qcom_rpm.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <dt-bindings/mfd/qcom-rpm.h>
#include "vidc_core.h"

#define MMSS_AHB_EN		0x008
#define MMSS_MAXI_EN		0x018
#define MMSS_MAXI_EN3		0x02c
#define MMSS_SAXI_EN		0x030
#define MMSS_AHB_EN2		0x038
#define MMSS_VCODEC_AXI_AB	0x114	/* bit 25 vcodec_axi_a_clk, bit 23 vcodec_axi_b_clk */
#define MMSS_VCODEC_CC		0x0f8
#define MMSS_VCODEC_MD0		0x0fc
#define MMSS_VCODEC_NS		0x100
#define MMSS_VED_GFS_CTL	0x194	/* video core footswitch */
#define MMSS_SW_RESET_AXI	0x208
#define MMSS_SW_RESET_AHB	0x20c
#define MMSS_SW_RESET_CORE	0x210

#define GFS_ENABLE		BIT(8)
#define GFS_CLAMP		BIT(5)
#define VCODEC_CC_ON		0xa5	/* enable, root enable, MND enable, MND mode */
#define VCODEC_MD0_WEBOS	0x000001f7
#define VCODEC_NS_WEBOS		0x0007c002

/* MM fabric master ports of the codec (vendor msm_bus_board_8660.c: HD_CODEC_PORT0/1) */
#define MMFAB_HD_CODEC_PORTS	(BIT(12) | BIT(13))

static const char * const fabric_clks[] = { "mmfab", "mmfab_a", "afab", "afab_a", "ebi1", "ebi1_a" };

/* Probe side: map the MMSS clock controller, find the RPM, keep the SMI and the fabric clocks on. */
int vidc_legacy_probe(struct vidc_core *core)
{
	struct device *dev = core->dev;
	struct device_node *np;
	struct resource res;
	struct clk *smi;
	int i;

	np = of_parse_phandle(dev->of_node, "qcom,mmcc", 0);
	if (!np)
		return 0;	/* not the legacy wiring */

	if (of_address_to_resource(np, 0, &res)) {
		of_node_put(np);
		return -EINVAL;
	}
	of_node_put(np);
	/* the region belongs to the mmcc driver: map it without requesting it */
	core->mmss = devm_ioremap(dev, res.start, resource_size(&res));
	if (!core->mmss)
		return -ENOMEM;

	np = of_parse_phandle(dev->of_node, "qcom,rpm", 0);
	if (np) {
		struct platform_device *pdev = of_find_device_by_node(np);

		of_node_put(np);
		core->rpm = pdev ? platform_get_drvdata(pdev) : NULL;
		if (pdev)
			put_device(&pdev->dev);
		if (!core->rpm)
			return -EPROBE_DEFER;
	}

	/* the SMI holds the firmware and every codec buffer: its RPM clock needs a rate or the RPM leaves it off */
	smi = devm_clk_get_enabled(dev, "smi");
	if (IS_ERR(smi))
		return dev_err_probe(dev, PTR_ERR(smi), "no smi clock\n");

	/* the LuneOS interconnect provider keeps every RPM fabric clock at 384 MHz or more */
	for (i = 0; i < ARRAY_SIZE(fabric_clks); i++) {
		struct clk *c = devm_clk_get_optional(dev, fabric_clks[i]);

		if (IS_ERR_OR_NULL(c))
			continue;
		if (!clk_set_rate(c, 384000000))
			clk_prepare_enable(c);
	}

	core->legacy = true;
	dev_info(dev, "legacy power-up: MMSS registers mapped, RPM %s\n", core->rpm ? "found" : "not given");
	return 0;
}

/* Resume side: everything the mmcc driver, the footswitch and the bus driver of the vendor kernel do before the core is touched. */
void vidc_legacy_power_on(struct vidc_core *core)
{
	void __iomem *mmss = core->mmss;
	struct device *dev = core->dev;
	u32 gfs = readl(mmss + MMSS_VED_GFS_CTL);
	u32 cc = readl(mmss + MMSS_VCODEC_CC);
	u32 v, axi, ahb, cc_core;

	/* MM AXI setup of the vendor msm8660_clock_init() (hardware gating, FORCE_CORE_ON, sleep/wake delays of the MM AXI ports) */
	v = readl(mmss + MMSS_MAXI_EN);
	writel((v & ~0x1803ffff) | 0x100207f9, mmss + MMSS_MAXI_EN);
	writel(0x3fe7fcff, mmss + MMSS_MAXI_EN3);
	writel(0x000001d8, mmss + MMSS_SAXI_EN);
	writel(0x000007f9, mmss + MMSS_AHB_EN2);
	writel(readl(mmss + MMSS_VCODEC_CC) | BIT(30), mmss + MMSS_VCODEC_CC);	/* core memory retention */

	/* vcodec_axi_a_clk (port A: RISC firmware fetch) and vcodec_axi_b_clk (port B: data) */
	writel(readl(mmss + MMSS_VCODEC_AXI_AB) | BIT(25) | BIT(23), mmss + MMSS_VCODEC_AXI_AB);
	mb();
	udelay(10);

	/* block reset of the codec as the vendor footswitch enable does (clk_reset of vcodec_axi, vcodec_p, vcodec) */
	axi = readl(mmss + MMSS_SW_RESET_AXI);
	ahb = readl(mmss + MMSS_SW_RESET_AHB);
	cc_core = readl(mmss + MMSS_SW_RESET_CORE);
	writel(cc_core | BIT(6), mmss + MMSS_SW_RESET_CORE);
	writel(ahb | BIT(1), mmss + MMSS_SW_RESET_AHB);
	writel(axi | BIT(4) | BIT(5), mmss + MMSS_SW_RESET_AXI);
	mb();
	udelay(5);
	writel(axi & ~(BIT(4) | BIT(5)), mmss + MMSS_SW_RESET_AXI);
	writel(ahb & ~BIT(1), mmss + MMSS_SW_RESET_AHB);
	writel(cc_core & ~BIT(6), mmss + MMSS_SW_RESET_CORE);
	mb();
	udelay(5);

	if (core->rpm) {
		/* vendor msm_bus_rpm_port_unhalt: word 0 = halt value (0 = run), word 1 = mask of the ports */
		u32 halt[2] = { 0, MMFAB_HD_CODEC_PORTS };
		/* fabric arbitration read in the RPM request registers (0x104600 + id * 4) under webOS while its codec ran */
		u32 mm_arb[23] = { 0x0c5c0f07, 0, 0x00008c5c, 0, 0, 0, 0, 0x00030000, 0x01700138, 0x00008c5c };
		u32 apps_arb[6] = { 0x00000c5f, 3, 0x00030000, 0x00008c5c, 0, 0 };
		int r0 = qcom_rpm_write(core->rpm, QCOM_RPM_ACTIVE_STATE, QCOM_RPM_MM_FABRIC_HALT, halt, 2);
		int r1 = qcom_rpm_write(core->rpm, QCOM_RPM_ACTIVE_STATE, QCOM_RPM_MM_FABRIC_ARB, mm_arb, 23);
		int r2 = qcom_rpm_write(core->rpm, QCOM_RPM_ACTIVE_STATE, QCOM_RPM_APPS_FABRIC_ARB, apps_arb, 6);

		dev_info(dev, "legacy power-up: MM fabric unhalt %d, arbitration MM %d APPS %d\n", r0, r1, r2);
	}

	/* the vcodec core clock (the mainline mmcc left VCODEC_CC at 0x80000000 and NS at 0 on the MSM8660) */
	if ((cc & VCODEC_CC_ON) != VCODEC_CC_ON) {
		writel(cc & ~VCODEC_CC_ON, mmss + MMSS_VCODEC_CC);
		writel(VCODEC_NS_WEBOS, mmss + MMSS_VCODEC_NS);
		writel(VCODEC_MD0_WEBOS, mmss + MMSS_VCODEC_MD0);
		writel(cc | 0xa4, mmss + MMSS_VCODEC_CC);	/* root, MND, mode */
		udelay(1);
		writel(cc | VCODEC_CC_ON, mmss + MMSS_VCODEC_CC);
		udelay(10);
	}

	/* video core footswitch */
	if (!((gfs & GFS_ENABLE) && !(gfs & GFS_CLAMP))) {
		writel(gfs | GFS_ENABLE, mmss + MMSS_VED_GFS_CTL);
		udelay(1);
		writel((gfs | GFS_ENABLE) & ~GFS_CLAMP, mmss + MMSS_VED_GFS_CTL);
		udelay(1);
	}

	dev_info(dev, "legacy power-up: VED_GFS 0x%08x VCODEC_CC 0x%08x NS 0x%08x AXI_AB 0x%08x halt 0x%08x\n",
		 readl(mmss + MMSS_VED_GFS_CTL), readl(mmss + MMSS_VCODEC_CC), readl(mmss + MMSS_VCODEC_NS),
		 readl(mmss + MMSS_VCODEC_AXI_AB), readl(mmss + 0x1e8));
}
