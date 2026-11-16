// SPDX-License-Identifier: GPL-2.0-only
/*
 * HP TouchPad (tenderloin) charger: Maxim MAX8903B driven by GPIOs, with the USB charger type detection of the
 * HP/CyanogenMod 3.4 kernel (drivers/usb/gadget/msm72k_udc.c, usb_multi_chg_detect(), CONFIG_USB_MULTIPLE_CHARGER_DETECT).
 *
 * The cable VBUS is on the DC input of the MAX8903 (DOK_N low as soon as a cable is plugged, PC port or adapter); the
 * USB PHY does not see VBUS. So this driver:
 *  - watches DOK_N; on a plug it probes the D+/D- lines through the ChipIdea ULPI viewport (the controller is idle:
 *    VBUS is not reported to it yet), works out the kind of port and sets the input current limit;
 *  - is the VBUS extcon of the USB controller and its PHY: EXTCON_USB is set for any source but a known adapter
 *    (HP 10 W, phone adapter), so the gadget starts on a cable plugged after boot and stays off on a wall adapter;
 *  - maps the controller registers without requesting them (the ChipIdea driver owns the region);
 *  - owns CEN (charge enable, high = enabled on this board) and the input current pins (DCM, IUSB, USUS, ISET1/2),
 *    reports CHG_N/FLT_N.
 *
 * Input current (MAX8903 pins, STATUS.md "Charging on a wall adapter"):
 *   USB mode (DCM high): IUSB high 500 mA, low 100 mA.  DC mode (DCM low), ISET1/ISET2: 0/0 750, 0/1 900, 1/1 1400,
 *   1/0 2000 mA.
 * Detection result -> limit, as in the vendor code: SDP 500, CDP 1400, HP 10 W adapter (DCP) 2000, phone adapter
 * (D+/D- shorted through a divider) 900; an unknown kind gets 500 here (the vendor used 100 mA, less than the
 * tablet draws). input_current_limit is writable to lower it.
 */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/extcon-provider.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

/* ChipIdea registers (offsets from the controller base, as in msm72k_udc/msm72k_otg) */
#define CI_USBCMD		0x140
#define CI_USBCMD_RS		BIT(0)
#define CI_ULPI_VIEWPORT	0x170
#define ULPI_VP_WAKEUP		BIT(31)
#define ULPI_VP_RUN		BIT(30)
#define ULPI_VP_WRITE		BIT(29)
#define ULPI_VP_ADDR		GENMASK(23, 16)
#define ULPI_VP_RDATA		GENMASK(15, 8)
#define ULPI_VP_WDATA		GENMASK(7, 0)
#define CI_PORTSC		0x184
#define PORTSC_LS_DP		BIT(11)
#define PORTSC_LS_DM		BIT(10)
#define PORTSC_PHCD		BIT(23)
#define PORTSC_W1C		(BIT(1) | BIT(3) | BIT(5))	/* CSC, PEC, OCC: write 0 to leave them alone */

/* ULPI registers */
#define ULPI_FUNC_CTRL		0x04
#define ULPI_DEBUG		0x15
#define ULPI_OTG_CTRL		0x0a
#define ULPI_OTG_CTRL_SET	0x0b
#define ULPI_OTG_CTRL_CLR	0x0c
#define ULPI_OTG_DP_PULLDOWN	BIT(1)
#define ULPI_OTG_DM_PULLDOWN	BIT(2)
/* Qualcomm vendor register: charger detection (current source on D+, comparator), values from the vendor code */
#define ULPI_CHG_DET		0x34
#define CHG_DET_IDLE		0x0f
#define CHG_DET_DP_SRC		0x25	/* current source on D+ */
#define CHG_DET_CDP		0x24
#define CHG_DET_CDP_SEEN	BIT(4)
/* function control: SuspendM | TermSelect | XcvrSelect FS, op mode non-driving (0x4d) or normal (0x45) */
#define FUNC_NON_DRIVING	0x4d
#define FUNC_NORMAL		0x45

