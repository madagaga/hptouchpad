// SPDX-License-Identifier: GPL-2.0-only
/*
 * MSM8660 (APQ8060, HP TouchPad) camera capture: CSI1 receiver + VFE 3.1, front camera MT9M113 -> /dev/videoN, NV12 640x480.
 *
 * Mainline has no driver for this block (camss starts at the MSM8916). This one is minimal and replays what the vendor stack does:
 *  - CSI1 (receiver and D-PHY in one window): the sequence of msm_io_8x60.c msm_camio_csi_config() for one lane, 8-bit;
 *  - VFE 3.1: reset as msm_vfe31.c, then the register state written by the closed webOS camera library while its front camera
 *    preview ran (data/webos/camregs/webos-camregs-2.txt, msm8660_vfe_regs.h): CAMIF 1280 bytes x 480 lines of UYVY, write master
 *    0 = Y plane, write master 4 = CbCr plane 640x240 at Y + 640 * 480, both in composite group 0 (frame done = IRQ_STATUS_0 bit 21),
 *    ping/pong addresses at +4/+8 of each write master block;
 *  - clocks: vfe_ahb/vfe_axi through the clock framework (same bits as the MSM8960 table); the camera clocks whose registers differ
 *    from the MSM8960 table (VFE core clock, CSI source/core, CSI1 to VFE, CSI interface clocks) are written in the MMSS clock
 *    controller with the values read under webOS (STATUS.md "Camera: webOS register dump").
 * Buffers are physical (vb2 dma-contig): the vendor stack did not use the VFE SMMU either.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>

struct vfe_reg {
	u16 off;
	u32 val;
};

#include "msm8660_vfe_regs.h"

#define DRV_NAME		"msm8660-vfe"
#define WIDTH			640
#define HEIGHT			480
#define Y_SIZE			(WIDTH * HEIGHT)
#define IMAGE_SIZE		(Y_SIZE * 3 / 2)	/* NV12 */

/* MMSS clock controller (0x04000000), vendor clock-8x60.c and the webOS dump */
#define MMSS_AHB_EN		0x008
#define AHB_CSI0_P		BIT(7)
#define AHB_CSI1_P		BIT(20)
#define MMSS_MAXI_EN		0x018
#define MAXI_VFE_BITS		BIT(18)	/* bit 19, also set under webOS, is the video codec AXI clock (vcodec_axi) */
#define MMSS_CSI_CC		0x040
#define CSI_CC_CSI0		BIT(0)
#define CSI_CC_ROOT		BIT(2)
#define CSI_CC_CSI1		BIT(7)
#define MMSS_CSI_NS		0x048
#define CSI_NS_PLL8_DIV1	0x2	/* 384 MHz */
#define MMSS_VFE_CC		0x104
#define VFE_CC_EN		BIT(0)
#define VFE_CC_ROOT		BIT(2)
#define VFE_CC_MND		BIT(5)
#define VFE_CC_MODE		BIT(7)	/* set by webOS with the MND counter */
#define VFE_CC_CSI1_VFE		BIT(10)
#define VFE_CC_CSI0_VFE		BIT(12)
#define VFE_CC_ON		(VFE_CC_EN | VFE_CC_ROOT | VFE_CC_MND | VFE_CC_MODE | VFE_CC_CSI1_VFE | VFE_CC_CSI0_VFE)
#define MMSS_VFE_MD		0x108
#define MMSS_VFE_NS		0x10c
#define VFE_MD_WEBOS		0x000002f8
#define VFE_NS_WEBOS		0x00fa0001
#define MMSS_VFE_GFS		0x198
#define GFS_ENABLE		BIT(8)
#define GFS_CLAMP		BIT(5)

