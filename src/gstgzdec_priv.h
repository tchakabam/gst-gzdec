#pragma once

struct _GstGzDecPrivate
{
	// TODO
};

typedef struct _GstGzDecPrivate GstGzDecPrivate;

guint pop_input_buffer(GstGzDec *filter, GstBuffer* buf);
void push_output_buffers(gpointer user_data);
void queue_new_output_buffer (GstGzDec *filter, gpointer data, gsize bytes);

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

void drive_worker_state(GstGzDec *filter, guint queue_size) {
	if (queue_size == 0) {
		gst_task_pause(filter->decode_worker);
	}
}

void push_one_output_buffer (gpointer data, gpointer user_data) {
	GstBuffer* buf = GST_BUFFER(data);
	GstGzDec* filter = GST_GZDEC(user_data);

 	GstFlowReturn ret = gst_pad_push (filter->srcpad, buf);

 	if (ret != GST_FLOW_OK) {
 		filter->error = TRUE;
 		GST_ERROR_OBJECT (filter, "Flow returned: %s", gst_flow_get_name (ret));
 	}

 	OUTPUT_QUEUE_LOCK(filter);
 	filter->output_buffers = g_list_remove (filter->output_buffers, buf);
 	OUTPUT_QUEUE_UNLOCK(filter);
}

void push_output_buffers(gpointer user_data) {
	guint in_size, out_size;
	GstGzDec* filter = GST_GZDEC(user_data);

	OUTPUT_QUEUE_LOCK(filter);
	out_size = g_list_length(filter->output_buffers);
	if (out_size == 0) {
		GST_INFO_OBJECT (filter, "No output buffers to push");
	} else {
		GST_INFO_OBJECT (filter, "Now pushing whole output queue (%d buffers)", (int) out_size);
		g_list_foreach(filter->output_buffers, push_one_output_buffer, filter);		
	}
	OUTPUT_QUEUE_UNLOCK(filter);

	INPUT_QUEUE_LOCK(filter);
	in_size = g_list_length(filter->input_buffers);
	INPUT_QUEUE_UNLOCK(filter);

	GstEvent *event = NULL;
	GST_OBJECT_LOCK(filter);
	// We received an EOS event AND have processed everything
	// on the input queue (after EOS received in sync with data-flow, no more data can arrive in input queue).
	// Now we can dispatch the pending EOS event!
	if (filter->eos && in_size == 0) {
		GST_INFO_OBJECT (filter, "Dispatching pending EOS!");
		event = filter->pending_eos;
	}
	GST_OBJECT_UNLOCK(filter);

	if (event) {
		GST_LOG_OBJECT (filter, "Now handling pending EOS event on srcpad");
		if (!gst_pad_event_default (filter->sinkpad, GST_OBJECT(filter), event)) {
			GST_WARNING_OBJECT(filter, "Failed to propagate pending EOS event: %" GST_PTR_FORMAT, event);
		}
	}

	if (filter->use_async_push) {
		GST_TRACE_OBJECT (filter, "Setting srcpad task to paused");
		gst_pad_pause_task (filter->srcpad);		
	}

	GST_TRACE_OBJECT (filter, "Done pushing output buffers");
}

void do_work_on_buffer_unlocked(GstGzDec* filter, GstBuffer* buf) {

	// TESTING
	char foo = 'c';

	queue_new_output_buffer (filter, &foo, 1);
}

void process_one_input_buffer (gpointer data, gpointer user_data) {

	GstBuffer* buf = GST_BUFFER(data);
	GstGzDec* filter = GST_GZDEC(user_data);

	GST_TRACE_OBJECT (filter, "Processing one input buffer: %" GST_PTR_FORMAT, buf);
	if (G_UNLIKELY(filter->error)) {
		return;
	}

	drive_worker_state(
		filter,
		pop_input_buffer(filter, buf)
	);

	INPUT_QUEUE_UNLOCK(filter);

	// DO ACTUAL PROCESSING HERE!!

	do_work_on_buffer_unlocked(filter, buf);

	//////////////////////////////
	//////////////////////////////

	INPUT_QUEUE_LOCK(filter);
}

void decode_worker_func (gpointer data) {

 	GstGzDec *filter = GST_GZDEC (data);
 	guint size;
 	gboolean use_async_push = filter->use_async_push;

	GST_TRACE_OBJECT(filter, "Entering worker function");

	GST_DEBUG_OBJECT(filter, "Waiting for queue access ...");
	INPUT_QUEUE_LOCK(filter);
	size = g_list_length (filter->input_buffers);
	drive_worker_state(filter, size);
	GST_DEBUG_OBJECT(filter, "Input queue length before run: %d", (int) size);
	g_list_foreach(filter->input_buffers, process_one_input_buffer, filter);
 	INPUT_QUEUE_UNLOCK(filter);

	if (use_async_push) {
 		GST_TRACE_OBJECT (filter, "Scheduling async push (starting srcpad task)");
 		gst_pad_start_task (filter->srcpad, push_output_buffers, filter, NULL);	
	}

 	if (!filter->use_async_push) {
	 	// NOTE: with this we are running the push on our worker task;
	 	//       however in principle this is a streaming task job
	 	//       that should be run on the respective task's streaming thread directly.
	 	//       Doing it like this makes this worker thread the de-facto down-streaming-thread!
	 	//       Async push allows to further decouple the heavy-duty from the downstreaming-thread
	 	//       to avoid our processing being blocked by downstream components.
		push_output_buffers(filter);	
 	}

	GST_TRACE_OBJECT(filter, "Leaving worker function");
}

guint pop_input_buffer(GstGzDec *filter, GstBuffer* buf) {
	guint size;

	INPUT_QUEUE_LOCK(filter);
	GST_DEBUG_OBJECT(filter, "Pop-ing buffer");
	filter->input_buffers = g_list_remove (filter->input_buffers, buf);
	size = g_list_length (filter->input_buffers);
	INPUT_QUEUE_UNLOCK(filter);

	GST_DEBUG_OBJECT(filter, "Input queue length after pop: %d", (int) size);

	return size;
}

void append_buffer (GstGzDec *filter, GstBuffer* buf) {
	INPUT_QUEUE_LOCK(filter);
	GST_DEBUG_OBJECT (filter, "Appending data to input buffer");
	filter->input_buffers = g_list_append (filter->input_buffers, gst_buffer_ref(buf));
	GST_DEBUG_OBJECT(filter, "Input queue length after append: %d", (int) g_list_length (filter->input_buffers));
	INPUT_QUEUE_UNLOCK(filter);
}

void queue_new_output_buffer (GstGzDec *filter, gpointer data, gsize bytes) {

	GstBuffer* buf = BUFFER_ALLOC(bytes);
	BUFFER_SET_DATA(buf, data, bytes);

	GST_LOG_OBJECT (filter, "Queueing new output buffer: %" GST_PTR_FORMAT, buf);

	OUTPUT_QUEUE_LOCK(filter);
	filter->output_buffers = g_list_append (filter->output_buffers, buf);
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
