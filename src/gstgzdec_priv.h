#pragma once

// Mutex convenience macros

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

// Decoder implementation adapters. This might come in handy if one would like to switch between implementations
// for the same format at compile time.

// Gzip
#define CREATE_ZIP_DECODER(element, writer_func) zipdec_stream_new(element, writer_func)
#define ZIP_DECODER_DECODE zipdec_stream_digest_buffer
// Bzip
#define CREATE_BZIP_DECODER(element, writer_func) bzipdec_stream_new(element, writer_func)
#define BZIP_DECODER_DECODE bzipdec_stream_digest_buffer

static GstBuffer* input_queue_pop_buffer (GstGzDec *filter);
static void srcpad_task_func(gpointer user_data);
static void output_queue_append_data (GstGzDec *filter, gpointer data, gsize bytes);
static void setup_decoder (GstGzDec* filter, void* stream_writer_func);

// Just an adapter function resulting from the abstraction
static void
stream_writer_func (gpointer user_data, gpointer data, gsize bytes) {
	output_queue_append_data (user_data, data, bytes);
}

static void
try_feed_stream_start(GstGzDec* filter, GstBuffer* buf)
{
	GstMapInfo map;
	guchar *data;
	guint size;

	if (G_LIKELY(filter->stream_start_fill == sizeof(filter->stream_start))) {
		return;
	}

#ifdef USE_GSTREAMER_1_DOT_0_API
	if(!gst_buffer_map(buf, &map, GST_MAP_READ)) {
		GST_ERROR("Error mapping for read access");
		return;
	}
	data = map.data;
	size = map.size;
#endif

	for (; filter->stream_start_fill < sizeof(filter->stream_start)
	     && filter->stream_start_fill < size;
	     filter->stream_start_fill++) {
		filter->stream_start[filter->stream_start_fill] = data[filter->stream_start_fill];
	}

	GST_DEBUG ("Got stream starting chars: %x %x", filter->stream_start[0], filter->stream_start[1]);

#ifdef USE_GSTREAMER_1_DOT_0_API
	gst_buffer_unmap(buf, &map);
#endif

	g_assert(filter->decoder == NULL);

	if (filter->stream_start_fill == sizeof(filter->stream_start)) {

		GST_INFO ("Setup decoder");

		setup_decoder(filter, stream_writer_func);
	}
}

/*
   We could have used a typefind element to figure this out as well, and potentially
   "cover more cases" but again we chose solution which for our concrete use-case
   necessites less code. It is also easily extensible to peek in further or test other
   stream types. Substantially it works like a typefind less the generic re-usability across
   other elements for it. Using existing typefind functions for these formats would have
   been possible but in this case not offering any substantial advantage
   (except if one would like to do something with the caps, which we dont).
 */
static gboolean stream_is_bzip(GstGzDec* filter) {
	return filter->stream_start[0] == 0x42
	       && filter->stream_start[1] == 0x5a;
}

static gboolean stream_is_gzip(GstGzDec* filter) {
	return filter->stream_start[0] == 0x78
	       && filter->stream_start[1] == 0xffffff9c;
}

/*
        This is the factory that will create the necessary decoder implementation instance
        and setup the according decoding function.

        We could have gone with a full-fledged abstract-class pattern declared on the decoder's side,
        and the call that classe's functions will would redirect to the class implementation (virtual functions GObject style),
        but it seemed a bit over-engineered for this tiny case with only one function type to abstract (GstGzDecFunc).
        Therefore we simply initialize the instance and function pointers here as part of
        the actual element's state. This allows the same flexibility at runtime for less framework code.
 */
static void setup_decoder (GstGzDec* filter, void* stream_writer_func) {

	GST_DEBUG ("Got stream starting chars: %x %x",
	           filter->stream_start[0],
	           filter->stream_start[1]);

	g_assert(!filter->decoder);

	if (stream_is_bzip(filter)) {
		GST_INFO ("Stream is bzip");
		filter->stream_type = BZIP;
		filter->decoder = CREATE_BZIP_DECODER(filter, stream_writer_func);
		filter->decode_func = BZIP_DECODER_DECODE;
		return;
	}
	else if (stream_is_gzip(filter)) {
		GST_INFO ("Stream is gzip");
		filter->stream_type = GZIP;
		filter->decoder = CREATE_ZIP_DECODER(filter, stream_writer_func);
		filter->decode_func = ZIP_DECODER_DECODE;
		return;
	}

	GST_WARNING ("Could not recongnize format in stream peek!");

	g_warn_if_reached();
}