/* CSI1 receiver (0x04900000), msm_io_8x60.c */
#define MIPI_PHY_CONTROL	0x00
#define MIPI_PROTOCOL_CONTROL	0x04
#define MIPI_INTERRUPT_STATUS	0x08
#define MIPI_INTERRUPT_MASK	0x0c
#define MIPI_PHY_D1_CONTROL	0x20	/* PHY power: CLK/DATA_PHY_SHUTDOWNB */
#define MIPI_CAMERA_CNTL	0x24
#define MIPI_PHY_D2_CONTROL	0x2c
#define MIPI_PHY_D3_CONTROL	0x30
#define MIPI_PHY_D0_CONTROL	0x34
#define MIPI_PHY_D0_CONTROL2	0x38
#define MIPI_PHY_CL_CONTROL	0x48

/* VFE 3.1 (0x04500000), msm_vfe31.h */
#define VFE_HW_VERSION		0x000
#define VFE_GLOBAL_RESET	0x004
#define VFE_IRQ_CMD		0x018
#define VFE_IRQ_MASK_0		0x01c
#define VFE_IRQ_MASK_1		0x020
#define VFE_IRQ_CLEAR_0		0x024
#define VFE_IRQ_CLEAR_1		0x028
#define VFE_IRQ_STATUS_0	0x02c
#define VFE_IRQ_STATUS_1	0x030
#define VFE_BUS_CMD		0x038
#define VFE_BUS_PP_STATUS	0x180
#define VFE_AXI_CMD		0x1d8
#define VFE_AXI_STATUS		0x1dc
#define VFE_CAMIF_COMMAND	0x1e0
#define VFE_CAMIF_STATUS	0x204
#define VFE_REG_UPDATE		0x260

#define WM_BASE(n)		(0x04c + 0x18 * (n))
#define WM_PING(n)		(WM_BASE(n) + 0x4)
#define WM_PONG(n)		(WM_BASE(n) + 0x8)
#define WM_Y			0
#define WM_CBCR			4
#define WM_OTHER_Y		1	/* second webOS output (disabled in its state): pointed at the scratch buffer */
#define WM_OTHER_CBCR		5

#define RESET_ALL		0x3ff
#define RESET_STOP		0x3ef
#define IRQ1_RESET_ACK		BIT(22)
#define IRQ0_CAMIF_SOF		BIT(0)
#define IRQ0_REG_UPDATE		BIT(5)
#define IRQ0_COMPOSITE0		BIT(21)
#define IRQ1_ERRORS		GENMASK(21, 0)
#define IRQ_MASK0_WEBOS		0x00efe021
#define BUS_RELOAD		0x3fff
#define CAMIF_START		0x1
#define CAMIF_STOP_NOW		0x2

struct vfe_buf {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct vfe {
	struct device *dev;
	void __iomem *base;
	void __iomem *csi;
	void __iomem *mmss;
	struct clk *ahb_clk;
	struct clk *axi_clk;
	int irq;

	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct vb2_queue queue;
	struct mutex lock;	/* video device and queue */
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *sensor;

