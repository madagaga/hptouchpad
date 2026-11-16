// SPDX-License-Identifier: GPL-2.0-only
/*
 * lpass-msm8660.c -- ALSA SoC CPU DAI driver for the LPASS audio interface (LPAIF) of the Qualcomm MSM8660/APQ8060
 * (HP TouchPad), driven from the apps CPU without the Q6 DSP.
 *
 * The vendor kernel plays through the Q6 DSP, but its tree also has a DSP-less driver for this block
 * (sound/soc/msm/lpass-i2s.c, lpass-dma.c, CONFIG_SND_SOC_MSM8660_LPAIF; register map in
 * arch/arm/mach-msm/include/mach/audio_dma_msm8k.h): base 0x28100000, I2S_CTL at 0x4 + 4 * port (codec speaker 0, codec mic 1,
 * MI2S 2, secondary speaker 3, secondary mic 4), same bit fields as the IPQ806X LPAIF; IRQ block 0x3000, DMA 0x6000 + 0x1000 * ch.
 * The vendor speaker path uses DMA channel 0 with interface code 1 (codec), I2S_CTL 0x4400 | WS (enable, SD0).
 *
 * Playback on the codec speaker interface (the WM8958 AIF1 on the TouchPad), capture on the codec mic interface (the WM8958
 * AIF2): vendor msm8660-dma.c DAI_MIC, I2S_CTL port 1 (0x0110 | WS: mic enable, SD0), write DMA channel 5 (0xb000) with
 * DMA_CTL interface code 1 as well, which the generic "port + dmactl_audif_start" rule cannot give (wrdma_audif, patch 0026).
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "lpass-lpaif-reg.h"
#include "lpass.h"

enum msm8660_lpaif_i2s_ports {
	MSM8660_LPAIF_I2S_PORT_CODEC_SPK,
	MSM8660_LPAIF_I2S_PORT_CODEC_MIC,
	MSM8660_LPAIF_I2S_PORT_MI2S,
	MSM8660_LPAIF_I2S_PORT_SEC_SPK,
	MSM8660_LPAIF_I2S_PORT_SEC_MIC,
};

#define MSM8660_LPAIF_RDMA_CHAN_CODEC_SPK	0
#define MSM8660_LPAIF_WRDMA_CHAN_CODEC_MIC	5

static struct snd_soc_dai_driver msm8660_lpass_cpu_dai_driver[] = {
{
	.id	= MSM8660_LPAIF_I2S_PORT_CODEC_SPK,	/* = MI2S_PRIMARY for the generic code: an I2S port */
	.name	= "lpass-codec-spkr",
	.playback = {
		.stream_name	= "lpass-codec-spkr-playback",
		.formats	= SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_S24 | SNDRV_PCM_FMTBIT_S32,
		.rates		= SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 |
				  SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000,
		.rate_min	= 8000,
		.rate_max	= 96000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops	= &asoc_qcom_lpass_cpu_dai_ops,
}, {
	.id	= MSM8660_LPAIF_I2S_PORT_CODEC_MIC,	/* = MI2S_SECONDARY for the generic code */
	.name	= "lpass-codec-mic",
	.capture = {
		.stream_name	= "lpass-codec-mic-capture",
		.formats	= SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_S24 | SNDRV_PCM_FMTBIT_S32,
		.rates		= SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 |
				  SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000,
		.rate_min	= 8000,
		.rate_max	= 96000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops	= &asoc_qcom_lpass_cpu_dai_ops,
},
};

static int msm8660_lpass_alloc_dma_channel(struct lpass_data *drvdata, int dir, unsigned int dai_id)
{
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		return MSM8660_LPAIF_RDMA_CHAN_CODEC_SPK;
	return MSM8660_LPAIF_WRDMA_CHAN_CODEC_MIC;
}

static int msm8660_lpass_free_dma_channel(struct lpass_data *drvdata, int chan, unsigned int dai_id)
{
	return 0;
}

