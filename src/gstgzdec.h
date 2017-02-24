/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2017 Stephan Hesse <<user@hostname.org>>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_GZDEC_H__
#define __GST_GZDEC_H__

#include <gst/gst.h>

#define USE_GSTATIC_REC_MUTEX FALSE // WARNING: GStaticRecMutex is deprecated !!!
									// Meanwhile this is what is actually expected by the GStreamer 0.10.x 
									// GstTask API. The two implementations might however compatible.
#define USE_GSTREAMER_1_DOT_0_API TRUE

#if USE_GSTREAMER_1_DOT_0_API

  void buffer_set_data (GstBuffer* buf, gpointer data, gsize size) {
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
      GST_ERROR ("Error mapping buffer: %" GST_PTR_FORMAT, buf);
      return;
    }

    if (size > map.size) {
      GST_WARNING ("buffer_set_data: Mapped memory is smaller than data!");
    }

    memcpy(map.data, data, 
      size >= map.size ? map.size : size);

    gst_buffer_unmap (buf, &map);
  }

	#define CREATE_TASK(func, data) gst_task_new(func, data, NULL) // just monkey-patch this
	#define BUFFER_SET_DATA(buf, data, size) buffer_set_data(buf, data, size)
	#define BUFFER_ALLOC(size) gst_buffer_new_allocate(NULL, size, NULL)
  #define GST_BUFFER_SIZE(buf) gst_buffer_get_size(buf)

#else // fallback to default: GStreamer 0.10.x API

	#define TASK_CREATE(func, data) gst_task_create(func, data)
	#define BUFFER_SET_DATA(buf, data, size) gst_buffer_set_data(buf, data, size)
	#define BUFFER_ALLOC(size) gst_buffer_new_and_alloc(size)

#endif

#if USE_GSTATIC_REC_MUTEX
	typedef GStaticRecMutex MUTEX;
	#define REC_MUTEX_LOCK g_static_rec_mutex_lock
  #define REC_MUTEX_UNLOCK g_static_rec_mutex_unlock
	#define REC_MUTEX_INIT g_static_rec_mutex_init
#else
	typedef GRecMutex MUTEX;
	#define REC_MUTEX_LOCK g_rec_mutex_lock
  #define REC_MUTEX_UNLOCK g_rec_mutex_unlock
	#define REC_MUTEX_INIT g_rec_mutex_init
#endif

#define INPUT_TASK_LOCK(element) REC_MUTEX_LOCK(&element->input_task_mutex)
#define INPUT_TASK_UNLOCK(element) REC_MUTEX_UNLOCK(&element->input_task_mutex)
#define SRCPAD_TASK_LOCK(element) REC_MUTEX_LOCK(GST_PAD_GET_STREAM_LOCK(&element->srcpad))
#define SRCPAD_TASK_UNLOCK(element) REC_MUTEX_UNLOCK(GST_PAD_GET_STREAM_LOCK(&element->srcpad))

#define INPUT_QUEUE_WAIT(element) g_cond_wait(&element->input_queue_run_cond, &filter->input_queue_mutex)
#define INPUT_QUEUE_SIGNAL(element) g_cond_signal(&element->input_queue_run_cond)

#define OUTPUT_QUEUE_WAIT(element) g_cond_wait(&element->output_queue_run_cond, &filter->output_queue_mutex)
#define OUTPUT_QUEUE_SIGNAL(element) g_cond_signal(&element->output_queue_run_cond)

#define INPUT_QUEUE_LOCK(element) g_mutex_lock(&element->input_queue_mutex)
#define INPUT_QUEUE_UNLOCK(element) g_mutex_unlock(&element->input_queue_mutex)

#define OUTPUT_QUEUE_LOCK(element) g_mutex_lock(&element->output_queue_mutex)
#define OUTPUT_QUEUE_UNLOCK(element) g_mutex_unlock(&element->output_queue_mutex)

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_GZDEC \
  (gst_gz_dec_get_type())
#define GST_GZDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GZDEC,GstGzDec))
#define GST_GZDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GZDEC,GstGzDecClass))
#define GST_IS_GZDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GZDEC))
#define GST_IS_GZDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GZDEC))

typedef struct _GstGzDec      GstGzDec;
typedef struct _GstGzDecClass GstGzDecClass;

struct _GstGzDec
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GQueue *input_queue;
  GQueue *output_queue;

  GstTask *input_task;
  MUTEX input_task_mutex;

  GCond output_queue_run_cond;
  GCond input_queue_run_cond;

  GMutex input_queue_mutex;
  GMutex output_queue_mutex;

  GstEvent* pending_eos;

  gboolean srcpad_task_resume;
  gboolean input_task_resume;

  gboolean use_worker;
  gboolean use_async_push;
  gboolean enabled;
  gboolean error;
  gboolean eos;
  guint bytes;
};

struct _GstGzDecClass 
{
  GstElementClass parent_class;
};

GType gst_gz_dec_get_type (void);

G_END_DECLS

#endif /* __GST_GZDEC_H__ */
