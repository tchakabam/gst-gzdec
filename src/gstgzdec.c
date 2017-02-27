/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2017 Stephan Hesse <<disparat@gmail.com>>
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

/**
 * SECTION:element-gzdec
 *
 * Generic decoder element that will unzip things by default. 
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=test/test.txt.zip ! gzdec ! filesink location=test/test.out.txt
 * ]|
 * </refsect2>
 */

#include <string.h>
#include <stdlib.h>

#include <zlib.h>
#include <bzlib.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstgzdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_gz_dec_debug);
#define GST_CAT_DEFAULT gst_gz_dec_debug

#include "gstgzdec_bzipdecstream.h"
#include "gstgzdec_zipdecstream.h"
#include "gstgzdec_priv.h"

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_gz_dec_parent_class parent_class
G_DEFINE_TYPE (GstGzDec, gst_gz_dec, GST_TYPE_ELEMENT);

static void gst_gz_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gz_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gz_dec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_gz_dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

static GstStateChangeReturn
gst_gz_dec_change_state (GstElement *element, GstStateChange transition);

/* GObject vmethod implementations */

/* initialize the gzdec's class */
static void
gst_gz_dec_class_init (GstGzDecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_gz_dec_set_property;
  gobject_class->get_property = gst_gz_dec_get_property;

  gst_element_class_set_details_simple(gstelement_class,
    "Gzip decoder",
    "Decoder",
    "Decode compressed zip data",
    "Stephan Hesse <disparat@gmail.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gstelement_class->change_state = gst_gz_dec_change_state;
}

// Just an adapter function resulting from the abstraction
static void
gst_gz_stream_writer_func (gpointer user_data, gpointer data, gsize bytes) {
  output_queue_append_data (user_data, data, bytes);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_gz_dec_init (GstGzDec * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_gz_dec_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_gz_dec_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->pending_eos = NULL;

  filter->input_task_resume = FALSE;
  filter->srcpad_task_resume = FALSE;

  // Queues
  filter->input_queue = g_queue_new();
  filter->output_queue = g_queue_new();

  // Init locks
  g_mutex_init(&filter->input_queue_mutex);
  g_mutex_init(&filter->output_queue_mutex);
  REC_MUTEX_INIT(&filter->input_task_mutex);

  g_cond_init(&filter->input_queue_run_cond);
  g_cond_init(&filter->output_queue_run_cond);

  // Create input task
  filter->input_task = CREATE_TASK(input_task_func, filter);
  gst_task_set_lock(filter->input_task, &filter->input_task_mutex);

  GST_INFO_OBJECT(filter, "Done initializing element");
}

static void
gst_gz_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gz_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_gz_dec_change_state (GstElement *element, GstStateChange transition)
{
  GstGzDec *filter = GST_GZDEC (element);

  GstState current = GST_STATE_TRANSITION_CURRENT(transition);
  GstState next = GST_STATE_TRANSITION_NEXT(transition);

  GST_INFO_OBJECT (element, "Transition from %s to %s", 
    gst_element_state_get_name (current), 
    gst_element_state_get_name (next));

  switch(transition) {
  case GST_STATE_CHANGE_NULL_TO_READY:
    // Initialize things
    GST_OBJECT_LOCK(filter);
    filter->eos = FALSE;
    GST_OBJECT_UNLOCK(filter);
    // Pre-warm our input processing worker
    gst_task_pause(filter->input_task);
    break;
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    // Pre-process input data to have prerolled data
    // on output when we go to play
    input_task_start(filter);
    // Weird! we need this to startup, but it should only be necessary in PLAYING state, no?
    srcpad_task_start(filter);
    break;
  case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    // We're playing, start srcpad streaming task!
    srcpad_task_start(filter);
    break;
  case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    // this will be syncroneous
    srcpad_task_pause(filter);
    break;
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    // We might have never reached playing state, in this
    // case we want to pause the srcpad task from here!
    // Pausing srcpad streaming task (this will be syncroneous!)
    srcpad_task_pause(filter);
    // Pause input processing worker (blocking/sync)
    input_task_pause(filter);
    break;
  case GST_STATE_CHANGE_READY_TO_NULL:
    // This will actually join all the task threads
    // (but the tasks are re-usable)
    srcpad_task_join(filter);
    input_task_join(filter);
    break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_gz_dec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstGzDec *filter = GST_GZDEC (parent);
  gboolean ret;

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_STREAM_START:

    filter->stream_start_fill
      = filter->stream_start[0] 
      = filter->stream_start[1] = 0;
    filter->eos = FALSE;

    ret = gst_pad_event_default (pad, parent, event);
    break;
  case GST_EVENT_EOS:
    GST_OBJECT_LOCK(filter);
    filter->pending_eos = event;
    GST_OBJECT_UNLOCK(filter);
    // the queue might be waiting at this point
    // even if there is no data to process
    // we want it to pick up this EOS signal
    input_queue_signal_resume (filter);

    ret = TRUE;
    break;
  case GST_EVENT_CAPS:
  {
    GstCaps * caps;
    gst_event_parse_caps (event, &caps);

    ret = gst_pad_event_default (pad, parent, event);
    break;
  }
  default:
    ret = gst_pad_event_default (pad, parent, event);
    break;
  }
  return ret;
}

static void
gst_gz_dec_try_feed_stream_start(GstGzDec* filter, GstBuffer* buf)
{
  GstMapInfo map;

  if (filter->stream_start_fill == sizeof(filter->stream_start)) {
    return;
  }

  if(!gst_buffer_map(buf, &map, GST_MAP_READ)) {
    GST_ERROR("Error mapping for read access");
    return;
  }

  for (;filter->stream_start_fill < sizeof(filter->stream_start) 
    && filter->stream_start_fill < map.size; 
    filter->stream_start_fill++) {
    filter->stream_start[filter->stream_start_fill] = map.data[filter->stream_start_fill];
  }

  GST_DEBUG ("Got stream starting chars: %x %x", filter->stream_start[0], filter->stream_start[1]);

  gst_buffer_unmap(buf, &map);

  g_assert(filter->decoder == NULL);

  if (filter->stream_start_fill == sizeof(filter->stream_start)) {

    GST_INFO ("Setup decoder");

    setup_decoder(filter, gst_gz_stream_writer_func);
  }
} 

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_gz_dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstGzDec *filter;

  filter = GST_GZDEC (parent);

  GST_TRACE_OBJECT(filter, "Entering chain function: %" GST_PTR_FORMAT, buf);

  if (!filter->decoder) {
    gst_gz_dec_try_feed_stream_start(filter, buf);
  }

  g_assert(filter->decoder != NULL);

  // once task is paused sooner or later
  // we should be able to take the worker lock
  GST_TRACE_OBJECT(filter, "Appending input buffer of %d bytes", (int) BUFFER_SIZE(buf));
  input_queue_append_buffer(filter, buf);
  // To avoid any races between the task feeding us here
  // and the worker thread let's set the state to started
  // and then only release the lock.
  GST_TRACE_OBJECT(filter, "Leaving chain function");

  /* just push out the incoming buffer without touching it */
  return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
gzdec_init (GstPlugin * gzdec)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template gzdec' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_gz_dec_debug, "gzdec",
      0, "Template gzdec");

  return gst_element_register (gzdec, "gzdec", GST_RANK_NONE,
      GST_TYPE_GZDEC);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstgzdec"
#endif

/* gstreamer looks for this structure to register gzdecs
 *
 * exchange the string 'Template gzdec' with your gzdec description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    gzdec,
    "Template gzdec",
    gzdec_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
