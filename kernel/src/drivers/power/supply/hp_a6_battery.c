// SPDX-License-Identifier: GPL-2.0
/*
 * Battery gauge of the HP TouchPad, read through the "A6" microcontroller.
 *
 * The A6 (a TI MSP430 running the "TS2" firmware of Palm/HP, also used as touch panel controller) sits on the
 * I2C bus of the GSBI8 at the 7-bit address 0x31 and exposes the registers of a Dallas DS2784 style gauge
 * (the register map and the conversions come from the vendor driver drivers/misc/a6/a6.c). Register access:
 * write the 16-bit register address (big endian), repeated start, read one byte; a 16-bit value is read as two
 * separate single-byte transfers, most significant byte first.
 *
 * Registers (from 0x0100): STATUS 0x00, RARC (relative capacity, %) 0x01, temperature 0x05/0x06,
 * voltage 0x07/0x08, current 0x09/0x0a, sense resistor conductance 0x12.
 *
 * The A6 sleeps between its periodic wake-ups and then does not answer on I2C (-ENXIO): always the case on
 * battery, rarely with the charger plugged. Its wake line (GPIO 155 for this A6, "wake-gpios") is held high for
 * the whole of each property read; the first transfer is retried while the A6 comes up.
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/power_supply.h>

#define A6_BAT_STATUS		0x0100
#define A6_BAT_RARC		0x0101
#define A6_BAT_TEMP_MSB		0x0105
#define A6_BAT_VOLT_MSB		0x0107
#define A6_BAT_CUR_MSB		0x0109
#define A6_BAT_RSNSP		0x0112

#define A6_RSENSE_DEFAULT_MOHM	20	/* used when the gauge does not report the sense resistor */
#define A6_WAKE_TRIES		6	/* first transfer after the wake line goes up: 1 + 2 + 4 + 8 + 16 ms of retries */

struct a6_battery {
	struct i2c_client *client;
	struct power_supply *psy;
	struct mutex lock;
	struct gpio_desc *wake;
	u32 rsense_mohm;
};

static int a6_read8(struct i2c_client *client, u16 reg, u8 *val)
{
	u8 addr[2] = { reg >> 8, reg & 0xff };
	struct i2c_msg msgs[2] = {
		{ .addr = client->addr, .flags = 0, .len = 2, .buf = addr },
		{ .addr = client->addr, .flags = I2C_M_RD, .len = 1, .buf = val },
	};
	int ret;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0)
		return ret;

	return ret == 2 ? 0 : -EIO;
}

/* 16-bit value: MSB register first, LSB register right after */
static int a6_read16(struct i2c_client *client, u16 msb_reg, u16 *val)
{
	u8 msb, lsb;
	int ret;

	ret = a6_read8(client, msb_reg, &msb);
	if (ret)
		return ret;

	ret = a6_read8(client, msb_reg + 1, &lsb);
	if (ret)
		return ret;

	*val = (msb << 8) | lsb;

	return 0;
}

/*
 * Raises the wake line and waits until the A6 answers (a read of RARC). Called with bat->lock held; a6_sleep()
 * lowers the line again. Without a wake line only the plain read is done.
 */
static int a6_wake(struct a6_battery *bat)
{
	unsigned int i;
	u8 byte;
	int ret;

	gpiod_set_value_cansleep(bat->wake, 1);
	for (i = 0; ; i++) {
		ret = a6_read8(bat->client, A6_BAT_RARC, &byte);
		if (!ret || !bat->wake || i == A6_WAKE_TRIES - 1)
			break;
		usleep_range(1000 << i, 2000 << i);
	}
	if (ret)
		gpiod_set_value_cansleep(bat->wake, 0);

	return ret;
}

static void a6_sleep(struct a6_battery *bat)
{
	gpiod_set_value_cansleep(bat->wake, 0);
}