	spinlock_t slock;	/* buffers below and the ping/pong registers */
	struct list_head pending;
	struct vfe_buf *slot[2];	/* 0 = ping, 1 = pong */
	void *scratch;
	dma_addr_t scratch_dma;
	unsigned int sequence;
	unsigned int errors;
	bool streaming;
	struct completion reset_done;	/* reset acknowledge interrupt (IRQ_STATUS_1 bit 22) */
};

static inline void vfe_w(struct vfe *v, u32 off, u32 val)
{
	writel_relaxed(val, v->base + off);
}

static inline u32 vfe_r(struct vfe *v, u32 off)
{
	return readl_relaxed(v->base + off);
}

static void mmss_set(struct vfe *v, u32 off, u32 bits, bool on)
{
	u32 val = readl(v->mmss + off);

	writel(on ? val | bits : val & ~bits, v->mmss + off);
}

/* ---- clocks and power ---- */

static int vfe_power_on(struct vfe *v)
{
	u32 gfs = readl(v->mmss + MMSS_VFE_GFS);
	int ret;

	ret = clk_prepare_enable(v->ahb_clk);
	if (ret)
		return ret;
	ret = clk_prepare_enable(v->axi_clk);
	if (ret)
		goto err_ahb;
	mmss_set(v, MMSS_MAXI_EN, MAXI_VFE_BITS, true);
	mmss_set(v, MMSS_AHB_EN, AHB_CSI0_P | AHB_CSI1_P, true);

	/* CSI source 384 MHz, then the root and both CSI cores (the vendor turns CSI0 and CSI1 on) */
	writel(CSI_NS_PLL8_DIV1, v->mmss + MMSS_CSI_NS);
	mmss_set(v, MMSS_CSI_CC, CSI_CC_ROOT, true);
	mmss_set(v, MMSS_CSI_CC, CSI_CC_CSI0 | CSI_CC_CSI1, true);

	/* VFE core clock as webOS left it while streaming (MND counter), then the CSI -> VFE clocks */
	writel(VFE_NS_WEBOS, v->mmss + MMSS_VFE_NS);
	writel(VFE_MD_WEBOS, v->mmss + MMSS_VFE_MD);
	mmss_set(v, MMSS_VFE_CC, VFE_CC_ROOT | VFE_CC_MND | VFE_CC_MODE, true);
	mmss_set(v, MMSS_VFE_CC, VFE_CC_EN | VFE_CC_CSI1_VFE | VFE_CC_CSI0_VFE, true);

	/* the footswitch is on at boot (0x300) and webOS left it so; switch it on if something turned it off */
	if (!(gfs & GFS_ENABLE) || (gfs & GFS_CLAMP)) {
		writel((gfs | GFS_ENABLE), v->mmss + MMSS_VFE_GFS);
		udelay(1);
		writel((gfs | GFS_ENABLE) & ~GFS_CLAMP, v->mmss + MMSS_VFE_GFS);
		udelay(1);
		dev_info(v->dev, "VFE footswitch was 0x%08x, enabled\n", gfs);
	}
	udelay(10);
	return 0;

err_ahb:
	clk_disable_unprepare(v->ahb_clk);
	return ret;
}

static void vfe_power_off(struct vfe *v)
{
	mmss_set(v, MMSS_VFE_CC, VFE_CC_EN | VFE_CC_CSI1_VFE | VFE_CC_CSI0_VFE, false);
	mmss_set(v, MMSS_CSI_CC, CSI_CC_CSI0 | CSI_CC_CSI1 | CSI_CC_ROOT, false);
	mmss_set(v, MMSS_AHB_EN, AHB_CSI0_P | AHB_CSI1_P, false);
	clk_disable_unprepare(v->axi_clk);
	clk_disable_unprepare(v->ahb_clk);
}

/* ---- CSI1 ---- */

static void csi_start(struct vfe *v)
{
	writel(0x4, v->csi + MIPI_PHY_CONTROL);			/* SOT_ECC_EN */
	writel(0x08000000, v->csi + MIPI_PROTOCOL_CONTROL);	/* SW reset */
	writel(0x00260000, v->csi + MIPI_PROTOCOL_CONTROL);	/* long header capture, decode id, ECC, 8-bit */
	writel(0x140f0018, v->csi + MIPI_PHY_D0_CONTROL2);	/* settle 0x14, HS termination, LP receiver, SOT error */
	writel(0x0f000004, v->csi + MIPI_PHY_CL_CONTROL);
	writel(0, v->csi + MIPI_PHY_D0_CONTROL);
	writel(0x300, v->csi + MIPI_PHY_D1_CONTROL);		/* clock and data PHY out of shutdown */
	writel(0, v->csi + MIPI_PHY_D2_CONTROL);
	writel(0, v->csi + MIPI_PHY_D3_CONTROL);
	writel(0xe404, v->csi + MIPI_CAMERA_CNTL);		/* lane assignment 0xe4, one lane */
	writel(0xf017f3c0, v->csi + MIPI_INTERRUPT_MASK);
	writel(0xf017f3c0, v->csi + MIPI_INTERRUPT_STATUS);
}

static void csi_stop(struct vfe *v)
{
	writel(0, v->csi + MIPI_PHY_D0_CONTROL2);
	writel(0, v->csi + MIPI_PHY_CL_CONTROL);
	usleep_range(10000, 11000);
	writel(readl(v->csi + MIPI_PHY_D1_CONTROL) & ~0x300, v->csi + MIPI_PHY_D1_CONTROL);
	usleep_range(5000, 6000);
	writel(0, v->csi + MIPI_INTERRUPT_MASK);
	writel(0, v->csi + MIPI_INTERRUPT_STATUS);
}

/* ---- VFE ---- */

static int vfe_reset(struct vfe *v, u32 cmd)
{
	int ret;

	/*
	 * As msm_vfe31.c vfe31_reset(): all interrupts off and cleared, then only the reset acknowledge interrupt enabled
	 * (VFE_IMASK_WHILE_STOPPING_1) before the reset command; the acknowledge arrives through the interrupt handler.
	 * IRQ_STATUS_1 does not reliably latch the acknowledge while its interrupt is masked: polling it with every interrupt
	 * masked missed it on about half of the starts (VIDIOC_STREAMON timeout, STATUS.md "Camera start fix").
	 */
	reinit_completion(&v->reset_done);
	vfe_w(v, VFE_IRQ_MASK_0, 0);
	vfe_w(v, VFE_IRQ_MASK_1, 0);
	vfe_w(v, VFE_IRQ_CLEAR_0, 0xffffffff);
	vfe_w(v, VFE_IRQ_CLEAR_1, 0xffffffff);
	vfe_w(v, VFE_IRQ_CMD, 1);
	wmb();
	vfe_w(v, VFE_IRQ_MASK_1, IRQ1_RESET_ACK);
	wmb();
	vfe_w(v, VFE_GLOBAL_RESET, cmd);
	wmb();
	ret = wait_for_completion_timeout(&v->reset_done, msecs_to_jiffies(100)) ? 0 : -ETIMEDOUT;
	vfe_w(v, VFE_IRQ_MASK_1, 0);
	vfe_w(v, VFE_IRQ_CLEAR_1, IRQ1_RESET_ACK);
	vfe_w(v, VFE_IRQ_CMD, 1);
	if (ret)
		dev_err(v->dev, "VFE reset 0x%x: no acknowledge\n", cmd);
	return ret;
}

static dma_addr_t buf_addr(struct vfe *v, struct vfe_buf *b)
{
	return b ? vb2_dma_contig_plane_dma_addr(&b->vb.vb2_buf, 0) : v->scratch_dma;
}

/* point one slot (ping or pong) of both write masters at a buffer, or at the scratch buffer when none is queued */
static void vfe_set_slot(struct vfe *v, int slot, struct vfe_buf *b)
{
	dma_addr_t a = buf_addr(v, b);

	v->slot[slot] = b;
	vfe_w(v, slot ? WM_PONG(WM_Y) : WM_PING(WM_Y), a);
	vfe_w(v, slot ? WM_PONG(WM_CBCR) : WM_PING(WM_CBCR), a + Y_SIZE);
}

static struct vfe_buf *vfe_next(struct vfe *v)
{
	struct vfe_buf *b = list_first_entry_or_null(&v->pending, struct vfe_buf, list);

