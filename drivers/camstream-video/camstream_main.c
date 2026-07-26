// SPDX-License-Identifier: GPL-2.0-only

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>

#include "camstream_video.h"

#define CAMSTREAM_DRIVER_NAME "camstream-video"
#define CAMSTREAM_WIDTH 640U
#define CAMSTREAM_HEIGHT 480U
#define CAMSTREAM_BYTES_PER_PIXEL 2U
#define CAMSTREAM_BYTES_PER_LINE \
	(CAMSTREAM_WIDTH * CAMSTREAM_BYTES_PER_PIXEL)
#define CAMSTREAM_SIZE_IMAGE (CAMSTREAM_BYTES_PER_LINE * CAMSTREAM_HEIGHT)
#define CAMSTREAM_FRAME_RATE 30U

static struct camstream_video_device *camstream_device;

/**
 * camstream_fill_format - normalize to the single supported capture format
 * @pix: Pixel-format structure to replace with the effective format
 *
 * Stage 6C.2 supports packed YUYV at 640x480 only. This helper is the common
 * contract used for initialization and all format-negotiation callbacks.
 */
static void camstream_fill_format(struct v4l2_pix_format *pix)
{
	memset(pix, 0, sizeof(*pix));
	pix->width = CAMSTREAM_WIDTH;
	pix->height = CAMSTREAM_HEIGHT;
	pix->pixelformat = V4L2_PIX_FMT_YUYV;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = CAMSTREAM_BYTES_PER_LINE;
	pix->sizeimage = CAMSTREAM_SIZE_IMAGE;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix->quantization = V4L2_QUANTIZATION_DEFAULT;
	pix->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int camstream_querycap(struct file *file, void *priv,
			      struct v4l2_capability *cap)
{
	strscpy(cap->driver, CAMSTREAM_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, "CamStream synthetic video", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:camstream-video",
		sizeof(cap->bus_info));

	return 0;
}

static int camstream_enum_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_fmtdesc *format)
{
	if (format->type != V4L2_BUF_TYPE_VIDEO_CAPTURE || format->index != 0)
		return -EINVAL;

	format->pixelformat = V4L2_PIX_FMT_YUYV;
	strscpy(format->description, "YUYV 4:2:2",
		sizeof(format->description));

	return 0;
}

static int camstream_enum_framesizes(struct file *file, void *priv,
				     struct v4l2_frmsizeenum *size)
{
	if (size->index != 0 || size->pixel_format != V4L2_PIX_FMT_YUYV)
		return -EINVAL;

	size->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	size->discrete.width = CAMSTREAM_WIDTH;
	size->discrete.height = CAMSTREAM_HEIGHT;

	return 0;
}

static int camstream_enum_frameintervals(struct file *file, void *priv,
					 struct v4l2_frmivalenum *interval)
{
	if (interval->index != 0 ||
	    interval->pixel_format != V4L2_PIX_FMT_YUYV ||
	    interval->width != CAMSTREAM_WIDTH ||
	    interval->height != CAMSTREAM_HEIGHT)
		return -EINVAL;

	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	interval->discrete.numerator = 1;
	interval->discrete.denominator = CAMSTREAM_FRAME_RATE;

	return 0;
}

static int camstream_try_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_format *format)
{
	if (format->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	camstream_fill_format(&format->fmt.pix);

	return 0;
}

static int camstream_g_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *format)
{
	struct camstream_video_device *device = video_drvdata(file);

	if (format->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	format->fmt.pix = device->active_format;

	return 0;
}

static int camstream_s_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *format)
{
	struct camstream_video_device *device = video_drvdata(file);
	int ret;

	if (vb2_is_busy(&device->vb2_queue))
		return -EBUSY;

	ret = camstream_try_fmt_vid_cap(file, priv, format);
	if (ret)
		return ret;

	device->active_format = format->fmt.pix;

	return 0;
}

