// SPDX-License-Identifier: GPL-2.0-only
/*
 * Aptina MT9M113 camera sensor (1.3 MP SoC with its own ISP), front camera of the HP TouchPad: V4L2 sub-device.
 *
 * The sequences come from the vendor driver of the CyanogenMod 3.4 kernel (drivers/media/video/msm/mt9m113.c, mt9m113_reg.c) and from the
 * board file (board-tenderloin.c): power-up with the power-down pin low then MCLK at 24 MHz, an initialisation table that sets the PLL, the
 * sensor core and the ISP (mt9m113_regs.h, generated from the vendor table), then the preview mode. The sensor outputs 640x480 YUV 4:2:2 on
 * one MIPI CSI-2 lane.
 *
 * Registers have 16-bit addresses and 16-bit values (big endian). The ISP and the MCU are reached through "variables": the logical
 * address is written to MCU_ADDRESS (0x098c), the value read or written through MCU_DATA (0x0990).
 *
 * There is no receiver driver yet (CSI-2 PHY and VFE 3.1 of the MSM8660), so nothing consumes the stream. The debugfs directory
 * mt9m113/ (stream, regs) starts the sensor by hand and shows that its MCU and auto exposure run.
 */
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/seq_file.h>

#include <media/v4l2-async.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "mt9m113_regs.h"

#define MT9M113_CHIP_ID			CCI_REG16(0x0000)
#define MT9M113_CHIP_ID_VALUE		0x2480
#define MT9M113_PLL_CONTROL		CCI_REG16(0x0014)
#define MT9M113_STANDBY_CONTROL		CCI_REG16(0x0018)
#define MT9M113_MCU_ADDRESS		CCI_REG16(0x098c)
#define MT9M113_MCU_DATA		CCI_REG16(0x0990)
#define MT9M113_COARSE_INT_TIME		CCI_REG16(0x3012)
#define MT9M113_FINE_INT_TIME		CCI_REG16(0x3014)
#define MT9M113_RESET_REGISTER		CCI_REG16(0x301a)
#define MT9M113_RESET_REGISTER_STREAM	0x120c
#define MT9M113_MIPI_CONTROL		CCI_REG16(0x3400)
#define MT9M113_MIPI_CONTROL_LP		0x7a08	/* clock lane goes to low power between frames (vendor value) */
#define MT9M113_MIPI_CONTROL_CONT_CLK	0x7a0c	/* continuous clock lane: easier for a first receiver bring-up */

/* MCU variables */
#define MT9M113_VAR_CORE_0248		0x0248	/* bit 15: freeze the double-buffered register update (vendor suspend/resume) */
#define MT9M113_VAR_AE_MAX_DGAIN_AE1	0x2212
#define MT9M113_VAR_SEQ_MODE		0xa102
#define MT9M113_VAR_SEQ_CMD		0xa103	/* 1 preview, 2 capture, 5 refresh; reads 0 when the command is done */
#define MT9M113_VAR_SEQ_STATE		0xa104	/* sequencer state (not used by the vendor driver, read for information) */
#define MT9M113_VAR_AE_SKIP_FRAMES	0xa208
#define MT9M113_VAR_AE_JUMP_DIVISOR	0xa209
#define MT9M113_VAR_AE_MAX_INDEX	0xa20c
#define MT9M113_VAR_AE_MAX_VIRTGAIN	0xa20e
#define MT9M113_VAR_SEQ_CAP_MODE	0xa115

#define MT9M113_WIDTH			640
#define MT9M113_HEIGHT			480
#define MT9M113_MBUS_CODE		MEDIA_BUS_FMT_UYVY8_2X8

static bool continuous_clock;
module_param(continuous_clock, bool, 0444);
MODULE_PARM_DESC(continuous_clock, "keep the MIPI clock lane running between frames (default: low power between frames, as the vendor driver)");

static const char * const mt9m113_supply_names[] = { "vdd", "vddio", "vaa" };

struct mt9m113 {
	struct i2c_client *client;
	struct regmap *regmap;
	struct clk *clk;
	struct gpio_desc *reset;
	struct gpio_desc *powerdown;
	struct regulator_bulk_data supplies[ARRAY_SIZE(mt9m113_supply_names)];
	struct v4l2_fwnode_endpoint bus_cfg;

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct mutex lock;	/* serialises power, streaming and register access */
	bool powered;
	bool streaming;

	struct dentry *debugfs;
};

