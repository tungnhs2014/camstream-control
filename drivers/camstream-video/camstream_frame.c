// SPDX-License-Identifier: GPL-2.0-only

#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/overflow.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "camstream_video.h"

#define CAMSTREAM_YUYV_BYTES_PER_PIXEL 2U
#define CAMSTREAM_NEUTRAL_CHROMA 128U
#define CAMSTREAM_LUMA_MINIMUM 16U
#define CAMSTREAM_LUMA_RANGE 219U
#define CAMSTREAM_PATTERN_SHIFT_PIXELS 8U

static u64
camstream_frame_interval_ns(const struct camstream_video_device *device)
{
	u64 interval;

	if (WARN_ON_ONCE(device->timeperframe.numerator == 0U ||
			 device->timeperframe.denominator == 0U))
		return jiffies_to_nsecs(1UL);

	interval = div_u64((u64)NSEC_PER_SEC *
			   device->timeperframe.numerator,
			   device->timeperframe.denominator);
	if (interval == 0U)
		return 1U;

	return interval;
}

static unsigned long camstream_delay_until(u64 deadline_ns)
{
	const u64 now_ns = ktime_get_ns();
	u64 remaining_ns;
	unsigned long delay;

	if (deadline_ns <= now_ns)
		return 1UL;

	remaining_ns = deadline_ns - now_ns;
	delay = nsecs_to_jiffies(remaining_ns);
	if (delay >= MAX_JIFFY_OFFSET)
		return MAX_JIFFY_OFFSET;

	/* nsecs_to_jiffies() truncates; delayed work must not run early. */
	if (jiffies_to_nsecs(delay) < remaining_ns)
		++delay;

	return max(delay, 1UL);
}

static bool
camstream_format_is_safe(const struct v4l2_pix_format *format)
{
	u32 packed_bytesperline;
	u32 expected_size;

	if (format->pixelformat != V4L2_PIX_FMT_YUYV ||
	    format->width < 2U || (format->width & 1U) != 0U ||
	    format->height == 0U)
		return false;

	if (check_mul_overflow(format->width,
			       CAMSTREAM_YUYV_BYTES_PER_PIXEL,
			       &packed_bytesperline) ||
	    packed_bytesperline != format->bytesperline)
		return false;

	if (check_mul_overflow(format->bytesperline, format->height,
			       &expected_size) ||
	    expected_size != format->sizeimage)
		return false;

	return true;
}

static u8 camstream_luma(u32 position, u32 width)
{
	return (u8)(CAMSTREAM_LUMA_MINIMUM +
		    div_u64((u64)position * CAMSTREAM_LUMA_RANGE,
			    width - 1U));
}

static void camstream_fill_yuyv(u8 *destination,
				const struct v4l2_pix_format *format,
				u32 sequence)
{
	const u32 shift = ((sequence % format->width) *
			   CAMSTREAM_PATTERN_SHIFT_PIXELS) % format->width;
	u8 *first_row = destination;
	u32 row_index;
	u32 column;

	for (column = 0U; column < format->width; column += 2U) {
		const u32 first = (column + shift) % format->width;
		const u32 second = (column + 1U + shift) % format->width;
		const size_t offset = (size_t)column *
				      CAMSTREAM_YUYV_BYTES_PER_PIXEL;

		first_row[offset] = camstream_luma(first, format->width);
		first_row[offset + 1U] = CAMSTREAM_NEUTRAL_CHROMA;
		first_row[offset + 2U] = camstream_luma(second,
							   format->width);
		first_row[offset + 3U] = CAMSTREAM_NEUTRAL_CHROMA;
	}

	for (row_index = 1U; row_index < format->height; ++row_index)
		memcpy(destination + (size_t)row_index * format->bytesperline,
		       first_row, format->bytesperline);
}

static void
camstream_advance_deadline_locked(struct camstream_video_device *device)
{
	const u64 interval_ns = camstream_frame_interval_ns(device);
	const u64 now_ns = ktime_get_ns();
	u64 deadline_ns;
	u64 periods;
	u64 advance_ns;

	if (check_add_overflow(device->next_frame_deadline_ns, interval_ns,
			       &deadline_ns)) {
		device->next_frame_deadline_ns = now_ns + interval_ns;
		return;
	}

	if (deadline_ns <= now_ns) {
		periods = div64_u64(now_ns - deadline_ns, interval_ns) + 1U;
		if (check_mul_overflow(periods, interval_ns, &advance_ns) ||
		    check_add_overflow(deadline_ns, advance_ns, &deadline_ns))
			deadline_ns = now_ns + interval_ns;
	}

	device->next_frame_deadline_ns = deadline_ns;
}

static void
camstream_schedule_next_locked(struct camstream_video_device *device)
{
	if (device->streaming && !list_empty(&device->queued_buffers))
		mod_delayed_work(system_wq, &device->frame_work,
				 camstream_delay_until(
					 device->next_frame_deadline_ns));
}