#define HOST_WAIT_MS		5000
/*
 * PC port (SDP) step-up, as the old tp did: 900 mA (DC mode) after SDP_UP_WAIT_MS of stable 500 mA. If the input
 * drops within SDP_UP_GUARD_MS of the step, the port could not give that much: SDP stays at 500 mA for
 * SDP_UP_FORGET_MS. The user's PC USB 3 port held 900 mA (STATUS.md 2026-10-16).
 */
#define SDP_UP_MA		900
#define SDP_UP_WAIT_MS		20000
#define SDP_UP_GUARD_MS		30000
#define SDP_UP_FORGET_MS	120000	/* a PC enumerates us well within this */
#define CI_DEVICEADDR		0x154
#define DEVICEADDR_USBADR	GENMASK(31, 25)
#define DET_DELAY_MS		200	/* after DOK_N: contacts settle (BC1.2 data contact detect is 300-900 ms) */

enum tl_port {
	TL_NONE,
	TL_SDP,
	TL_CDP,
	TL_DCP,		/* HP 10 W adapter */
	TL_PHONE,	/* phone adapter, D+/D- on a divider */
	TL_UNKNOWN,
	TL_NOHOST,	/* looked like a PC port but no host enumerated us: an adapter that leaves D+/D- open */
};

static const char *const tl_port_name[] = {
	[TL_NONE] = "none", [TL_SDP] = "SDP (PC port)", [TL_CDP] = "CDP (charging PC port)",
	[TL_DCP] = "DCP (HP 10 W adapter)", [TL_PHONE] = "phone adapter", [TL_UNKNOWN] = "unknown adapter",
	[TL_NOHOST] = "adapter (no USB host)",
};

static const unsigned int tl_port_ma[] = {
	[TL_NONE] = 500, [TL_SDP] = 500, [TL_CDP] = 1400, [TL_DCP] = 2000, [TL_PHONE] = 900, [TL_UNKNOWN] = 500,
	[TL_NOHOST] = 1400,	/* the user's wall adapter held 1400 mA (STATUS.md 2026-10-16) */
};

static const unsigned int tl_extcon_cables[] = {
	EXTCON_USB,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_CDP,
	EXTCON_NONE,
};

struct tl_charger {
	struct device *dev;
	void __iomem *usb;
	struct gpio_desc *dok, *chg, *flt, *cen, *dcm, *iusb, *usus, *iset1, *iset2;
	struct power_supply *psy;
	struct power_supply_desc desc;
	struct extcon_dev *edev;
	struct delayed_work det_work;
	struct delayed_work host_work;
	struct delayed_work up_work;
	struct mutex lock;
	enum tl_port port;
	unsigned int limit_ma;		/* applied */
	unsigned int user_ma;		/* cap written through sysfs, 0 = none */
	bool inhibit;
	bool was_online;	/* DOK_N state seen by the interrupt */
	bool sdp_up;		/* SDP raised to SDP_UP_MA */
	unsigned long sdp_up_at;	/* jiffies of the step up */
	unsigned long sdp_bad_at;	/* jiffies of the last input lost after a step up, 0 = none */
};

/* ---- ULPI viewport (the controller is idle during the detection, nothing else uses the viewport) ---- */

static int tl_ulpi_wait(struct tl_charger *c, u32 bit, u32 *v)
{
	return readl_poll_timeout(c->usb + CI_ULPI_VIEWPORT, *v, !(*v & bit), 5, 2000);
}

static int tl_ulpi_wake(struct tl_charger *c)
{
	u32 v = readl(c->usb + CI_ULPI_VIEWPORT);

	if (v & ULPI_VP_WAKEUP)
		return tl_ulpi_wait(c, ULPI_VP_WAKEUP, &v);
	writel(ULPI_VP_WAKEUP, c->usb + CI_ULPI_VIEWPORT);
	return tl_ulpi_wait(c, ULPI_VP_WAKEUP, &v);
}

static int tl_ulpi_write(struct tl_charger *c, u8 reg, u8 val)
{
	u32 v;
	int ret = tl_ulpi_wake(c);

	if (ret)
		return ret;
	writel(ULPI_VP_RUN | ULPI_VP_WRITE | FIELD_PREP(ULPI_VP_ADDR, reg) | FIELD_PREP(ULPI_VP_WDATA, val),
	       c->usb + CI_ULPI_VIEWPORT);
	return tl_ulpi_wait(c, ULPI_VP_RUN, &v);
}

