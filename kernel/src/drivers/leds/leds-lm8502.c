// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED outputs of the LM8502 (National/TI programmable lighting driver: 10 LED outputs, a haptic H-bridge, a flash/torch driver).
 * On the HP TouchPad it lights the two white "core navi" LEDs on both sides of the Home button; the vibrator and the torch of the same chip
 * are not handled here.
 *
 * Register map and power-up sequence from the vendor driver of the CyanogenMod 3.4 kernel (drivers/leds/leds-lm8502.c, board-tenderloin.c).
 * The brightness of an output is written to its current register (0x26 + n); the control register (0x06 + n) holds the full-scale current.
 * Registers are only written: on this board every read returns 0xff (the vendor driver reads before it writes, which worked out to writing
 * 0xff based values), so nothing here depends on a read.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>

#define LM8502_ENGINE_CNTRL1		0x00
#define LM8502_CHIP_EN			0x40
#define LM8502_CONTROL(n)		(0x06 + (n))
#define LM8502_CURRENT(n)		(0x26 + (n))
#define LM8502_MISC			0x36
#define LM8502_MISC_POWER_SAVE		0x20
#define LM8502_MISC_BOOST_EN		0x08
#define LM8502_MISC_PWM_IN		0x02
#define LM8502_RESET			0x3d
#define LM8502_RESET_VALUE		0xff
#define LM8502_NUM_LEDS			10

/* full-scale current setting, bits 4:3 of the control register (vendor board file: 2) */
#define LM8502_FULL_SCALE_DEFAULT	2

struct lm8502;

struct lm8502_led {
	struct led_classdev cdev;
	struct lm8502 *chip;
	u8 index;
};

struct lm8502 {
	struct i2c_client *client;
	struct gpio_desc *enable;
	struct lm8502_led leds[LM8502_NUM_LEDS];
};

static int lm8502_brightness_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct lm8502_led *led = container_of(cdev, struct lm8502_led, cdev);

	return i2c_smbus_write_byte_data(led->chip->client, LM8502_CURRENT(led->index), brightness);
}

/*
 * The first transfer after the software reset is not acknowledged (measured: a plain register write is always acknowledged, the write that
 * follows a reset is refused once). A few attempts are enough.
 */
static int lm8502_write_retry(struct i2c_client *client, u8 reg, u8 val)
{
	int ret = 0;
	int i;

	for (i = 0; i < 5; i++) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (!ret)
			return 0;
		usleep_range(2000, 3000);
	}
	return ret;
}

static int lm8502_chip_init(struct lm8502 *chip)
{
	struct i2c_client *client = chip->client;
	int ret;

	gpiod_set_value_cansleep(chip->enable, 1);
	usleep_range(1000, 2000);

	/*
	 * Software reset, as the vendor driver. The chip resets itself while it receives the data byte and does not acknowledge it (the
	 * transfer reports -ENXIO about every other time, measured), so the result is ignored; every other write is acknowledged and checked.
	 */
	i2c_smbus_write_byte_data(client, LM8502_RESET, LM8502_RESET_VALUE);
	msleep(50);

	ret = lm8502_write_retry(client, LM8502_ENGINE_CNTRL1, LM8502_CHIP_EN);
	if (ret)
		return ret;
	/* power save on, charge pump (boost) on, brightness from the PWM/current registers */
	return lm8502_write_retry(client, LM8502_MISC,
				  LM8502_MISC_POWER_SAVE | LM8502_MISC_BOOST_EN | LM8502_MISC_PWM_IN);
}

static int lm8502_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct lm8502 *chip;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->client = client;
	i2c_set_clientdata(client, chip);

	chip->enable = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(chip->enable))
		return dev_err_probe(dev, PTR_ERR(chip->enable), "failed to get the enable GPIO\n");

	ret = lm8502_chip_init(chip);
	if (ret) {
		gpiod_set_value_cansleep(chip->enable, 0);
		return dev_err_probe(dev, ret, "chip initialisation failed\n");
	}

	for_each_available_child_of_node_scoped(dev->of_node, child) {
		struct led_init_data init_data = { .fwnode = of_fwnode_handle(child) };
		struct lm8502_led *led;
		u32 reg;

		if (of_property_read_u32(child, "reg", &reg) || reg >= LM8502_NUM_LEDS) {
			dev_err(dev, "%pOF: invalid reg\n", child);
			continue;
		}
		led = &chip->leds[reg];
		led->chip = chip;
		led->index = reg;
		led->cdev.max_brightness = 255;
		led->cdev.brightness_set_blocking = lm8502_brightness_set;

		ret = i2c_smbus_write_byte_data(client, LM8502_CONTROL(reg), LM8502_FULL_SCALE_DEFAULT << 3);
		if (ret)
			break;
		ret = i2c_smbus_write_byte_data(client, LM8502_CURRENT(reg), 0);
		if (ret)
			break;
		ret = devm_led_classdev_register_ext(dev, &led->cdev, &init_data);
		if (ret) {
			dev_err(dev, "%pOF: failed to register the LED: %d\n", child, ret);
			break;
		}
	}
	if (ret) {
		gpiod_set_value_cansleep(chip->enable, 0);
		return ret;
	}

	return 0;
}

static void lm8502_remove(struct i2c_client *client)
{
	struct lm8502 *chip = i2c_get_clientdata(client);

	gpiod_set_value_cansleep(chip->enable, 0);
}

static const struct of_device_id lm8502_of_match[] = {
	{ .compatible = "ti,lm8502" },
	{ }
};
MODULE_DEVICE_TABLE(of, lm8502_of_match);

static struct i2c_driver lm8502_driver = {
	.driver = {
		.name = "lm8502",
		.of_match_table = lm8502_of_match,
	},
	.probe = lm8502_probe,
	.remove = lm8502_remove,
};
module_i2c_driver(lm8502_driver);

MODULE_DESCRIPTION("LM8502 LED outputs (HP TouchPad Home button LEDs)");
MODULE_LICENSE("GPL");