void clear_decoder(GstGzDec* filter) {
	g_assert(filter->decoder);
	switch (filter->stream_type) {
	case GZIP:
		zipdec_stream_free(ZIP_DECODER_STREAM(filter->decoder));
		break;
	case BZIP:
		bzipdec_stream_free(BZIP_DECODER_STREAM(filter->decoder));
		break;
	}
	filter->decoder = NULL;
}

static void input_queue_signal_resume (GstGzDec* filter) {
	INPUT_QUEUE_LOCK(filter);
	// the queue might be empty
	filter->input_task_resume = TRUE;
	INPUT_QUEUE_SIGNAL(filter);
	INPUT_QUEUE_UNLOCK(filter);
}

static void output_queue_signal_resume (GstGzDec* filter) {
	OUTPUT_QUEUE_LOCK(filter);
	// the queue might be empty
	filter->srcpad_task_resume = TRUE;
	OUTPUT_QUEUE_SIGNAL(filter);
	OUTPUT_QUEUE_UNLOCK(filter);
}

static void input_task_start(GstGzDec* filter) {
	gst_task_start(filter->input_task);
}

static void input_task_pause(GstGzDec* filter) {
	gst_task_pause(filter->input_task);
	input_queue_signal_resume (filter);
	// aquire input stream lock to make this
	// blocks until we are actually paused
	REC_MUTEX_LOCK(GST_TASK_GET_LOCK(filter->input_task));
	REC_MUTEX_UNLOCK(GST_TASK_GET_LOCK(filter->input_task));
}

static void input_task_join(GstGzDec* filter) {
	gst_task_stop(filter->input_task);
	input_queue_signal_resume (filter);
	gst_task_join(filter->input_task);
}

static void srcpad_task_start(GstGzDec* filter) {
	GST_INFO_OBJECT (filter, "Starting srcpad task");
	gst_pad_start_task (filter->srcpad, srcpad_task_func, filter, NULL);
}

static void srcpad_task_pause(GstGzDec* filter) {
	GST_INFO_OBJECT (filter, "Setting srcpad task to paused");
	gst_task_pause (GST_PAD_TASK(filter->srcpad));
	output_queue_signal_resume (filter);
	// this will just make sure we block until
	// the task is actually done as this
	// function acquires the stream lock
	gst_pad_pause_task(filter->srcpad);
}

static void srcpad_task_join(GstGzDec* filter) {
	GST_INFO_OBJECT (filter, "Setting srcpad task to stopped");
	// this looks hackish but we can't use
	// the actual pad function as it will
	// need to acquire the task lock which
	// will only be released after we signaled the task
	gst_task_stop (GST_PAD_TASK(filter->srcpad));
	output_queue_signal_resume (filter);
	// properly shutdown the pad task here now
	// this will join the thread
	gst_pad_stop_task(filter->srcpad);
}

static void push_one_output_buffer (GstGzDec* filter, GstBuffer* buf) {

	GST_TRACE_OBJECT (filter, "Pushing one buffer");

	GstFlowReturn ret = gst_pad_push (filter->srcpad, buf);

	if (ret != GST_FLOW_OK) {
		GST_ERROR_OBJECT (filter, "Flow returned: %s", gst_flow_get_name (ret));
	}
}

static void srcpad_check_pending_eos (GstGzDec* filter) {
	GstEvent *event = NULL;

	GST_OBJECT_LOCK(filter);
	GST_TRACE_OBJECT (filter, "Checking if EOS conds met");
	// We received an EOS event AND have processed everything
	// on the input queue (after EOS received in sync with data-flow, no more data can arrive in input queue).
	// Now we can dispatch the pending EOS event!
	if (filter->eos && filter->pending_eos) {
		GST_INFO_OBJECT (filter, "Dispatching pending EOS!");
		event = filter->pending_eos;
		// make sure we don't dispatch it twice
		filter->pending_eos = NULL;
	}
	GST_OBJECT_UNLOCK(filter);

	if (event) {
		GST_LOG_OBJECT (filter, "Now handling pending EOS event on srcpad");
		if (!gst_pad_event_default (filter->sinkpad, GST_OBJECT(filter), event)) {
			GST_WARNING_OBJECT(filter, "Failed to propagate pending EOS event: %" GST_PTR_FORMAT, event);
		}
	}
}