static int tl_ulpi_read(struct tl_charger *c, u8 reg)
{
	u32 v;
	int ret = tl_ulpi_wake(c);

	if (ret)
		return ret;
	writel(ULPI_VP_RUN | FIELD_PREP(ULPI_VP_ADDR, reg), c->usb + CI_ULPI_VIEWPORT);
	ret = tl_ulpi_wait(c, ULPI_VP_RUN, &v);
	return ret ? ret : FIELD_GET(ULPI_VP_RDATA, v);
}

/*
 * Line state straight from the PHY (ULPI Debug register, bit 0 = D+, bit 1 = D-): PORTSC.LS only follows the PHY
 * while the link runs, and stayed at SE0 on a wall adapter (2026-11-04). PORTSC is logged for comparison.
 */
static void tl_line_state(struct tl_charger *c, bool *dp, bool *dm)
{
	u32 ps = readl(c->usb + CI_PORTSC);
	int dbg = tl_ulpi_read(c, ULPI_DEBUG);

	if (dbg < 0) {
		*dp = ps & PORTSC_LS_DP;
		*dm = ps & PORTSC_LS_DM;
	} else {
		*dp = dbg & BIT(0);
		*dm = dbg & BIT(1);
	}
	dev_info(c->dev, "line state: ULPI debug %02x PORTSC %08x -> D+ %d D- %d\n", dbg & 0xff, ps, *dp, *dm);
}

/*
 * After a disconnect the ChipIdea driver puts the PHY in low power (PORTSC.PHCD): the line state is then frozen at
 * SE0, which looks like a PC port. Bring the PHY clock back before probing the lines.
 */
static void tl_phy_wake(struct tl_charger *c)
{
	u32 v = readl(c->usb + CI_PORTSC);

	if (!(v & PORTSC_PHCD))
		return;
	writel(v & ~(PORTSC_PHCD | PORTSC_W1C), c->usb + CI_PORTSC);
	usleep_range(2000, 3000);
	dev_info(c->dev, "PHY woken from low power (PORTSC %08x -> %08x)\n", v, readl(c->usb + CI_PORTSC));
}

/* usb_multi_chg_detect() of the vendor kernel */
static enum tl_port tl_detect(struct tl_charger *c)
{
	enum tl_port port;
	int fc, otg, ret;
	bool dp, dm;

	if (readl(c->usb + CI_USBCMD) & CI_USBCMD_RS) {
		/* the controller is running: a host saw us, it is a PC port */
		dev_info(c->dev, "controller already running, assuming a PC port\n");
		return TL_SDP;
	}

	tl_phy_wake(c);
	fc = tl_ulpi_read(c, ULPI_FUNC_CTRL);
	otg = tl_ulpi_read(c, ULPI_OTG_CTRL);
	dev_info(c->dev, "detect: USBCMD %08x FUNC_CTRL %02x OTG_CTRL %02x CHG_DET %02x\n",
		 readl(c->usb + CI_USBCMD), fc, otg, tl_ulpi_read(c, ULPI_CHG_DET));
	if (fc < 0 || otg < 0) {
		dev_warn(c->dev, "ULPI not reachable (%d/%d), assuming a PC port\n", fc, otg);
		return TL_SDP;
	}

	ret = tl_ulpi_write(c, ULPI_CHG_DET, CHG_DET_IDLE);
	msleep(10);
	ret = ret ?: tl_ulpi_write(c, ULPI_FUNC_CTRL, FUNC_NON_DRIVING);
	ret = ret ?: tl_ulpi_write(c, ULPI_OTG_CTRL_CLR, ULPI_OTG_DP_PULLDOWN | ULPI_OTG_DM_PULLDOWN);
	if (ret)
		goto fail;
	msleep(20);
	tl_line_state(c, &dp, &dm);