static struct mt9m113 *sd_to_mt9m113(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9m113, sd);
}

/* The MCU is sometimes busy and does not acknowledge: the vendor driver retries 5 times 10 ms apart. */
static int mt9m113_write(struct mt9m113 *sensor, u32 reg, u16 val)
{
	int ret = 0;
	int i;

	for (i = 0; i < 5; i++) {
		ret = 0;
		cci_write(sensor->regmap, reg, val, &ret);
		if (!ret)
			return 0;
		msleep(10);
	}
	dev_err(&sensor->client->dev, "write 0x%04lx failed (%d)\n", CCI_REG_ADDR(reg), ret);
	return ret;
}

static int mt9m113_read(struct mt9m113 *sensor, u32 reg, u16 *val)
{
	u64 v = 0;
	int ret = 0;
	int i;

	for (i = 0; i < 5; i++) {
		ret = 0;
		cci_read(sensor->regmap, reg, &v, &ret);
		if (!ret) {
			*val = v;
			return 0;
		}
		msleep(10);
	}
	dev_err(&sensor->client->dev, "read 0x%04lx failed (%d)\n", CCI_REG_ADDR(reg), ret);
	return ret;
}

static int mt9m113_var_write(struct mt9m113 *sensor, u16 var, u16 val)
{
	int ret;

	ret = mt9m113_write(sensor, MT9M113_MCU_ADDRESS, var);
	if (ret)
		return ret;
	return mt9m113_write(sensor, MT9M113_MCU_DATA, val);
}

static int mt9m113_var_read(struct mt9m113 *sensor, u16 var, u16 *val)
{
	int ret;

	ret = mt9m113_write(sensor, MT9M113_MCU_ADDRESS, var);
	if (ret)
		return ret;
	return mt9m113_read(sensor, MT9M113_MCU_DATA, val);
}

/* Wait for the MCU to finish a command: the variable reads 0 when done (vendor: polled every 10 ms, 100 times at most). */
static int mt9m113_wait_seq_cmd(struct mt9m113 *sensor)
{
	u16 val = 1;
	int ret;
	int i;

	for (i = 0; i < 100; i++) {
		ret = mt9m113_var_read(sensor, MT9M113_VAR_SEQ_CMD, &val);
		if (ret)
			return ret;
		if (!val)
			return 0;
		msleep(10);
	}
	dev_warn(&sensor->client->dev, "sequencer command still pending after 1 s (0x%04x)\n", val);
	return -ETIMEDOUT;
}

/* ---------------------------------------------------------------------------------------------------------------------------------- */
/* Power */

static int mt9m113_power_on(struct mt9m113 *sensor)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
	if (ret)
		return ret;

	/* reset asserted while the supplies settle; power-down is active high and the vendor driver drives it low while the camera is used */
	gpiod_set_value_cansleep(sensor->reset, 1);
	gpiod_set_value_cansleep(sensor->powerdown, 0);
	msleep(20);

	ret = clk_prepare_enable(sensor->clk);
	if (ret) {
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
		return ret;
	}
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(sensor->reset, 0);
	/* vendor: 5 ms after the clock before the first I2C access; a bit more after a hard reset */
	msleep(10);

	sensor->powered = true;
	return 0;
}

static void mt9m113_power_off(struct mt9m113 *sensor)
{
	gpiod_set_value_cansleep(sensor->powerdown, 1);
	gpiod_set_value_cansleep(sensor->reset, 1);
	clk_disable_unprepare(sensor->clk);
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
	sensor->powered = false;
	sensor->streaming = false;
}

static int mt9m113_identify(struct mt9m113 *sensor)
{
	u16 id;
	int ret;

	ret = mt9m113_read(sensor, MT9M113_CHIP_ID, &id);
	if (ret)
		return ret;
	if (id != MT9M113_CHIP_ID_VALUE) {
		dev_err(&sensor->client->dev, "unexpected chip id 0x%04x (MT9M113 is 0x%04x)\n", id, MT9M113_CHIP_ID_VALUE);
		return -ENODEV;
	}
	return 0;
}

/* ---------------------------------------------------------------------------------------------------------------------------------- */
/* Streaming */

/* The vendor driver freezes the double-buffered register update while it writes the table (bit 15 of variable 0x0248). */
static int mt9m113_freeze_update(struct mt9m113 *sensor, bool freeze)
{
	u16 val;
	int ret;

	ret = mt9m113_var_read(sensor, MT9M113_VAR_CORE_0248, &val);
	if (ret)
		return ret;
	return mt9m113_var_write(sensor, MT9M113_VAR_CORE_0248, freeze ? (val | 0x8000) : (val & 0x7fff));
}