static int a6_battery_get_property(struct power_supply *psy, enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct a6_battery *bat = power_supply_get_drvdata(psy);
	struct i2c_client *client = bat->client;
	u16 raw;
	u8 byte;
	int ret = 0;

	/* constant properties: no need to wake the A6 */
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		return 0;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		return 0;
	default:
		break;
	}

	mutex_lock(&bat->lock);

	ret = a6_wake(bat);
	if (ret) {
		mutex_unlock(&bat->lock);
		return ret;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = a6_read8(client, A6_BAT_RARC, &byte);
		val->intval = min_t(int, byte, 100);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* 11 bits, 4.88 mV per step, left aligned in the 16-bit value */
		ret = a6_read16(client, A6_BAT_VOLT_MSB, &raw);
		val->intval = (raw >> 5) * 4880;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		/* signed, 1.5625 uV per step across the sense resistor; positive = charging */
		ret = a6_read16(client, A6_BAT_CUR_MSB, &raw);
		val->intval = ((s16)raw * 3125 / 2) / (int)bat->rsense_mohm;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		/* signed 11 bits, 0.125 C per step: tenths of a degree */
		ret = a6_read16(client, A6_BAT_TEMP_MSB, &raw);
		val->intval = ((s16)raw >> 5) * 10 / 8;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = a6_read16(client, A6_BAT_CUR_MSB, &raw);
		if (ret)
			break;
		if (power_supply_am_i_supplied(psy)) {
			int ua = ((s16)raw * 3125 / 2) / (int)bat->rsense_mohm;
			u8 cap;

			ret = a6_read8(client, A6_BAT_RARC, &cap);
			if (ret)
				break;

			/*
			 * With the cable plugged the battery can still discharge: the input current is limited
			 * (500 mA from a PC port) and the tablet draws more than that.
			 */
			if (ua > 20000)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else if (cap >= 100)
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else if (ua < -20000)
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		} else {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = a6_read16(client, A6_BAT_TEMP_MSB, &raw);
		if (ret)
			break;
		raw = ((s16)raw >> 5) * 10 / 8;
		if ((s16)raw > 600)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if ((s16)raw < 0)
			val->intval = POWER_SUPPLY_HEALTH_COLD;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	default:
		ret = -EINVAL;
	}

	a6_sleep(bat);
	mutex_unlock(&bat->lock);

	return ret;
}

static const enum power_supply_property a6_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
};

static const struct power_supply_desc a6_battery_desc = {
	.name		= "battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= a6_battery_props,
	.num_properties	= ARRAY_SIZE(a6_battery_props),
	.get_property	= a6_battery_get_property,
};

static int a6_battery_probe(struct i2c_client *client)
{
	struct power_supply_config cfg = { };
	struct a6_battery *bat;
	u8 byte;
	int ret;

	bat = devm_kzalloc(&client->dev, sizeof(*bat), GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	bat->client = client;
	mutex_init(&bat->lock);

	bat->wake = devm_gpiod_get_optional(&client->dev, "wake", GPIOD_OUT_LOW);
	if (IS_ERR(bat->wake))
		return dev_err_probe(&client->dev, PTR_ERR(bat->wake), "cannot get the wake GPIO\n");

	/* the gauge must answer before the supply is registered (on battery only once woken up) */
	ret = a6_wake(bat);
	if (ret)
		return dev_err_probe(&client->dev, ret, "A6 gauge does not answer\n");

	/* RSNSP is the conductance of the sense resistor in 1/Ohm: the vendor driver uses 1000 / RSNSP mOhm */
	bat->rsense_mohm = A6_RSENSE_DEFAULT_MOHM;
	if (!a6_read8(client, A6_BAT_RSNSP, &byte) && byte)
		bat->rsense_mohm = 1000 / byte;
	a6_sleep(bat);

	cfg.drv_data = bat;
	cfg.fwnode = dev_fwnode(&client->dev);

	bat->psy = devm_power_supply_register(&client->dev, &a6_battery_desc, &cfg);
	if (IS_ERR(bat->psy))
		return dev_err_probe(&client->dev, PTR_ERR(bat->psy), "cannot register the battery\n");

	dev_info(&client->dev, "HP TouchPad battery gauge (A6), sense resistor %u mOhm\n", bat->rsense_mohm);

	return 0;
}

static const struct of_device_id a6_battery_of_match[] = {
	{ .compatible = "hp,tenderloin-a6-battery" },
	{ }
};
MODULE_DEVICE_TABLE(of, a6_battery_of_match);

static struct i2c_driver a6_battery_driver = {
	.driver = {
		.name = "hp-a6-battery",
		.of_match_table = a6_battery_of_match,
	},
	.probe = a6_battery_probe,
};
module_i2c_driver(a6_battery_driver);

MODULE_DESCRIPTION("HP TouchPad battery gauge behind the A6 microcontroller");
MODULE_LICENSE("GPL");