	if (dp && dm) {
		/* D+ and D- high: an adapter. Pull D+ down: D- follows if they are shorted (HP 10 W) */
		ret = tl_ulpi_write(c, ULPI_FUNC_CTRL, FUNC_NORMAL);
		ret = ret ?: tl_ulpi_write(c, ULPI_OTG_CTRL_SET, ULPI_OTG_DP_PULLDOWN);
		if (ret)
			goto fail;
		msleep(10);
		tl_line_state(c, &dp, &dm);
		port = dm ? TL_PHONE : TL_DCP;
	} else if (dp || dm) {
		port = TL_UNKNOWN;
	} else {
		/* both low: a host (pull-downs) or a shorted adapter. Current source on D+ */
		ret = tl_ulpi_write(c, ULPI_CHG_DET, CHG_DET_DP_SRC);
		if (ret)
			goto fail;
		msleep(10);
		tl_line_state(c, &dp, &dm);
		if (dp) {
			port = dm ? TL_PHONE : TL_UNKNOWN;	/* vendor: "OMTP phone adaptor" 900 mA */
		} else {
			ret = tl_ulpi_write(c, ULPI_CHG_DET, CHG_DET_CDP);
			if (ret)
				goto fail;
			msleep(10);
			ret = tl_ulpi_read(c, ULPI_CHG_DET);
			if (ret < 0)
				goto fail;
			dev_info(c->dev, "CHG_DET after CDP probe: %02x\n", ret);
			port = (ret & CHG_DET_CDP_SEEN) ? TL_CDP : TL_SDP;
		}
	}
	ret = 0;
	goto restore;

fail:
	dev_warn(c->dev, "ULPI access failed (%d), assuming a PC port\n", ret);
	port = TL_SDP;
restore:
	tl_ulpi_write(c, ULPI_CHG_DET, CHG_DET_IDLE);
	tl_ulpi_write(c, ULPI_FUNC_CTRL, fc);
	tl_ulpi_write(c, ULPI_OTG_CTRL, otg);
	return port;
}

/* ---- MAX8903 pins ---- */

static unsigned int tl_set_limit(struct tl_charger *c, unsigned int ma)
{
	int dcm, iusb = 1, iset1 = 0, iset2 = 0;

	if (ma >= 2000) {
		dcm = 0; iset1 = 1; ma = 2000;
	} else if (ma >= 1400) {
		dcm = 0; iset1 = 1; iset2 = 1; ma = 1400;
	} else if (ma >= 900) {
		dcm = 0; iset2 = 1; ma = 900;
	} else if (ma >= 750) {
		dcm = 0; ma = 750;
	} else if (ma >= 500) {
		dcm = 1; ma = 500;
	} else {
		dcm = 1; iusb = 0; ma = 100;
	}

	/* lower the DC limit before leaving the USB mode, so that no state allows more than both */
	gpiod_set_value_cansleep(c->iset1, iset1 && !dcm);
	gpiod_set_value_cansleep(c->iset2, iset2 && !dcm);
	gpiod_set_value_cansleep(c->iusb, iusb);
	gpiod_set_value_cansleep(c->usus, 0);
	gpiod_set_value_cansleep(c->dcm, dcm);
	c->limit_ma = ma;
	return ma;
}

static bool tl_online(struct tl_charger *c)
{
	return gpiod_get_value_cansleep(c->dok) > 0;
}

static void tl_apply(struct tl_charger *c)
{
	unsigned int ma = c->port == TL_SDP && c->sdp_up ? SDP_UP_MA : tl_port_ma[c->port];

	if (c->user_ma && c->user_ma < ma)
		ma = c->user_ma;
	tl_set_limit(c, ma);
	gpiod_set_value_cansleep(c->cen, c->port != TL_NONE && !c->inhibit);
}

