// SPDX-License-Identifier: GPL-2.0-only
/*
 * tenderloin.c -- ASoC machine driver of the HP TouchPad (MSM8660/APQ8060 + WM8958), without the Q6 DSP.
 *
 * Links (vendor sound/soc/msm/msm8x60.c): LPAIF codec speaker I2S -> WM8958 AIF1 (playback) and LPAIF codec mic I2S <- WM8958
 * AIF2 (capture), MSM clock master on both. The WM8958 has no MCLK on this board: each AIF clocks from its own FLL locked
 * to the bit clock of its I2S and running at 8 x BCLK (12.288 MHz at 48 kHz): FLL1/AIF1 for playback, FLL2/AIF2 for capture.
 * The codec has no interrupt wired, so the FLL is not waited for: it locks when the LPAIF starts the bit clock.
 *
 * Outputs (vendor tenderloin_dapm_routes): the speaker amplifier is fed from LINEOUT1/2 and enabled by the WM8958 GPIO1
 * (register write 0x41 on, 0x01 off, as the vendor); the headphones are on HPOUT1L/R with the jack detect on TLMM GPIO 67
 * (high = inserted). Inputs: internal microphone on IN1LN with MICBIAS1, headset microphone on IN2LN with MICBIAS2.
 */

#include <linux/gpio/consumer.h>
#include <linux/mfd/wm8994/registers.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/wm8994.h"

#define TENDERLOIN_FLL_MULT	8	/* FLL = 8 x BCLK = 256 fs with 2 x 16-bit slots */
#define TENDERLOIN_FLL_MIN	4096000	/* minimum AIF clock of the WM8958 */

enum { LINK_PLAYBACK, LINK_CAPTURE, NUM_LINKS };

struct tenderloin_data {
	struct snd_soc_component *codec;
	struct snd_soc_jack hp_jack;
	struct snd_soc_jack_gpio hp_jack_gpio;
	unsigned int bclk[NUM_LINKS];
};

static struct snd_soc_jack_pin tenderloin_hp_jack_pins[] = {
	{ .pin = "Headphones", .mask = SND_JACK_HEADPHONE },
};

static int tenderloin_spk_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *k, int event)
{
	struct tenderloin_data *data = snd_soc_card_get_drvdata(w->dapm->card);

	/* WM8958 GPIO1 as a GPIO output (function 1), level bit 6 (vendor wm8994_en_pwr_amp) */
	return snd_soc_component_write(data->codec, WM8994_GPIO_1, SND_SOC_DAPM_EVENT_ON(event) ? 0x41 : 0x01);
}

static const struct snd_soc_dapm_widget tenderloin_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", tenderloin_spk_event),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route tenderloin_dapm_routes[] = {
	{ "Headphones", NULL, "HPOUT1L" },
	{ "Headphones", NULL, "HPOUT1R" },
	{ "Ext Spk", NULL, "LINEOUT1P" },
	{ "Ext Spk", NULL, "LINEOUT1N" },
	{ "Ext Spk", NULL, "LINEOUT2P" },
	{ "Ext Spk", NULL, "LINEOUT2N" },
	{ "IN1LN", NULL, "Int Mic" },
	{ "Int Mic", NULL, "MICBIAS1" },
	{ "IN2LN", NULL, "Headset Mic" },
	{ "Headset Mic", NULL, "MICBIAS2" },
};

static const struct snd_kcontrol_new tenderloin_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphones"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static int tenderloin_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct tenderloin_data *data = snd_soc_card_get_drvdata(rtd->card);
	int link = substream->stream == SNDRV_PCM_STREAM_CAPTURE ? LINK_CAPTURE : LINK_PLAYBACK;
	int fll_id = link == LINK_CAPTURE ? WM8994_FLL2 : WM8994_FLL1;
	int sysclk = link == LINK_CAPTURE ? WM8994_SYSCLK_FLL2 : WM8994_SYSCLK_FLL1;
	unsigned int bclk, fll;
	int ret;

	/* lpass-cpu runs the bit clock at rate x bit width x 2 */
	bclk = params_rate(params) * params_width(params) * 2;
	if (bclk == data->bclk[link])
		return 0;

	fll = max(bclk * TENDERLOIN_FLL_MULT, TENDERLOIN_FLL_MIN);
	ret = snd_soc_dai_set_pll(codec_dai, fll_id, WM8994_FLL_SRC_BCLK, bclk, fll);
	if (ret < 0) {
		dev_err(rtd->dev, "FLL%d %u Hz from BCLK %u Hz: %d\n", fll_id, fll, bclk, ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, sysclk, fll, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "SYSCLK from FLL%d: %d\n", fll_id, ret);
		return ret;
	}

	data->bclk[link] = bclk;
	return 0;
}

static const struct snd_soc_ops tenderloin_ops = {
	.hw_params = tenderloin_hw_params,
};

