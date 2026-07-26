/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CAMSTREAM_VIDEO_H
#define CAMSTREAM_VIDEO_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>

/**
 * struct camstream_video_device - resources owned by one CamStream video node
 * @v4l2_dev: V4L2 core device registered for the module lifetime
 * @video_dev: Dynamically allocated capture node
 * @lock: Serializes V4L2 ioctls and VB2 queue operations
 * @active_format: Effective single-planar capture format
 * @timeperframe: Fixed capture interval exposed to userspace
 * @vb2_queue: MMAP-only single-planar capture queue
 * @queued_buffers: Buffers currently owned by the synthetic driver
 * @queued_lock: Protects @queued_buffers independently of process context
 *
 * Initialization registers @v4l2_dev, initializes @vb2_queue, and then
 * registers @video_dev. After video-node registration, VB2-aware video-device
 * unregistration releases both the video node and queue. Before registration,
 * error paths release only the resources whose initialization succeeded.
 */
struct camstream_video_device {
	struct v4l2_device v4l2_dev;
	struct video_device *video_dev;
	struct mutex lock;
	struct v4l2_pix_format active_format;
	struct v4l2_fract timeperframe;
	struct vb2_queue vb2_queue;
	struct list_head queued_buffers;
	spinlock_t queued_lock;
};

int camstream_vb2_queue_init(struct camstream_video_device *device);

#endif