static void tl_report(struct tl_charger *c, enum tl_port port)
{
	/* only a port known to be an adapter keeps the gadget off; anything else may be a host, let it enumerate us */
	bool data = port != TL_NONE && port != TL_DCP && port != TL_PHONE;

	extcon_set_state(c->edev, EXTCON_CHG_USB_SDP, port == TL_SDP);
	extcon_set_state(c->edev, EXTCON_CHG_USB_CDP, port == TL_CDP);
	extcon_set_state(c->edev, EXTCON_CHG_USB_DCP, port == TL_DCP || port == TL_PHONE || port == TL_UNKNOWN ||
						      port == TL_NOHOST);
	extcon_set_state(c->edev, EXTCON_USB, data);
	extcon_sync(c->edev, EXTCON_CHG_USB_SDP);
	extcon_sync(c->edev, EXTCON_CHG_USB_CDP);
	extcon_sync(c->edev, EXTCON_CHG_USB_DCP);
	extcon_sync(c->edev, EXTCON_USB);
}

static void tl_det_work(struct work_struct *work)
{
	struct tl_charger *c = container_of(to_delayed_work(work), struct tl_charger, det_work);
	enum tl_port port = TL_NONE;

	mutex_lock(&c->lock);
	if (tl_online(c)) {
		/* a new cable starts from the USB 500 mA mode while it is probed */
		if (c->port == TL_NONE)
			tl_set_limit(c, 500);
		port = c->port != TL_NONE ? c->port : tl_detect(c);
	}
	if (port != c->port || port == TL_NONE) {
		if (port != c->port)
			dev_info(c->dev, "source: %s, input limit %u mA\n", tl_port_name[port], tl_port_ma[port]);
		c->port = port;
		tl_apply(c);
	}
	mutex_unlock(&c->lock);

	tl_report(c, port);
	power_supply_changed(c->psy);
	if (port == TL_SDP || port == TL_UNKNOWN)
		mod_delayed_work(system_wq, &c->host_work, msecs_to_jiffies(HOST_WAIT_MS));
}

/* SDP/unknown with no address from a host after HOST_WAIT_MS: not a PC, the old tp heuristic ("not attached" = wall) */
static void tl_host_work(struct work_struct *work)
{
	struct tl_charger *c = container_of(to_delayed_work(work), struct tl_charger, host_work);
	u32 addr = FIELD_GET(DEVICEADDR_USBADR, readl(c->usb + CI_DEVICEADDR));
	bool changed = false;

	mutex_lock(&c->lock);
	if ((c->port == TL_SDP || c->port == TL_UNKNOWN) && tl_online(c) && !addr) {
		dev_info(c->dev, "no USB host after %d ms: %s, input limit %u mA\n", HOST_WAIT_MS,
			 tl_port_name[TL_NOHOST], tl_port_ma[TL_NOHOST]);
		c->port = TL_NOHOST;
		tl_apply(c);
		changed = true;
	}
	mutex_unlock(&c->lock);
	if (changed) {
		tl_report(c, TL_NOHOST);
		power_supply_changed(c->psy);
	} else if (c->port == TL_SDP) {
		mod_delayed_work(system_wq, &c->up_work, msecs_to_jiffies(SDP_UP_WAIT_MS));
	}
}

static void tl_up_work(struct work_struct *work)
{
	struct tl_charger *c = container_of(to_delayed_work(work), struct tl_charger, up_work);
	bool changed = false;

	mutex_lock(&c->lock);
	if (c->sdp_bad_at && time_after(jiffies, c->sdp_bad_at + msecs_to_jiffies(SDP_UP_FORGET_MS)))
		c->sdp_bad_at = 0;
	if (c->port == TL_SDP && !c->sdp_up && !c->sdp_bad_at && tl_online(c) &&
	    gpiod_get_value_cansleep(c->flt) <= 0) {
		c->sdp_up = true;
		c->sdp_up_at = jiffies;
		tl_apply(c);
		changed = true;
		dev_info(c->dev, "PC port stable: input limit %u mA\n", c->limit_ma);
	}
	mutex_unlock(&c->lock);
	if (changed)
		power_supply_changed(c->psy);
}