static int mt9m113_load_init_table(struct mt9m113 *sensor)
{
	unsigned int i;
	int ret;

	ret = mt9m113_freeze_update(sensor, true);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(mt9m113_init_regs); i++) {
		const struct mt9m113_reg *r = &mt9m113_init_regs[i];

		ret = mt9m113_write(sensor, CCI_REG16(r->addr), r->val);
		if (ret)
			return ret;
		if (r->delay_ms)
			msleep(r->delay_ms);
	}

	return mt9m113_freeze_update(sensor, false);
}

/* Preview auto-exposure limits (vendor mod_preview_mode_reg_tbl): 1 ms between the writes */
static const struct { u16 var; u16 val; } mt9m113_preview_ae[] = {
	{ MT9M113_VAR_AE_MAX_INDEX, 0x0008 },
	{ MT9M113_VAR_AE_MAX_VIRTGAIN, 0x00a0 },
	{ MT9M113_VAR_AE_MAX_DGAIN_AE1, 0x0150 },
	{ MT9M113_VAR_AE_JUMP_DIVISOR, 0x0001 },
	{ MT9M113_VAR_AE_SKIP_FRAMES, 0x0001 },
};

static int mt9m113_start(struct mt9m113 *sensor)
{
	u16 mipi = continuous_clock ? MT9M113_MIPI_CONTROL_CONT_CLK : MT9M113_MIPI_CONTROL_LP;
	unsigned int i;
	int ret;

	if (!sensor->powered) {
		ret = mt9m113_power_on(sensor);
		if (ret)
			return ret;
	}

	ret = mt9m113_identify(sensor);
	if (ret)
		goto err;

	/* vendor mt9m113_reg_init(): table, wait for the MCU, refresh (5), wait again, MIPI control */
	ret = mt9m113_load_init_table(sensor);
	if (ret)
		goto err;
	ret = mt9m113_wait_seq_cmd(sensor);
	if (ret)
		goto err;
	ret = mt9m113_var_write(sensor, MT9M113_VAR_SEQ_CMD, 5);
	if (ret)
		goto err;
	ret = mt9m113_wait_seq_cmd(sensor);
	if (ret)
		goto err;
	ret = mt9m113_write(sensor, MT9M113_MIPI_CONTROL, mipi);
	if (ret)
		goto err;

	/* vendor mt9m113_set_sensor_mode(SENSOR_PREVIEW_MODE), first call: streaming enable, then preview */
	ret = mt9m113_write(sensor, MT9M113_RESET_REGISTER, MT9M113_RESET_REGISTER_STREAM);
	if (ret)
		goto err;
	ret = mt9m113_var_write(sensor, MT9M113_VAR_SEQ_CAP_MODE, 0x0030);
	if (ret)
		goto err;
	msleep(40);
	ret = mt9m113_var_write(sensor, MT9M113_VAR_SEQ_CMD, 1);
	if (ret)
		goto err;

	ret = mt9m113_freeze_update(sensor, true);
	if (ret)
		goto err;
	for (i = 0; i < ARRAY_SIZE(mt9m113_preview_ae); i++) {
		ret = mt9m113_var_write(sensor, mt9m113_preview_ae[i].var, mt9m113_preview_ae[i].val);
		if (ret)
			goto err;
		msleep(1);
	}
	ret = mt9m113_freeze_update(sensor, false);
	if (ret)
		goto err;

	sensor->streaming = true;
	return 0;

err:
	mt9m113_power_off(sensor);
	return ret;
}

static void mt9m113_stop(struct mt9m113 *sensor)
{
	if (sensor->powered)
		mt9m113_power_off(sensor);
}

static int mt9m113_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct mt9m113 *sensor = sd_to_mt9m113(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);
	if (enable) {
		if (!sensor->streaming)
			ret = mt9m113_start(sensor);
	} else {
		mt9m113_stop(sensor);
	}
	mutex_unlock(&sensor->lock);
	return ret;
}

/* ---------------------------------------------------------------------------------------------------------------------------------- */
/* Pad operations: one fixed format, 640x480 UYVY (context A of the sensor) */

static void mt9m113_fill_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = MT9M113_WIDTH;
	fmt->height = MT9M113_HEIGHT;
	fmt->code = MT9M113_MBUS_CODE;
	fmt->field = V4L2_FIELD_NONE;
	/* to be confirmed against real frames (byte order, range) once there is a receiver */
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
}

