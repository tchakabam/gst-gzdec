#pragma once

struct _GstGzDecPrivate
{
	// TODO
};

typedef struct _GstGzDecPrivate GstGzDecPrivate;

GstBuffer* input_queue_pop_buffer (GstGzDec *filter);
void srcpad_task_func(gpointer user_data);
void output_queue_append_data (GstGzDec *filter, gpointer data, gsize bytes);

void push_one_output_buffer (GstGzDec* filter, GstBuffer* buf) {

	GST_TRACE_OBJECT (filter, "Pushing one buffer");

 	GstFlowReturn ret = gst_pad_push (filter->srcpad, buf);

 	if (ret != GST_FLOW_OK) {
 		filter->error = TRUE;
 		GST_ERROR_OBJECT (filter, "Flow returned: %s", gst_flow_get_name (ret));
 	}
}

void srcpad_check_pending_eos (GstGzDec* filter) {
	GstEvent *event = NULL;

	GST_OBJECT_LOCK(filter);
	// We received an EOS event AND have processed everything
	// on the input queue (after EOS received in sync with data-flow, no more data can arrive in input queue).
	// Now we can dispatch the pending EOS event!
	if (filter->eos) {
		GST_INFO_OBJECT (filter, "Dispatching pending EOS!");
		event = filter->pending_eos;
	}
	GST_OBJECT_UNLOCK(filter);

	if (event) {
		GST_LOG_OBJECT (filter, "Now handling pending EOS event on srcpad");
		if (!gst_pad_event_default (filter->sinkpad, GST_OBJECT(filter), event)) {
			GST_WARNING_OBJECT(filter, "Failed to propagate pending EOS event: %" GST_PTR_FORMAT, event);
		}

		gst_pad_pause_task (filter->srcpad);
	}
}

void srcpad_task_func(gpointer user_data) {
	GstGzDec* filter = GST_GZDEC(user_data);

	GST_TRACE_OBJECT (filter, "Entering srcpad task func");

	guint size;
	gpointer data;

	OUTPUT_QUEUE_LOCK(filter);
	size = g_queue_get_length (filter->output_queue);
	data = g_queue_pop_head (filter->output_queue);
	OUTPUT_QUEUE_UNLOCK(filter);

	GST_INFO_OBJECT (filter, "Output queue length before pop: %d", (int) size);

	if (data) {
		push_one_output_buffer (filter, GST_BUFFER(data));
	}

	// output queue is currently empty, check if we should send EOS
	OUTPUT_QUEUE_LOCK(filter);
	size = g_queue_get_length (filter->output_queue);
	while (size == 0) {
		// check if its time to EOS really
		srcpad_check_pending_eos(filter);
		// anyway if the queue is empty we'll just wait
		GST_INFO_OBJECT (filter, "Waiting in srcpad task func");
		OUTPUT_QUEUE_WAIT(filter);
		GST_INFO_OBJECT(filter, "Resuming srcpad task func");
	}
	OUTPUT_QUEUE_UNLOCK(filter);

	GST_TRACE_OBJECT (filter, "Leaving srcpad task func");
}

void do_work_on_buffer_unlocked(GstGzDec* filter, GstBuffer* buf) {

	// TESTING
	char foo = 'c';

	output_queue_append_data (filter, &foo, 1);
}

void process_one_input_buffer (GstGzDec* filter, GstBuffer* buf) {

	GST_TRACE_OBJECT (filter, "Processing one input buffer: %" GST_PTR_FORMAT, buf);

	// FIXME: why again?
	/*
	if (G_UNLIKELY(filter->error)) {
		return;
	}
	*/

	// DO ACTUAL PROCESSING HERE!!

	do_work_on_buffer_unlocked(filter, buf);

	//////////////////////////////
	//////////////////////////////
}

void input_task_func (gpointer data) {

 	GstGzDec *filter = GST_GZDEC (data);
 	GstBuffer* buf;

	GST_TRACE_OBJECT(filter, "Entering input task function");

	GST_DEBUG_OBJECT(filter, "Waiting for queue access ...");

	buf = input_queue_pop_buffer (filter);
 	if (buf != NULL) {
		process_one_input_buffer(filter, buf);	
	} else {
		// OPTIMIZABLE: this will run one iteration later than it could
		GST_OBJECT_LOCK(filter);
		// There is an EOS event pending and the input queue is fully processed
		// We are at EOS.
		if (filter->pending_eos) {
			filter->eos = TRUE;
		}
		GST_OBJECT_UNLOCK(filter);

		// Wait around empty queue condition
		INPUT_QUEUE_LOCK(filter);
		while(g_queue_get_length(filter->input_queue) == 0) {
			GST_INFO_OBJECT(filter, "Waiting in input task func");
			INPUT_QUEUE_WAIT(filter);
			GST_INFO_OBJECT(filter, "Resuming input task func");
		}
		INPUT_QUEUE_UNLOCK(filter);
		
	}

	GST_TRACE_OBJECT(filter, "Leaving input task function");
}

GstBuffer* input_queue_pop_buffer (GstGzDec *filter) {
	gpointer data;
	guint size;

	INPUT_QUEUE_LOCK(filter);
	GST_DEBUG_OBJECT(filter, "Pop-ing buffer");
	size = g_queue_get_length (filter->input_queue);
	data = g_queue_pop_head (filter->input_queue);
	INPUT_QUEUE_UNLOCK(filter);

	GST_DEBUG_OBJECT(filter, "Input queue length before pop: %d", (int) size);

	return GST_BUFFER(data);
}

void input_queue_append_buffer (GstGzDec *filter, GstBuffer* buf) {
	INPUT_QUEUE_LOCK(filter);
	GST_DEBUG_OBJECT (filter, "Appending data to input buffer");
	g_queue_push_tail (filter->input_queue, gst_buffer_ref(buf));
	GST_DEBUG_OBJECT(filter, "Input queue length after append: %d", (int) g_queue_get_length (filter->input_queue));
	INPUT_QUEUE_SIGNAL(filter);
	INPUT_QUEUE_UNLOCK(filter);
}

void output_queue_append_data (GstGzDec *filter, gpointer data, gsize bytes) {

	GstBuffer* buf = BUFFER_ALLOC(bytes);
	BUFFER_SET_DATA(buf, data, bytes);

	GST_LOG_OBJECT (filter, "Queueing new output buffer: %" GST_PTR_FORMAT, buf);

	OUTPUT_QUEUE_LOCK(filter);
	g_queue_push_tail (filter->output_queue, buf);
	OUTPUT_QUEUE_SIGNAL(filter);
	OUTPUT_QUEUE_UNLOCK(filter);
}

/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */

#if 0

int inf(FILE *source, FILE *dest)
{
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)inflateEnd(&strm);
            return Z_ERRNO;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
            }
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

#endif