	if (b)
		list_del(&b->list);
	return b;
}

static irqreturn_t vfe_irq(int irq, void *data)
{
	struct vfe *v = data;
	u32 st0, st1, pp, camif;
	unsigned long flags;

	st0 = vfe_r(v, VFE_IRQ_STATUS_0);
	st1 = vfe_r(v, VFE_IRQ_STATUS_1);
	camif = vfe_r(v, VFE_CAMIF_STATUS);
	pp = vfe_r(v, VFE_BUS_PP_STATUS);
	vfe_w(v, VFE_IRQ_CLEAR_0, st0);
	vfe_w(v, VFE_IRQ_CLEAR_1, st1);
	vfe_w(v, VFE_IRQ_CMD, 1);

	if (st1 & IRQ1_RESET_ACK)
		complete(&v->reset_done);

	if ((st1 & IRQ1_ERRORS) && v->errors++ < 20)
		dev_warn(v->dev, "VFE error: status0 0x%08x status1 0x%08x camif 0x%08x\n", st0, st1, camif);

	if (st0 & IRQ0_COMPOSITE0) {
		/* the write master just finished the slot whose ping/pong bit is set (msm_vfe31.c vfe31_process_output_path_irq) */
		int slot = (pp & BIT(WM_Y)) ? 0 : 1;
		struct vfe_buf *done;

		spin_lock_irqsave(&v->slock, flags);
		done = v->slot[slot];
		vfe_set_slot(v, slot, vfe_next(v));
		spin_unlock_irqrestore(&v->slock, flags);

		if (done) {
			done->vb.vb2_buf.timestamp = ktime_get_ns();
			done->vb.sequence = v->sequence;
			done->vb.field = V4L2_FIELD_NONE;
			vb2_buffer_done(&done->vb.vb2_buf, VB2_BUF_STATE_DONE);
		}
		v->sequence++;
	}
	return IRQ_HANDLED;
}

static int vfe_start(struct vfe *v)
{
	unsigned long flags;
	unsigned int i;
	int ret;

	ret = vfe_power_on(v);
	if (ret)
		return ret;
	dev_dbg(v->dev, "VFE version 0x%08x\n", vfe_r(v, VFE_HW_VERSION));
	ret = vfe_reset(v, RESET_ALL);
	if (ret)
		goto err_power;

	for (i = 0; i < ARRAY_SIZE(vfe31_webos_preview); i++)
		vfe_w(v, vfe31_webos_preview[i].off, vfe31_webos_preview[i].val);

	spin_lock_irqsave(&v->slock, flags);
	vfe_set_slot(v, 0, vfe_next(v));
	vfe_set_slot(v, 1, vfe_next(v));
	spin_unlock_irqrestore(&v->slock, flags);
	for (i = 0; i < 2; i++) {
		vfe_w(v, i ? WM_PONG(WM_OTHER_Y) : WM_PING(WM_OTHER_Y), v->scratch_dma);
		vfe_w(v, i ? WM_PONG(WM_OTHER_CBCR) : WM_PING(WM_OTHER_CBCR), v->scratch_dma + Y_SIZE);
	}
	vfe_w(v, VFE_BUS_CMD, BUS_RELOAD);
	wmb();

	csi_start(v);
	ret = v4l2_subdev_call(v->sensor, video, s_stream, 1);
	if (ret) {
		dev_err(v->dev, "sensor start: %d\n", ret);
		goto err_csi;
	}

	v->sequence = 0;
	v->errors = 0;
	vfe_w(v, VFE_IRQ_CLEAR_0, 0xffffffff);
	vfe_w(v, VFE_IRQ_CLEAR_1, 0xffffffff);
	vfe_w(v, VFE_IRQ_CMD, 1);
	vfe_w(v, VFE_IRQ_MASK_0, IRQ_MASK0_WEBOS);
	vfe_w(v, VFE_IRQ_MASK_1, IRQ1_ERRORS | IRQ1_RESET_ACK);
	vfe_w(v, VFE_REG_UPDATE, 1);
	wmb();
	vfe_w(v, VFE_CAMIF_COMMAND, CAMIF_START);
	v->streaming = true;
	return 0;

err_csi:
	csi_stop(v);
err_power:
	vfe_power_off(v);
	return ret;
}

static void vfe_stop(struct vfe *v)
{
	u32 st;

	vfe_w(v, VFE_IRQ_MASK_0, 0);
	vfe_w(v, VFE_IRQ_MASK_1, 0);
	vfe_w(v, VFE_IRQ_CLEAR_0, 0xffffffff);
	vfe_w(v, VFE_IRQ_CLEAR_1, 0xffffffff);
	vfe_w(v, VFE_IRQ_CMD, 1);
	vfe_w(v, VFE_CAMIF_COMMAND, CAMIF_STOP_NOW);
	vfe_w(v, VFE_AXI_CMD, 1);
	if (readl_poll_timeout(v->base + VFE_AXI_STATUS, st, st & 1, 10, 50000))
		dev_warn(v->dev, "AXI halt: no acknowledge\n");
	vfe_w(v, VFE_AXI_CMD, 0);
	vfe_reset(v, RESET_STOP);
	synchronize_irq(v->irq);

	v4l2_subdev_call(v->sensor, video, s_stream, 0);
	csi_stop(v);
	vfe_power_off(v);
	v->streaming = false;
	dev_info(v->dev, "stopped after %u frames, %u error interrupts\n", v->sequence, v->errors);
}

/* ---- vb2 ---- */

static int vfe_queue_setup(struct vb2_queue *q, unsigned int *nbuf, unsigned int *nplanes, unsigned int sizes[],
			   struct device *alloc[])
{
	if (*nplanes)
		return sizes[0] < IMAGE_SIZE ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = IMAGE_SIZE;
	return 0;
}

static int vfe_buf_prepare(struct vb2_buffer *vb)
{
	if (vb2_plane_size(vb, 0) < IMAGE_SIZE)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, IMAGE_SIZE);
	return 0;
}

