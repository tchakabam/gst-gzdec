#pragma once

#define BZIP_DEC_STREAM_OUT_BUFFER_SIZE 16*1024

#define BZIP_DECODER_STREAM(ptr) ((BzipDecoderStream*)ptr) 
typedef struct _BzipDecoderStream BzipDecoderStream;
typedef void (*StreamWriterFunc) (gpointer user_data, gpointer data, gsize bytes);
typedef bz_stream BzipStream;

struct _BzipDecoderStream {
	BzipStream stream;
	gpointer user_data;
	StreamWriterFunc writer_func;
};

static BzipDecoderStream* bzipdec_stream_new(gpointer user_data, StreamWriterFunc writer_func) {
	BzipDecoderStream* wrapper = BZIP_DECODER_STREAM(g_malloc(sizeof(BzipDecoderStream)));
	// NOTE: 
	// If we dont do this memset BZ2_bzDecompressInit crashes 1 out of 5 times consistent 
	// with a SEGV sig because it expects to initialize a completely 0'd piece of memory, 
	// as otherwise things fall apart in there apparently.
	memset(wrapper, 0, sizeof(BzipDecoderStream)); // Important
	wrapper->user_data = user_data;
	wrapper->writer_func = writer_func;
	GST_INFO("+BZ2_bzDecompressInit");
	int ret = BZ2_bzDecompressInit(&wrapper->stream, 0, 0);
	GST_INFO("-BZ2_bzDecompressInit");
    if (ret != BZ_OK) {
    	GST_ERROR("Got code %d when calling BZ2_bzDecompressInit", (int) ret);
    }
	return wrapper;
}


static void bzipdec_stream_free(BzipDecoderStream* wrapper) {
	BZ2_bzDecompressEnd(&wrapper->stream);
	g_free(wrapper);
}

static gboolean bzipdec_stream_digest_buffer(void *w, GstBuffer* buf) {

	GST_TRACE ("Processing one buffer for inflation: %" GST_PTR_FORMAT, buf);

	BzipDecoderStream *wrapper = BZIP_DECODER_STREAM(w);

	// unwrap components
	gpointer user_data = wrapper->user_data;
	BzipStream* strm = &wrapper->stream;
	StreamWriterFunc writer_func = wrapper->writer_func;

	// processing state
    int ret;
    guint have;
    gboolean success = FALSE;

    // deflate output buffer
    gchar out[BZIP_DEC_STREAM_OUT_BUFFER_SIZE];
    guint out_size = sizeof(out);

    // input buffer
    guint buffer_size;
    gpointer buffer_data;

#ifdef USE_GSTREAMER_1_DOT_0_API
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
      GST_ERROR ("Error mapping buffer for read access: %" GST_PTR_FORMAT, buf);
      return Z_ERRNO;
    }
    buffer_size = map.size;
    buffer_data = map.data;
#else
    buffer_size = BUFFER_SIZE(buf)
    buffer_data = GST_BUFFER_DATA(buf);
#endif

#if 0
	// debug: prints hex dump of input buffer
	guchar* buffer_chars = buffer_data;
	for (gint i = 0; i < buffer_size; i++)
	{
	    if (i > 0) g_print(":");
	    g_print("%02X", buffer_chars[i]);
	}
	g_print("\n");
#endif

    GST_TRACE("Input pointer is %p", buffer_data);
	GST_TRACE ("Input chunk size: %d", (int) buffer_size);
	GST_TRACE ("Total output buffer size: %d", out_size);

    // set initial input pointer and size
    strm->avail_in = buffer_size;
    strm->next_in = buffer_data;

    while(strm->avail_in) {

    	// reset output buffer on every iteration
	    strm->avail_out = out_size;
	    strm->next_out = out;
	    
	    // this function will inflate as much from the input
	    // as our output buffer can take
	    // therefore we need to iterate eventually several times 
	    // over this function
	    GST_TRACE ("Running decompress func now");
	    ret = BZ2_bzDecompress(strm);
	    if (!(ret == BZ_OK || ret == BZ_STREAM_END)) {
	    	GST_ERROR("BZ2_bzDecompress returned code %d", (int) ret);
	    	goto done;
	    }
	    GST_TRACE("BZ2_bzDecompress returned %d", (int) ret);

		GST_TRACE ("Remaining output buffer bytes: %d", (int) strm->avail_out);

	    have = out_size - strm->avail_out;

	    GST_TRACE("Have %d inflated bytes, writing to output stream", (int) have);

	    writer_func(user_data, out, have);
    }

    success = TRUE;

done:
#ifdef USE_GSTREAMER_1_DOT_0_API
    gst_buffer_unmap (buf, &map);
#endif
    return success;
}