static int mt9m113_init_state(struct v4l2_subdev *sd, struct v4l2_subdev_state *state)
{
	mt9m113_fill_format(v4l2_subdev_state_get_format(state, 0));
	return 0;
}

static int mt9m113_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *state, struct v4l2_subdev_format *fmt)
{
	/* only one format: whatever is requested, the fixed one is applied and returned */
	mt9m113_fill_format(&fmt->format);
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;
	return 0;
}

static int mt9m113_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_state *state, struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MT9M113_MBUS_CODE;
	return 0;
}

static int mt9m113_enum_frame_size(struct v4l2_subdev *sd, struct v4l2_subdev_state *state, struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MT9M113_MBUS_CODE)
		return -EINVAL;
	fse->min_width = fse->max_width = MT9M113_WIDTH;
	fse->min_height = fse->max_height = MT9M113_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops mt9m113_video_ops = {
	.s_stream = mt9m113_s_stream,
};

static const struct v4l2_subdev_pad_ops mt9m113_pad_ops = {
	.enum_mbus_code = mt9m113_enum_mbus_code,
	.enum_frame_size = mt9m113_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = mt9m113_set_fmt,
};

static const struct v4l2_subdev_ops mt9m113_subdev_ops = {
	.video = &mt9m113_video_ops,
	.pad = &mt9m113_pad_ops,
};

static const struct v4l2_subdev_internal_ops mt9m113_internal_ops = {
	.init_state = mt9m113_init_state,
};

static const struct media_entity_operations mt9m113_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* ---------------------------------------------------------------------------------------------------------------------------------- */
/* debugfs: start/stop by hand and look at the state of the MCU and of the auto exposure */

static int mt9m113_regs_show(struct seq_file *s, void *unused)
{
	struct mt9m113 *sensor = s->private;
	static const struct { const char *name; u32 reg; } regs[] = {
		{ "PLL_CONTROL", MT9M113_PLL_CONTROL },
		{ "STANDBY_CONTROL", MT9M113_STANDBY_CONTROL },
		{ "RESET_REGISTER", MT9M113_RESET_REGISTER },
		{ "MIPI_CONTROL", MT9M113_MIPI_CONTROL },
		{ "COARSE_INT_TIME", MT9M113_COARSE_INT_TIME },
		{ "FINE_INT_TIME", MT9M113_FINE_INT_TIME },
		{ "ANALOG_GAIN (0x3028)", CCI_REG16(0x3028) },
	};
	static const struct { const char *name; u16 var; } vars[] = {
		{ "SEQ_MODE", MT9M113_VAR_SEQ_MODE },
		{ "SEQ_CMD", MT9M113_VAR_SEQ_CMD },
		{ "SEQ_STATE", MT9M113_VAR_SEQ_STATE },
		{ "SEQ_CAP_MODE", MT9M113_VAR_SEQ_CAP_MODE },
		{ "AE_MAX_INDEX", MT9M113_VAR_AE_MAX_INDEX },
	};
	unsigned int i;
	u16 val;

	mutex_lock(&sensor->lock);
	if (!sensor->powered) {
		seq_puts(s, "sensor off (echo 1 > stream)\n");
		goto out;
	}
	if (!mt9m113_read(sensor, MT9M113_CHIP_ID, &val))
		seq_printf(s, "CHIP_ID          0x%04x\n", val);
	for (i = 0; i < ARRAY_SIZE(regs); i++)
		if (!mt9m113_read(sensor, regs[i].reg, &val))
			seq_printf(s, "%-24s 0x%04x (%u)\n", regs[i].name, val, val);
	for (i = 0; i < ARRAY_SIZE(vars); i++)
		if (!mt9m113_var_read(sensor, vars[i].var, &val))
			seq_printf(s, "var %-20s 0x%04x (%u)\n", vars[i].name, val, val);
out:
	mutex_unlock(&sensor->lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mt9m113_regs);

static ssize_t mt9m113_stream_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct mt9m113 *sensor = file->private_data;
	bool enable;
	int ret;

	ret = kstrtobool_from_user(ubuf, count, &enable);
	if (ret)
		return ret;
	ret = mt9m113_s_stream(&sensor->sd, enable);
	return ret ? ret : count;
}