static struct camstream_buffer *
camstream_take_buffer(struct camstream_video_device *device, u32 *sequence)
{
	struct camstream_buffer *buffer = NULL;
	unsigned long flags;

	spin_lock_irqsave(&device->queued_lock, flags);
	if (device->streaming && !list_empty(&device->queued_buffers)) {
		buffer = list_first_entry(&device->queued_buffers,
					  struct camstream_buffer, list);
		list_del_init(&buffer->list);
		*sequence = device->sequence;
	}
	spin_unlock_irqrestore(&device->queued_lock, flags);

	return buffer;
}

static bool
camstream_commit_frame(struct camstream_video_device *device, u32 sequence)
{
	unsigned long flags;
	bool commit = false;

	spin_lock_irqsave(&device->queued_lock, flags);
	if (device->streaming && device->sequence == sequence) {
		++device->sequence;
		commit = true;
	}
	spin_unlock_irqrestore(&device->queued_lock, flags);

	return commit;
}

static void camstream_rearm_producer(struct camstream_video_device *device,
				     bool advance_deadline)
{
	unsigned long flags;

	spin_lock_irqsave(&device->queued_lock, flags);
	if (advance_deadline)
		camstream_advance_deadline_locked(device);
	camstream_schedule_next_locked(device);
	spin_unlock_irqrestore(&device->queued_lock, flags);
}

/**
 * camstream_frame_work - generate at most one synthetic capture frame
 * @work: Work item embedded in the owning CamStream device
 *
 * The worker removes one driver-owned buffer under the queue spinlock, fills
 * it outside the lock, and returns it exactly once. STREAMOFF disables future
 * commits and waits synchronously for an in-flight invocation to finish.
 */
static void camstream_frame_work(struct work_struct *work)
{
	struct camstream_video_device *device =
		container_of(to_delayed_work(work),
			     struct camstream_video_device, frame_work);
	struct camstream_buffer *buffer;
	struct vb2_buffer *vb;
	void *plane;
	u32 sequence = 0U;
	bool valid;

	buffer = camstream_take_buffer(device, &sequence);
	if (!buffer)
		return;

	vb = &buffer->vb.vb2_buf;
	plane = vb2_plane_vaddr(vb, 0U);
	valid = plane && vb2_plane_size(vb, 0U) >=
		 device->active_format.sizeimage &&
		 camstream_format_is_safe(&device->active_format);

	if (valid)
		camstream_fill_yuyv(plane, &device->active_format, sequence);

	if (valid && camstream_commit_frame(device, sequence)) {
		vb2_set_plane_payload(vb, 0U,
				      device->active_format.sizeimage);
		buffer->vb.field = V4L2_FIELD_NONE;
		buffer->vb.sequence = sequence;
		vb->timestamp = ktime_get_ns();
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	} else {
		vb2_set_plane_payload(vb, 0U, 0U);
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
	}

	camstream_rearm_producer(device, true);
}

/**
 * camstream_frame_init - initialize one delayed-work producer
 * @device: Device that owns the work item for its full lifetime
 */
void camstream_frame_init(struct camstream_video_device *device)
{
	INIT_DELAYED_WORK(&device->frame_work, camstream_frame_work);
}

/**
 * camstream_frame_start - begin an asynchronous capture session
 * @device: Device with initialized work and queued-buffer state
 *
 * Sequence numbering restarts at zero. The first frame is scheduled after one
 * interval; no frame is generated synchronously under the VB2 queue mutex.
 */
void camstream_frame_start(struct camstream_video_device *device)
{
	unsigned long flags;

	spin_lock_irqsave(&device->queued_lock, flags);
	device->sequence = 0U;
	device->next_frame_deadline_ns =
		ktime_get_ns() + camstream_frame_interval_ns(device);
	device->streaming = true;
	camstream_schedule_next_locked(device);
	spin_unlock_irqrestore(&device->queued_lock, flags);
}

/**
 * camstream_frame_notify_buffer - ensure a queued buffer reaches the producer
 * @device: Device that now owns at least one queued buffer
 */
void camstream_frame_notify_buffer(struct camstream_video_device *device)
{
	camstream_rearm_producer(device, false);
}

/**
 * camstream_frame_stop - disable and synchronously drain producer work
 * @device: Device whose stream is stopping
 *
 * Marking the stream inactive while holding the scheduling lock prevents any
 * worker or QBUF notification from re-arming after cancellation begins.
 */
void camstream_frame_stop(struct camstream_video_device *device)
{
	unsigned long flags;

	spin_lock_irqsave(&device->queued_lock, flags);
	device->streaming = false;
	spin_unlock_irqrestore(&device->queued_lock, flags);

	cancel_delayed_work_sync(&device->frame_work);
}