static int tenderloin_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = &card->dapm;
	struct tenderloin_data *data = snd_soc_card_get_drvdata(card);
	int ret;

	data->codec = snd_soc_rtd_to_codec(rtd, 0)->component;

	/* unconnected codec pins (vendor tenderloin_wm8994_init) */
	snd_soc_dapm_nc_pin(dapm, "SPKOUTLP");
	snd_soc_dapm_nc_pin(dapm, "SPKOUTLN");
	snd_soc_dapm_nc_pin(dapm, "SPKOUTRP");
	snd_soc_dapm_nc_pin(dapm, "SPKOUTRN");
	snd_soc_dapm_nc_pin(dapm, "HPOUT2P");
	snd_soc_dapm_nc_pin(dapm, "HPOUT2N");

	if (!data->hp_jack_gpio.desc)
		return 0;

	ret = snd_soc_card_jack_new_pins(card, "Headphone Jack", SND_JACK_HEADPHONE, &data->hp_jack,
					 tenderloin_hp_jack_pins, ARRAY_SIZE(tenderloin_hp_jack_pins));
	if (ret)
		return ret;

	data->hp_jack_gpio.name = "hp-det";
	data->hp_jack_gpio.report = SND_JACK_HEADPHONE;
	data->hp_jack_gpio.debounce_time = 30;
	data->hp_jack_gpio.wake = true;
	return snd_soc_jack_add_gpios(&data->hp_jack, 1, &data->hp_jack_gpio);
}

SND_SOC_DAILINK_DEFS(speaker,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "wm8994-aif1")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(mic,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "wm8994-aif2")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link tenderloin_dai_links[NUM_LINKS] = {
	[LINK_PLAYBACK] = {
		.name		= "WM8958 AIF1",
		.stream_name	= "Playback",
		.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC,
		.playback_only	= 1,
		.init		= tenderloin_init,
		.ops		= &tenderloin_ops,
		SND_SOC_DAILINK_REG(speaker),
	},
	[LINK_CAPTURE] = {
		.name		= "WM8958 AIF2",
		.stream_name	= "Capture",
		.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC,
		.capture_only	= 1,
		.ops		= &tenderloin_ops,
		SND_SOC_DAILINK_REG(mic),
	},
};

static struct snd_soc_card tenderloin_card = {
	.name			= "TouchPad",
	.owner			= THIS_MODULE,
	.dai_link		= tenderloin_dai_links,
	.num_links		= NUM_LINKS,
	.dapm_widgets		= tenderloin_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tenderloin_dapm_widgets),
	.dapm_routes		= tenderloin_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(tenderloin_dapm_routes),
	.controls		= tenderloin_controls,
	.num_controls		= ARRAY_SIZE(tenderloin_controls),
	.fully_routed		= true,
};

/* qcom,cpu-dai index i: the LPAIF DAI of link i (it is also the PCM platform); audio-codec: the WM8958 */
static int tenderloin_parse_link(struct device *dev, struct snd_soc_dai_link *link, int i, struct device_node *codec)
{
	struct of_phandle_args args;
	int ret;

	ret = of_parse_phandle_with_args(dev->of_node, "qcom,cpu-dai", "#sound-dai-cells", i, &args);
	if (ret)
		return dev_err_probe(dev, ret, "no qcom,cpu-dai %d\n", i);
	link->cpus->of_node = args.np;
	link->cpus->dai_args = NULL;
	ret = snd_soc_get_dai_name(&args, &link->cpus->dai_name);
	if (ret) {
		of_node_put(args.np);
		return dev_err_probe(dev, ret, "cpu DAI %d not ready\n", i);
	}
	link->platforms->of_node = args.np;
	link->codecs->of_node = codec;
	return 0;
}

static int tenderloin_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card = &tenderloin_card;
	struct tenderloin_data *data;
	struct device_node *codec;
	int i, ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	codec = of_parse_phandle(dev->of_node, "audio-codec", 0);
	if (!codec)
		return dev_err_probe(dev, -EINVAL, "no audio-codec\n");
	for (i = 0; i < NUM_LINKS; i++) {
		ret = tenderloin_parse_link(dev, &tenderloin_dai_links[i], i, codec);
		if (ret)
			return ret;
	}

	data->hp_jack_gpio.desc = devm_gpiod_get_optional(dev, "hp-det", GPIOD_IN);
	if (IS_ERR(data->hp_jack_gpio.desc))
		return dev_err_probe(dev, PTR_ERR(data->hp_jack_gpio.desc), "hp-det gpio\n");

	card->dev = dev;
	snd_soc_card_set_drvdata(card, data);
	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id tenderloin_of_match[] = {
	{ .compatible = "hp,tenderloin-sndcard" },
	{}
};
MODULE_DEVICE_TABLE(of, tenderloin_of_match);

static struct platform_driver tenderloin_driver = {
	.driver = {
		.name		= "tenderloin-sndcard",
		.of_match_table	= tenderloin_of_match,
	},
	.probe = tenderloin_probe,
};
module_platform_driver(tenderloin_driver);

MODULE_DESCRIPTION("HP TouchPad sound card (MSM8660 LPAIF + WM8958)");
MODULE_LICENSE("GPL");