static irqreturn_t tl_dok_irq(int irq, void *data)
{
	struct tl_charger *c = data;

	bool online = tl_online(c);

	/* a stale edge (one is latched when the interrupt is requested at probe): the probe work handles the boot state */
	if (online == c->was_online)
		return IRQ_HANDLED;
	c->was_online = online;

	mutex_lock(&c->lock);
	if (!online && c->port != TL_NONE) {
		/* unplugged: forget the source at once, back to the safe USB mode */
		dev_info(c->dev, "source removed\n");
		if (c->sdp_up && time_before(jiffies, c->sdp_up_at + msecs_to_jiffies(SDP_UP_GUARD_MS))) {
			c->sdp_bad_at = jiffies;
			dev_info(c->dev, "input lost after the PC port step up: PC ports stay at 500 mA for a while\n");
		}
		c->sdp_up = false;
		c->port = TL_NONE;
		tl_apply(c);
	}
	mutex_unlock(&c->lock);
	mod_delayed_work(system_wq, &c->det_work, msecs_to_jiffies(DET_DELAY_MS));
	return IRQ_HANDLED;
}

static irqreturn_t tl_status_irq(int irq, void *data)
{
	struct tl_charger *c = data;

	power_supply_changed(c->psy);
	return IRQ_HANDLED;
}

/* ---- power supply ---- */

static const enum power_supply_property tl_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
};

static int tl_get_property(struct power_supply *psy, enum power_supply_property psp,
			   union power_supply_propval *val)
{
	struct tl_charger *c = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = tl_online(c);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (!tl_online(c))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (gpiod_get_value_cansleep(c->chg) > 0)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = gpiod_get_value_cansleep(c->flt) > 0 ? POWER_SUPPLY_HEALTH_UNSPEC_FAILURE
								   : POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		switch (c->port) {
		case TL_SDP:
			val->intval = POWER_SUPPLY_USB_TYPE_SDP;
			break;
		case TL_CDP:
			val->intval = POWER_SUPPLY_USB_TYPE_CDP;
			break;
		case TL_DCP:
		case TL_PHONE:
		case TL_NOHOST:
			val->intval = POWER_SUPPLY_USB_TYPE_DCP;
			break;
		default:
			val->intval = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			break;
		}
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = c->limit_ma * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		val->intval = c->inhibit ? POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE
					 : POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int tl_set_property(struct power_supply *psy, enum power_supply_property psp,
			   const union power_supply_propval *val)
{
	struct tl_charger *c = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (val->intval < 0)
			return -EINVAL;
		mutex_lock(&c->lock);
		c->user_ma = val->intval / 1000;
		tl_apply(c);
		mutex_unlock(&c->lock);
		break;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		if (val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO &&
		    val->intval != POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)
			return -EINVAL;
		mutex_lock(&c->lock);
		c->inhibit = val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
		tl_apply(c);
		mutex_unlock(&c->lock);
		break;
	default:
		return -EINVAL;
	}
	power_supply_changed(psy);
	return 0;
}

static int tl_writeable(struct power_supply *psy, enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT || psp == POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR;
}

static int tl_irq(struct tl_charger *c, struct gpio_desc *g, irq_handler_t fn, const char *name)
{
	int irq = gpiod_to_irq(g);

	if (irq < 0)
		return irq;
	return devm_request_threaded_irq(c->dev, irq, NULL, fn,
					 IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT, name, c);
}