static void vfe_buf_queue(struct vb2_buffer *vb)
{
	struct vfe *v = vb2_get_drv_priv(vb->vb2_queue);
	struct vfe_buf *b = container_of(to_vb2_v4l2_buffer(vb), struct vfe_buf, vb);
	unsigned long flags;

	spin_lock_irqsave(&v->slock, flags);
	list_add_tail(&b->list, &v->pending);
	spin_unlock_irqrestore(&v->slock, flags);
}

static void vfe_return_all(struct vfe *v, enum vb2_buffer_state state)
{
	struct vfe_buf *b, *tmp;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&v->slock, flags);
	list_for_each_entry_safe(b, tmp, &v->pending, list) {
		list_del(&b->list);
		vb2_buffer_done(&b->vb.vb2_buf, state);
	}
	for (i = 0; i < 2; i++) {
		if (v->slot[i])
			vb2_buffer_done(&v->slot[i]->vb.vb2_buf, state);
		v->slot[i] = NULL;
	}
	spin_unlock_irqrestore(&v->slock, flags);
}

static int vfe_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct vfe *v = vb2_get_drv_priv(q);
	int ret = vfe_start(v);

	if (ret)
		vfe_return_all(v, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void vfe_stop_streaming(struct vb2_queue *q)
{
	struct vfe *v = vb2_get_drv_priv(q);

	if (v->streaming)
		vfe_stop(v);
	vfe_return_all(v, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops vfe_vb2_ops = {
	.queue_setup = vfe_queue_setup,
	.buf_prepare = vfe_buf_prepare,
	.buf_queue = vfe_buf_queue,
	.start_streaming = vfe_start_streaming,
	.stop_streaming = vfe_stop_streaming,
};

/* ---- V4L2 ---- */

static void vfe_fill_fmt(struct v4l2_pix_format *f)
{
	f->width = WIDTH;
	f->height = HEIGHT;
	f->pixelformat = V4L2_PIX_FMT_NV12;
	f->field = V4L2_FIELD_NONE;
	f->bytesperline = WIDTH;
	f->sizeimage = IMAGE_SIZE;
	f->colorspace = V4L2_COLORSPACE_SRGB;
	f->ycbcr_enc = V4L2_YCBCR_ENC_601;
	f->quantization = V4L2_QUANTIZATION_LIM_RANGE;
	f->xfer_func = V4L2_XFER_FUNC_SRGB;
}

static int vfe_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, DRV_NAME, sizeof(cap->driver));
	strscpy(cap->card, "MSM8660 VFE front camera", sizeof(cap->card));
	return 0;
}

static int vfe_enum_fmt(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;
	f->pixelformat = V4L2_PIX_FMT_NV12;
	return 0;
}

static int vfe_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	vfe_fill_fmt(&f->fmt.pix);
	return 0;
}

