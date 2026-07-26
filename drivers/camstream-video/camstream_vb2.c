// SPDX-License-Identifier: GPL-2.0-only

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include <media/videobuf2-vmalloc.h>

#include "camstream_video.h"

/**
 * struct camstream_buffer - VB2 buffer with driver queue membership
 * @vb: V4L2/VB2-owned buffer; must be the first member
 * @list: Link used only while the buffer is owned by the driver
 */
struct camstream_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

static int camstream_queue_setup(struct vb2_queue *queue,
				 unsigned int *num_buffers,
				 unsigned int *num_planes,
				 unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct camstream_video_device *device = vb2_get_drv_priv(queue);
	const unsigned int required_size = device->active_format.sizeimage;

	if (*num_planes != 0U) {
		if (*num_planes != 1U || sizes[0] < required_size)
			return -EINVAL;

		return 0;
	}

	*num_planes = 1U;
	sizes[0] = required_size;

	return 0;
}

static int camstream_buf_prepare(struct vb2_buffer *buffer)
{
	struct camstream_video_device *device = vb2_get_drv_priv(buffer->vb2_queue);

	if (vb2_plane_size(buffer, 0U) < device->active_format.sizeimage)
		return -EINVAL;

	/* Stage 6C.3 allocates buffers but intentionally produces no payload. */
	vb2_set_plane_payload(buffer, 0U, 0U);

	return 0;
}

static void camstream_buf_queue(struct vb2_buffer *vb)
{
	struct camstream_video_device *device = vb2_get_drv_priv(vb->vb2_queue);
	struct camstream_buffer *buffer =
		container_of(to_vb2_v4l2_buffer(vb), struct camstream_buffer, vb);
	unsigned long flags;

	spin_lock_irqsave(&device->queued_lock, flags);
	list_add_tail(&buffer->list, &device->queued_buffers);
	spin_unlock_irqrestore(&device->queued_lock, flags);
}

static int camstream_start_streaming(struct vb2_queue *queue,
				     unsigned int count)
{
	/* Stage 6C.4 will start frame production; 6C.3 only changes state. */
	return 0;
}

/**
 * camstream_stop_streaming - cancel every buffer owned by the driver
 * @queue: VB2 capture queue being stopped
 *
 * A buffer is removed from the protected driver list before ownership is
 * returned to VB2. The buffer is never accessed after vb2_buffer_done().
 */
static void camstream_stop_streaming(struct vb2_queue *queue)
{
	struct camstream_video_device *device = vb2_get_drv_priv(queue);
	struct camstream_buffer *buffer;
	unsigned long flags;

	for (;;) {
		spin_lock_irqsave(&device->queued_lock, flags);
		if (list_empty(&device->queued_buffers)) {
			spin_unlock_irqrestore(&device->queued_lock, flags);
			break;
		}

		buffer = list_first_entry(&device->queued_buffers,
					  struct camstream_buffer, list);
		list_del_init(&buffer->list);
		spin_unlock_irqrestore(&device->queued_lock, flags);

		vb2_buffer_done(&buffer->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops camstream_vb2_ops = {
	.queue_setup = camstream_queue_setup,
	.buf_prepare = camstream_buf_prepare,
	.buf_queue = camstream_buf_queue,
	.start_streaming = camstream_start_streaming,
	.stop_streaming = camstream_stop_streaming,
};

/**
 * camstream_vb2_queue_init - initialize the device's MMAP capture queue
 * @device: Fully allocated device with mutex, format, list, and spinlock set
 *
 * The caller owns queue cleanup after a successful return and before video
 * registration. Once the video node is registered, cleanup transfers to the
 * VB2-aware video-device release path.
 *
 * Return: 0 on success, or a negative errno from vb2_queue_init().
 */
int camstream_vb2_queue_init(struct camstream_video_device *device)
{
	struct vb2_queue *queue = &device->vb2_queue;

	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue->io_modes = VB2_MMAP;
	queue->dev = device->v4l2_dev.dev;
	queue->lock = &device->lock;
	queue->ops = &camstream_vb2_ops;
	queue->mem_ops = &vb2_vmalloc_memops;
	queue->drv_priv = device;
	queue->buf_struct_size = sizeof(struct camstream_buffer);
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	queue->min_queued_buffers = 1U;

	return vb2_queue_init(queue);
}