static ssize_t mt9m113_stream_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	struct mt9m113 *sensor = file->private_data;
	char buf[3] = { sensor->streaming ? '1' : '0', '\n', 0 };

	return simple_read_from_buffer(ubuf, count, ppos, buf, 2);
}

static const struct file_operations mt9m113_stream_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = mt9m113_stream_read,
	.write = mt9m113_stream_write,
};

/* ---------------------------------------------------------------------------------------------------------------------------------- */
/* Probe */

static int mt9m113_parse_dt(struct mt9m113 *sensor)
{
	struct device *dev = &sensor->client->dev;
	struct fwnode_handle *ep;
	int ret;

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!ep)
		return dev_err_probe(dev, -EINVAL, "no endpoint found\n");

	sensor->bus_cfg.bus_type = V4L2_MBUS_CSI2_DPHY;
	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &sensor->bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse the endpoint\n");

	if (sensor->bus_cfg.bus_type != V4L2_MBUS_CSI2_DPHY || sensor->bus_cfg.bus.mipi_csi2.num_data_lanes != 1) {
		v4l2_fwnode_endpoint_free(&sensor->bus_cfg);
		return dev_err_probe(dev, -EINVAL, "the sensor needs a MIPI CSI-2 endpoint with one data lane\n");
	}
	return 0;
}

static int mt9m113_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct mt9m113 *sensor;
	unsigned int i;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;
	sensor->client = client;
	mutex_init(&sensor->lock);

	sensor->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(sensor->regmap))
		return dev_err_probe(dev, PTR_ERR(sensor->regmap), "failed to initialise the register map\n");

	ret = mt9m113_parse_dt(sensor);
	if (ret)
		return ret;

	sensor->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(sensor->clk)) {
		ret = dev_err_probe(dev, PTR_ERR(sensor->clk), "failed to get the clock\n");
		goto err_ep;
	}
	sensor->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset)) {
		ret = dev_err_probe(dev, PTR_ERR(sensor->reset), "failed to get the reset GPIO\n");
		goto err_ep;
	}
	sensor->powerdown = devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->powerdown)) {
		ret = dev_err_probe(dev, PTR_ERR(sensor->powerdown), "failed to get the power-down GPIO\n");
		goto err_ep;
	}
	for (i = 0; i < ARRAY_SIZE(mt9m113_supply_names); i++)
		sensor->supplies[i].supply = mt9m113_supply_names[i];
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(sensor->supplies), sensor->supplies);
	if (ret) {
		dev_err_probe(dev, ret, "failed to get the supplies\n");
		goto err_ep;
	}

	/* identify the sensor, then leave it powered down: it is started by s_stream */
	ret = mt9m113_power_on(sensor);
	if (ret) {
		dev_err_probe(dev, ret, "power on failed\n");
		goto err_ep;
	}
	ret = mt9m113_identify(sensor);
	mt9m113_power_off(sensor);
	if (ret)
		goto err_ep;

	v4l2_i2c_subdev_init(&sensor->sd, client, &mt9m113_subdev_ops);
	sensor->sd.internal_ops = &mt9m113_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sensor->sd.entity.ops = &mt9m113_entity_ops;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		goto err_ep;

	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_entity;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_subdev;

	sensor->debugfs = debugfs_create_dir("mt9m113", NULL);
	debugfs_create_file("stream", 0644, sensor->debugfs, sensor, &mt9m113_stream_fops);
	debugfs_create_file("regs", 0444, sensor->debugfs, sensor, &mt9m113_regs_fops);

	dev_info(dev, "MT9M113 front camera ready (%ux%u UYVY, one CSI-2 lane)\n", MT9M113_WIDTH, MT9M113_HEIGHT);
	return 0;

err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
err_ep:
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);
	return ret;
}

static void mt9m113_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mt9m113 *sensor = sd_to_mt9m113(sd);

	debugfs_remove_recursive(sensor->debugfs);
	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);
	if (sensor->powered)
		mt9m113_power_off(sensor);
}

static const struct of_device_id mt9m113_of_match[] = {
	{ .compatible = "aptina,mt9m113" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt9m113_of_match);

static struct i2c_driver mt9m113_driver = {
	.driver = {
		.name = "mt9m113",
		.of_match_table = mt9m113_of_match,
	},
	.probe = mt9m113_probe,
	.remove = mt9m113_remove,
};
module_i2c_driver(mt9m113_driver);

MODULE_DESCRIPTION("Aptina MT9M113 camera sensor");
MODULE_LICENSE("GPL");