static int vfe_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct vfe *v = video_drvdata(file);

	if (vb2_is_busy(&v->queue))
		return -EBUSY;
	vfe_fill_fmt(&f->fmt.pix);
	return 0;
}

static int vfe_enum_framesizes(struct file *file, void *priv, struct v4l2_frmsizeenum *fs)
{
	if (fs->index || fs->pixel_format != V4L2_PIX_FMT_NV12)
		return -EINVAL;
	fs->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fs->discrete.width = WIDTH;
	fs->discrete.height = HEIGHT;
	return 0;
}

static int vfe_enum_input(struct file *file, void *priv, struct v4l2_input *in)
{
	if (in->index)
		return -EINVAL;
	in->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(in->name, "Front camera", sizeof(in->name));
	return 0;
}

static int vfe_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vfe_s_input(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static const struct v4l2_ioctl_ops vfe_ioctl_ops = {
	.vidioc_querycap = vfe_querycap,
	.vidioc_enum_fmt_vid_cap = vfe_enum_fmt,
	.vidioc_g_fmt_vid_cap = vfe_fmt,
	.vidioc_try_fmt_vid_cap = vfe_fmt,
	.vidioc_s_fmt_vid_cap = vfe_s_fmt,
	.vidioc_enum_framesizes = vfe_enum_framesizes,
	.vidioc_enum_input = vfe_enum_input,
	.vidioc_g_input = vfe_g_input,
	.vidioc_s_input = vfe_s_input,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static const struct v4l2_file_operations vfe_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static int vfe_register_video(struct vfe *v)
{
	struct vb2_queue *q = &v->queue;
	struct video_device *vd = &v->vdev;
	int ret;

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_READ;
	q->drv_priv = v;
	q->buf_struct_size = sizeof(struct vfe_buf);
	q->ops = &vfe_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 2;
	q->lock = &v->lock;
	q->dev = v->dev;
	ret = vb2_queue_init(q);
	if (ret)
		return ret;

	strscpy(vd->name, "msm8660-vfe front camera", sizeof(vd->name));
	vd->fops = &vfe_fops;
	vd->ioctl_ops = &vfe_ioctl_ops;
	vd->release = video_device_release_empty;
	vd->v4l2_dev = &v->v4l2_dev;
	vd->queue = q;
	vd->lock = &v->lock;
	vd->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
	video_set_drvdata(vd, v);
	ret = video_register_device(vd, VFL_TYPE_VIDEO, -1);
	if (!ret)
		dev_info(v->dev, "front camera on %s (NV12 %ux%u)\n", video_device_node_name(vd), WIDTH, HEIGHT);
	return ret;
}

static int vfe_notify_bound(struct v4l2_async_notifier *n, struct v4l2_subdev *sd, struct v4l2_async_connection *asc)
{
	struct vfe *v = container_of(n, struct vfe, notifier);

	v->sensor = sd;
	return vfe_register_video(v);
}

static void vfe_notify_unbind(struct v4l2_async_notifier *n, struct v4l2_subdev *sd, struct v4l2_async_connection *asc)
{
	struct vfe *v = container_of(n, struct vfe, notifier);

	video_unregister_device(&v->vdev);
	v->sensor = NULL;
}

static const struct v4l2_async_notifier_operations vfe_notify_ops = {
	.bound = vfe_notify_bound,
	.unbind = vfe_notify_unbind,
};

static int vfe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_async_connection *asc;
	struct fwnode_handle *ep;
	struct device_node *mmcc;
	struct resource res;
	struct vfe *v;
	int ret;

	v = devm_kzalloc(dev, sizeof(*v), GFP_KERNEL);
	if (!v)
		return -ENOMEM;
	v->dev = dev;
	mutex_init(&v->lock);
	spin_lock_init(&v->slock);
	INIT_LIST_HEAD(&v->pending);
	init_completion(&v->reset_done);

	v->base = devm_platform_ioremap_resource_byname(pdev, "vfe");
	if (IS_ERR(v->base))
		return PTR_ERR(v->base);
	v->csi = devm_platform_ioremap_resource_byname(pdev, "csi");
	if (IS_ERR(v->csi))
		return PTR_ERR(v->csi);
	/* the MMSS clock controller registers belong to the mmcc driver: map them without requesting the region */
	mmcc = of_parse_phandle(dev->of_node, "qcom,mmcc", 0);
	if (!mmcc)
		return dev_err_probe(dev, -EINVAL, "no qcom,mmcc\n");
	ret = of_address_to_resource(mmcc, 0, &res);
	of_node_put(mmcc);
	if (ret)
		return ret;
	v->mmss = devm_ioremap(dev, res.start, resource_size(&res));
	if (!v->mmss)
		return -ENOMEM;

	v->ahb_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(v->ahb_clk))
		return dev_err_probe(dev, PTR_ERR(v->ahb_clk), "no ahb clock\n");
	v->axi_clk = devm_clk_get(dev, "axi");
	if (IS_ERR(v->axi_clk))
		return dev_err_probe(dev, PTR_ERR(v->axi_clk), "no axi clock\n");

	v->irq = platform_get_irq_byname(pdev, "vfe");
	if (v->irq < 0)
		return v->irq;
	ret = devm_request_irq(dev, v->irq, vfe_irq, 0, DRV_NAME, v);
	if (ret)
		return ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	v->scratch = dmam_alloc_coherent(dev, IMAGE_SIZE, &v->scratch_dma, GFP_KERNEL);
	if (!v->scratch)
		return -ENOMEM;

	ret = v4l2_device_register(dev, &v->v4l2_dev);
	if (ret)
		return ret;

	v4l2_async_nf_init(&v->notifier, &v->v4l2_dev);
	ep = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!ep) {
		ret = dev_err_probe(dev, -EINVAL, "no endpoint to the sensor\n");
		goto err_v4l2;
	}
	asc = v4l2_async_nf_add_fwnode_remote(&v->notifier, ep, struct v4l2_async_connection);
	fwnode_handle_put(ep);
	if (IS_ERR(asc)) {
		ret = PTR_ERR(asc);
		goto err_nf;
	}
	v->notifier.ops = &vfe_notify_ops;
	ret = v4l2_async_nf_register(&v->notifier);
	if (ret)
		goto err_nf;

	platform_set_drvdata(pdev, v);
	return 0;

err_nf:
	v4l2_async_nf_cleanup(&v->notifier);
err_v4l2:
	v4l2_device_unregister(&v->v4l2_dev);
	return ret;
}

static void vfe_remove(struct platform_device *pdev)
{
	struct vfe *v = platform_get_drvdata(pdev);

	v4l2_async_nf_unregister(&v->notifier);
	v4l2_async_nf_cleanup(&v->notifier);
	v4l2_device_unregister(&v->v4l2_dev);
}

static const struct of_device_id vfe_of_match[] = {
	{ .compatible = "qcom,msm8660-vfe" },
	{ }
};
MODULE_DEVICE_TABLE(of, vfe_of_match);

static struct platform_driver vfe_driver = {
	.probe = vfe_probe,
	.remove = vfe_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = vfe_of_match,
	},
};
module_platform_driver(vfe_driver);

MODULE_DESCRIPTION("MSM8660 CSI1 + VFE 3.1 camera capture (HP TouchPad front camera)");
MODULE_LICENSE("GPL");