static int tl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct power_supply_config cfg = {};
	struct device_node *usb_np;
	struct resource res;
	struct tl_charger *c;
	int ret;

	c = devm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->dev = dev;
	mutex_init(&c->lock);
	INIT_DELAYED_WORK(&c->det_work, tl_det_work);
	INIT_DELAYED_WORK(&c->host_work, tl_host_work);
	INIT_DELAYED_WORK(&c->up_work, tl_up_work);

	/* the controller registers are owned by the ChipIdea driver: map them without requesting the region */
	usb_np = of_parse_phandle(dev->of_node, "hp,usb-controller", 0);
	if (!usb_np)
		return dev_err_probe(dev, -EINVAL, "no hp,usb-controller\n");
	ret = of_address_to_resource(usb_np, 0, &res);
	of_node_put(usb_np);
	if (ret)
		return dev_err_probe(dev, ret, "no USB controller registers\n");
	c->usb = devm_ioremap(dev, res.start, resource_size(&res));
	if (!c->usb)
		return dev_err_probe(dev, -ENOMEM, "cannot map the USB controller\n");

	c->dok = devm_gpiod_get(dev, "dok", GPIOD_IN);
	c->chg = devm_gpiod_get(dev, "chg", GPIOD_IN);
	c->flt = devm_gpiod_get(dev, "flt", GPIOD_IN);
	/* safe state until the source is known: USB mode 500 mA, charging enabled (as the vendor driver at probe) */
	c->cen = devm_gpiod_get(dev, "cen", GPIOD_OUT_HIGH);
	c->dcm = devm_gpiod_get(dev, "dcm", GPIOD_OUT_HIGH);
	c->iusb = devm_gpiod_get(dev, "iusb", GPIOD_OUT_HIGH);
	c->usus = devm_gpiod_get(dev, "usus", GPIOD_OUT_LOW);
	c->iset1 = devm_gpiod_get(dev, "iset1", GPIOD_OUT_LOW);
	c->iset2 = devm_gpiod_get(dev, "iset2", GPIOD_OUT_LOW);
	if (IS_ERR(c->dok) || IS_ERR(c->chg) || IS_ERR(c->flt) || IS_ERR(c->cen) || IS_ERR(c->dcm) ||
	    IS_ERR(c->iusb) || IS_ERR(c->usus) || IS_ERR(c->iset1) || IS_ERR(c->iset2))
		return dev_err_probe(dev, -EINVAL, "missing charger GPIO\n");
	c->limit_ma = 500;
	c->port = TL_NONE;
	c->was_online = tl_online(c);

	c->edev = devm_extcon_dev_allocate(dev, tl_extcon_cables);
	if (IS_ERR(c->edev))
		return PTR_ERR(c->edev);
	ret = devm_extcon_dev_register(dev, c->edev);
	if (ret)
		return ret;

	c->desc.name = "max8903_charger";	/* the name tp and the battery driver already know */
	c->desc.type = POWER_SUPPLY_TYPE_USB;
	c->desc.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) | BIT(POWER_SUPPLY_USB_TYPE_DCP) |
			    BIT(POWER_SUPPLY_USB_TYPE_CDP) | BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN);
	c->desc.charge_behaviours = BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) |
				    BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE);
	c->desc.properties = tl_props;
	c->desc.num_properties = ARRAY_SIZE(tl_props);
	c->desc.get_property = tl_get_property;
	c->desc.set_property = tl_set_property;
	c->desc.property_is_writeable = tl_writeable;
	cfg.of_node = dev->of_node;
	cfg.drv_data = c;
	c->psy = devm_power_supply_register(dev, &c->desc, &cfg);
	if (IS_ERR(c->psy))
		return dev_err_probe(dev, PTR_ERR(c->psy), "cannot register the power supply\n");

	ret = tl_irq(c, c->dok, tl_dok_irq, "charger-dok");
	ret = ret ?: tl_irq(c, c->chg, tl_status_irq, "charger-chg");
	ret = ret ?: tl_irq(c, c->flt, tl_status_irq, "charger-flt");
	if (ret)
		return dev_err_probe(dev, ret, "cannot request the charger interrupts\n");

	platform_set_drvdata(pdev, c);
	/* a cable already present at boot: probe it once the USB controller is up (its clocks are on by then) */
	schedule_delayed_work(&c->det_work, msecs_to_jiffies(tl_online(c) ? 3000 : 0));
	return 0;
}

static void tl_remove(struct platform_device *pdev)
{
	struct tl_charger *c = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&c->det_work);
	cancel_delayed_work_sync(&c->host_work);
	cancel_delayed_work_sync(&c->up_work);
}

static const struct of_device_id tl_of_match[] = {
	{ .compatible = "hp,tenderloin-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, tl_of_match);

static struct platform_driver tl_driver = {
	.probe = tl_probe,
	.remove = tl_remove,
	.driver = {
		.name = "tenderloin-charger",
		.of_match_table = tl_of_match,
	},
};
module_platform_driver(tl_driver);

MODULE_DESCRIPTION("HP TouchPad MAX8903B charger with USB charger type detection");
MODULE_LICENSE("GPL");