static void srcpad_task_func(gpointer user_data) {
	GstGzDec* filter = GST_GZDEC(user_data);

	GST_TRACE_OBJECT (filter, "Entering srcpad task func");

	guint size;
	gpointer data;

	OUTPUT_QUEUE_LOCK(filter);
	size = g_queue_get_length (filter->output_queue);
	data = g_queue_pop_head (filter->output_queue);
	OUTPUT_QUEUE_UNLOCK(filter);

	GST_TRACE_OBJECT (filter, "Output queue length before pop: %d", (int) size);

	if (data) {
		push_one_output_buffer (filter, GST_BUFFER(data));
	}

	// output queue is currently empty, check if we should send EOS
	OUTPUT_QUEUE_LOCK(filter);
	while (g_queue_get_length (filter->output_queue) == 0 && !filter->srcpad_task_resume) {
		// check if its time to EOS really, if yes this function will dispatch the EOS
		// to the srcpad
		srcpad_check_pending_eos(filter);
		// anyway if the queue is empty we'll just wait
		GST_TRACE_OBJECT (filter, "Waiting in srcpad task func");
		OUTPUT_QUEUE_WAIT(filter);
		GST_TRACE_OBJECT(filter, "Resuming srcpad task func");
	}
	filter->srcpad_task_resume = FALSE;
	OUTPUT_QUEUE_UNLOCK(filter);

	GST_TRACE_OBJECT (filter, "Leaving srcpad task func");
}

static void process_one_input_buffer (GstGzDec* filter, GstBuffer* buf) {

	GST_TRACE_OBJECT (filter, "Processing one input buffer: %" GST_PTR_FORMAT, buf);

	if (!filter->decode_func(filter->decoder, buf)) {
		GST_ERROR("Failed to decode: %" GST_PTR_FORMAT, buf);
	}
}

static void input_task_func (gpointer data) {

	GstGzDec *filter = GST_GZDEC (data);
	GstBuffer* buf;
	gboolean eos = FALSE;

	GST_TRACE_OBJECT(filter, "Entering input task function. Waiting for queue access ...");

	buf = input_queue_pop_buffer (filter);
	if (buf != NULL) {
		// takes ownership of the buffer
		process_one_input_buffer(filter, buf);
		// we can get rid of it now
		gst_buffer_unref(buf);
	} else {
		// OPTIMIZABLE: this will run one iteration later than it could
		GST_OBJECT_LOCK(filter);
		// There is an EOS event pending and the input queue is fully processed
		// We are at EOS.
		if (filter->pending_eos) {
			GST_DEBUG_OBJECT(filter, "Setting EOS flag");
			eos = filter->eos = TRUE;
		}
		GST_OBJECT_UNLOCK(filter);

		// we should signal EOS to srcpad queue
		// only after releasing the object lock
		// since the srcpad task might wait for it as well
		if (eos) {
			output_queue_signal_resume(filter);
		}

		// Wait around empty queue condition
		INPUT_QUEUE_LOCK(filter);
		while(g_queue_get_length(filter->input_queue) == 0
		      && !filter->input_task_resume) {
			GST_TRACE_OBJECT(filter, "Waiting in input task func");
			INPUT_QUEUE_WAIT(filter);
			GST_TRACE_OBJECT(filter, "Resuming input task func");
		}
		// reset resume flag in case it was set before signal
		filter->input_task_resume = FALSE;
		INPUT_QUEUE_UNLOCK(filter);
	}

	GST_TRACE_OBJECT(filter, "Leaving input task function");
}

static GstBuffer* input_queue_pop_buffer (GstGzDec *filter) {
	gpointer data;
	guint size;

	INPUT_QUEUE_LOCK(filter);
	GST_TRACE_OBJECT(filter, "Pop-ing buffer");
	size = g_queue_get_length (filter->input_queue);
	data = g_queue_pop_head (filter->input_queue);
	INPUT_QUEUE_UNLOCK(filter);

	GST_TRACE_OBJECT(filter, "Input queue length before pop: %d", (int) size);

	return GST_BUFFER(data);
}

static void input_queue_append_buffer (GstGzDec *filter, GstBuffer* buf) {
	INPUT_QUEUE_LOCK(filter);
	GST_TRACE_OBJECT (filter, "Appending data to input buffer");
	g_queue_push_tail (filter->input_queue, buf);
	GST_TRACE_OBJECT(filter, "Input queue length after append: %d", (int) g_queue_get_length (filter->input_queue));
	INPUT_QUEUE_SIGNAL(filter);
	INPUT_QUEUE_UNLOCK(filter);
}

static void output_queue_append_data (GstGzDec *filter, gpointer data, gsize bytes) {

	GstBuffer* buf = BUFFER_ALLOC(bytes);
	BUFFER_SET_DATA(buf, data, bytes);

	GST_TRACE_OBJECT (filter, "Queueing new output buffer: %" GST_PTR_FORMAT, buf);

	OUTPUT_QUEUE_LOCK(filter);
	g_queue_push_tail (filter->output_queue, buf);
	OUTPUT_QUEUE_SIGNAL(filter);
	OUTPUT_QUEUE_UNLOCK(filter);
}

