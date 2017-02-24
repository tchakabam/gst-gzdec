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
 * FIXME:Describe gzdec here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! gzdec ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#include <string.h>
//#include <stdio.h>
#include <stdlib.h>

#include <zlib.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstgzdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_gz_dec_debug);
#define GST_CAT_DEFAULT gst_gz_dec_debug

#include "gstgzdec_priv.h"

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_ENABLED,
  PROP_BYTES,
  PROP_USE_WORKER
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

  g_object_class_install_property (gobject_class, PROP_BYTES,
      g_param_spec_uint ("bytes", "Bytes", "Count of processed bytes",
          0, G_MAXUINT,0, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_ENABLED,
      g_param_spec_boolean ("enabled", "Enabled", "Enable decoding",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ENABLED,
      g_param_spec_boolean ("use-worker", "Use worker", "Use worker for decoding",
          TRUE, G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "Gzip decoder",
    "FIXME:Generic",
    "FIXME:Generic Template Element",
    "Stephan Hesse <disparat@gmail.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gstelement_class->change_state = gst_gz_dec_change_state;
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

  filter->use_worker = TRUE; // FIXME: not used!
  filter->use_async_push = TRUE;

  // FIXME: these flags need a real purpose now
  filter->error = FALSE;
  filter->enabled = TRUE;
  filter->bytes = 0;

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
}

static void
gst_gz_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGzDec *filter = GST_GZDEC (object);

  switch (prop_id) {

    case PROP_USE_WORKER:
      filter->use_worker = g_value_get_boolean (value);
      break;
    case PROP_ENABLED:
      filter->enabled = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gz_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGzDec *filter = GST_GZDEC (object);

  switch (prop_id) {
    case PROP_USE_WORKER:
      g_value_set_boolean (value, filter->use_worker);
      break;
    case PROP_ENABLED:
      g_value_set_boolean (value, filter->enabled);
      break;
    case PROP_BYTES:
      g_value_set_uint (value, filter->bytes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_gz_dec_change_state (GstElement *element, GstStateChange transition)
{
  GstGzDec *filter = GST_GZDEC (element);
  gboolean use_async_push = filter->use_async_push;

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
    filter->pending_eos = NULL;
    GST_OBJECT_UNLOCK(filter);
    // Pre-warm our input processing worker
    gst_task_pause(filter->input_task);
    break;
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    // Pre-process input data to have prerolled data
    // on output when we go to play
    gst_task_start(filter->input_task);

    // WEIRD!!!! we need this to startup, but it should only be necessary in PLAYING state, no?
    if (use_async_push) {
      GST_INFO_OBJECT (filter, "Scheduling async push (starting srcpad task)");
      gst_pad_start_task (filter->srcpad, srcpad_task_func, filter, NULL); 
    }
    break;
  case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    // We're playing, start srcpad streaming task!
    if (use_async_push) {
      GST_INFO_OBJECT (filter, "Scheduling async push (starting srcpad task)");
      gst_pad_start_task (filter->srcpad, srcpad_task_func, filter, NULL); 
    }
    break;
  case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    // Pausing srcpad streaming task (this will be syncroneous!)
    if (use_async_push) {
      GST_INFO_OBJECT (filter, "Setting srcpad task to paused");
      // this will actually 
      gst_pad_pause_task (filter->srcpad);
      OUTPUT_QUEUE_LOCK(filter);
      OUTPUT_QUEUE_SIGNAL(filter);
      OUTPUT_QUEUE_UNLOCK(filter);
    }
    break;
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    // Pause input processing worker
    gst_task_pause(filter->input_task);
    INPUT_QUEUE_LOCK(filter);
    INPUT_QUEUE_SIGNAL(filter);
    INPUT_QUEUE_UNLOCK(filter);
    break;
  case GST_STATE_CHANGE_READY_TO_NULL:
    // This will actually join all the task threads
    // (but the task are re-usable)
    if (filter->use_async_push) {
      GST_INFO_OBJECT (filter, "Setting srcpad task to paused");
      // this will actually 
      gst_pad_stop_task (filter->srcpad);
      OUTPUT_QUEUE_SIGNAL(filter); 
    }
    gst_task_join(filter->input_task);
    INPUT_QUEUE_LOCK(filter);
    INPUT_QUEUE_SIGNAL(filter);
    INPUT_QUEUE_UNLOCK(filter);
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
  case GST_EVENT_EOS:
    //ret = gst_pad_event_default (pad, parent, event);
    GST_OBJECT_LOCK(filter);
    filter->pending_eos = event;
    GST_OBJECT_UNLOCK(filter);
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

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_gz_dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstGzDec *filter;

  filter = GST_GZDEC (parent);

  GST_TRACE_OBJECT(filter, "Entering chain function");

  // once task is paused sooner or later
  // we should be able to take the worker lock
  GST_TRACE_OBJECT(filter, "Appending input buffer of %d bytes", (int) GST_BUFFER_SIZE(buf));
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