static int camstream_fill_streamparm(struct file *file,
				     struct v4l2_streamparm *parm)
{
	struct camstream_video_device *device = video_drvdata(file);

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(&parm->parm.capture, 0, sizeof(parm->parm.capture));
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe = device->timeperframe;

	return 0;
}

static int camstream_g_parm(struct file *file, void *priv,
			    struct v4l2_streamparm *parm)
{
	return camstream_fill_streamparm(file, parm);
}

static int camstream_s_parm(struct file *file, void *priv,
			    struct v4l2_streamparm *parm)
{
	return camstream_fill_streamparm(file, parm);
}

static const struct v4l2_file_operations camstream_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops camstream_ioctl_ops = {
	.vidioc_querycap = camstream_querycap,
	.vidioc_enum_fmt_vid_cap = camstream_enum_fmt_vid_cap,
	.vidioc_enum_framesizes = camstream_enum_framesizes,
	.vidioc_enum_frameintervals = camstream_enum_frameintervals,
	.vidioc_try_fmt_vid_cap = camstream_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = camstream_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = camstream_s_fmt_vid_cap,
	.vidioc_g_parm = camstream_g_parm,
	.vidioc_s_parm = camstream_s_parm,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static int __init camstream_video_init(void)
{
	struct camstream_video_device *device;
	struct video_device *video_dev;
	int ret;

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		return -ENOMEM;

	mutex_init(&device->lock);
	INIT_LIST_HEAD(&device->queued_buffers);
	spin_lock_init(&device->queued_lock);
	camstream_fill_format(&device->active_format);
	device->timeperframe.numerator = 1;
	device->timeperframe.denominator = CAMSTREAM_FRAME_RATE;
	strscpy(device->v4l2_dev.name, CAMSTREAM_DRIVER_NAME,
		sizeof(device->v4l2_dev.name));

	ret = v4l2_device_register(NULL, &device->v4l2_dev);
	if (ret)
		goto err_destroy_mutex;

	ret = camstream_vb2_queue_init(device);
	if (ret)
		goto err_unregister_v4l2;

	video_dev = video_device_alloc();
	if (!video_dev) {
		ret = -ENOMEM;
		goto err_release_queue;
	}

	device->video_dev = video_dev;
	strscpy(video_dev->name, CAMSTREAM_DRIVER_NAME,
		sizeof(video_dev->name));
	video_dev->fops = &camstream_fops;
	video_dev->ioctl_ops = &camstream_ioctl_ops;
	video_dev->v4l2_dev = &device->v4l2_dev;
	video_dev->queue = &device->vb2_queue;
	video_dev->lock = &device->lock;
	video_dev->release = video_device_release;
	video_dev->device_caps = V4L2_CAP_VIDEO_CAPTURE |
				 V4L2_CAP_STREAMING;
	video_dev->vfl_dir = VFL_DIR_RX;
	video_set_drvdata(video_dev, device);

	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_release_video;

	camstream_device = device;
	v4l2_info(&device->v4l2_dev, "registered as %s\n",
		  video_device_node_name(video_dev));

	return 0;

err_release_video:
	video_device_release(video_dev);
err_release_queue:
	vb2_queue_release(&device->vb2_queue);
err_unregister_v4l2:
	v4l2_device_unregister(&device->v4l2_dev);
err_destroy_mutex:
	mutex_destroy(&device->lock);
	kfree(device);

	return ret;
}

static void __exit camstream_video_exit(void)
{
	struct camstream_video_device *device = camstream_device;

	if (!device)
		return;

	v4l2_info(&device->v4l2_dev, "unregistering %s\n",
		  video_device_node_name(device->video_dev));
	vb2_video_unregister_device(device->video_dev);
	v4l2_device_unregister(&device->v4l2_dev);
	mutex_destroy(&device->lock);
	kfree(device);
	camstream_device = NULL;
}

module_init(camstream_video_init);
module_exit(camstream_video_exit);

MODULE_AUTHOR("CamStream Control project");
MODULE_DESCRIPTION("CamStream synthetic V4L2 capture driver");
MODULE_LICENSE("GPL");