static const struct lpass_variant msm8660_data = {
	.i2sctrl_reg_base	= 0x0004,
	.i2sctrl_reg_stride	= 0x04,
	.i2s_ports		= 5,
	.irq_reg_base		= 0x3000,
	.irq_reg_stride		= 0x1000,
	.irq_ports		= 3,
	.rdma_reg_base		= 0x6000,
	.rdma_reg_stride	= 0x1000,
	.rdma_channels		= 5,
	.wrdma_reg_base		= 0xB000,
	.wrdma_reg_stride	= 0x1000,
	.wrdma_channel_start	= 5,
	.wrdma_channels		= 4,
	.dmactl_audif_start	= 1,	/* codec speaker port 0 -> interface code 1 */
	.wrdma_audif		= 1,	/* codec mic: interface code 1 (vendor DMA_CTL 0x81b) */
	.dma_enable_at_start	= true,	/* patch 0026: no DMA before the trigger (pointer ran ahead, xrun at start) */
	.loopback		= REG_FIELD_ID(0x0004, 15, 15, 5, 0x4),
	.spken			= REG_FIELD_ID(0x0004, 14, 14, 5, 0x4),
	.spkmode		= REG_FIELD_ID(0x0004, 10, 13, 5, 0x4),
	.spkmono		= REG_FIELD_ID(0x0004, 9, 9, 5, 0x4),
	.micen			= REG_FIELD_ID(0x0004, 8, 8, 5, 0x4),
	.micmode		= REG_FIELD_ID(0x0004, 4, 7, 5, 0x4),
	.micmono		= REG_FIELD_ID(0x0004, 3, 3, 5, 0x4),
	.wssrc			= REG_FIELD_ID(0x0004, 2, 2, 5, 0x4),
	.bitwidth		= REG_FIELD_ID(0x0004, 0, 1, 5, 0x4),

	.rdma_dyncclk		= REG_FIELD_ID(0x6000, 12, 12, 5, 0x1000),
	.rdma_bursten		= REG_FIELD_ID(0x6000, 11, 11, 5, 0x1000),
	.rdma_wpscnt		= REG_FIELD_ID(0x6000, 8, 10, 5, 0x1000),
	.rdma_intf		= REG_FIELD_ID(0x6000, 4, 7, 5, 0x1000),
	.rdma_fifowm		= REG_FIELD_ID(0x6000, 1, 3, 5, 0x1000),
	.rdma_enable		= REG_FIELD_ID(0x6000, 0, 0, 5, 0x1000),

	.wrdma_dyncclk		= REG_FIELD_ID(0xB000, 12, 12, 4, 0x1000),
	.wrdma_bursten		= REG_FIELD_ID(0xB000, 11, 11, 4, 0x1000),
	.wrdma_wpscnt		= REG_FIELD_ID(0xB000, 8, 10, 4, 0x1000),
	.wrdma_intf		= REG_FIELD_ID(0xB000, 4, 7, 4, 0x1000),
	.wrdma_fifowm		= REG_FIELD_ID(0xB000, 1, 3, 4, 0x1000),
	.wrdma_enable		= REG_FIELD_ID(0xB000, 0, 0, 4, 0x1000),

	.dai_driver		= msm8660_lpass_cpu_dai_driver,
	.num_dai		= ARRAY_SIZE(msm8660_lpass_cpu_dai_driver),
	.dai_osr_clk_names	= (const char *[]) { "spkr-osr-clk", "mic-osr-clk" },
	.dai_bit_clk_names	= (const char *[]) { "spkr-bit-clk", "mic-bit-clk" },
	.alloc_dma_channel	= msm8660_lpass_alloc_dma_channel,
	.free_dma_channel	= msm8660_lpass_free_dma_channel,
};

static const struct of_device_id msm8660_lpass_cpu_device_id[] __maybe_unused = {
	{ .compatible = "qcom,msm8660-lpass-cpu", .data = &msm8660_data },
	{}
};
MODULE_DEVICE_TABLE(of, msm8660_lpass_cpu_device_id);

static struct platform_driver msm8660_lpass_cpu_platform_driver = {
	.driver	= {
		.name		= "msm8660-lpass-cpu",
		.of_match_table	= of_match_ptr(msm8660_lpass_cpu_device_id),
	},
	.probe	= asoc_qcom_lpass_cpu_platform_probe,
	.remove = asoc_qcom_lpass_cpu_platform_remove,
};
module_platform_driver(msm8660_lpass_cpu_platform_driver);

MODULE_DESCRIPTION("Qualcomm MSM8660 LPASS CPU driver (DSP-less)");
MODULE_LICENSE("GPL");
