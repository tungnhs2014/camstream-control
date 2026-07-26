// SPDX-License-Identifier: GPL-2.0-only

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>

#define CAMSTREAM_DRIVER_NAME "camstream-video"

/**
 * struct camstream_video_device - resources owned by one CamStream video node
 * @v4l2_dev: V4L2 core device registered for the module lifetime
 * @video_dev: Dynamically allocated capture node; registration owns release
 * @lock: Serializes V4L2 ioctls for the video node
 *
 * The module allocates this structure before registering either V4L2 object.
 * After successful video-node registration, video_unregister_device() releases
 * @video_dev; the module then unregisters @v4l2_dev and frees this structure.
 */
struct camstream_video_device {
	struct v4l2_device v4l2_dev;
	struct video_device *video_dev;
	struct mutex lock;
};

static struct camstream_video_device *camstream_device;

static int camstream_querycap(struct file *file, void *priv,
			      struct v4l2_capability *cap)
{
	strscpy(cap->driver, CAMSTREAM_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, "CamStream synthetic video", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:camstream-video",
		sizeof(cap->bus_info));

	return 0;
}

static const struct v4l2_file_operations camstream_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = v4l2_fh_release,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops camstream_ioctl_ops = {
	.vidioc_querycap = camstream_querycap,
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
	strscpy(device->v4l2_dev.name, CAMSTREAM_DRIVER_NAME,
		sizeof(device->v4l2_dev.name));

	ret = v4l2_device_register(NULL, &device->v4l2_dev);
	if (ret)
		goto err_destroy_mutex;

	video_dev = video_device_alloc();
	if (!video_dev) {
		ret = -ENOMEM;
		goto err_unregister_v4l2;
	}

	device->video_dev = video_dev;
	strscpy(video_dev->name, CAMSTREAM_DRIVER_NAME,
		sizeof(video_dev->name));
	video_dev->fops = &camstream_fops;
	video_dev->ioctl_ops = &camstream_ioctl_ops;
	video_dev->v4l2_dev = &device->v4l2_dev;
	video_dev->lock = &device->lock;
	video_dev->release = video_device_release;
	video_dev->device_caps = V4L2_CAP_VIDEO_CAPTURE;
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
	video_unregister_device(device->video_dev);
	v4l2_device_unregister(&device->v4l2_dev);
	mutex_destroy(&device->lock);
	kfree(device);
	camstream_device = NULL;
}

module_init(camstream_video_init);
module_exit(camstream_video_exit);

MODULE_AUTHOR("CamStream Control project");
MODULE_DESCRIPTION("CamStream synthetic V4L2 capture driver skeleton");
MODULE_LICENSE("GPL");
