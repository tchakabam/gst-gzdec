#pragma once

/* This is stream wrapper for Zlib inflate */

#define ZIP_DEC_STREAM_OUT_BUFFER_SIZE 16*1024
#define ZLIB_INFLATE_WINDOW_BITS 32 // This value (32) enables gzip as well as zlib formats
									// by automatic header detection.
									// Force to Gzip only with 16
									// TODO: one could make this a run-time param
									// of the wrapper constructor.

#define ZIP_DECODER_STREAM(ptr) ((ZipDecoderStream*)ptr) 
typedef struct _ZipDecoderStream ZipDecoderStream;
typedef void (*StreamWriterFunc) (gpointer user_data, gpointer data, gsize bytes);
typedef z_stream ZStream;

struct _ZipDecoderStream {
	ZStream stream;
	gpointer user_data;
	StreamWriterFunc writer_func;
	gboolean header;
};

ZipDecoderStream* zipdec_stream_new(gpointer user_data, StreamWriterFunc writer_func) {
	ZipDecoderStream* wrapper = ZIP_DECODER_STREAM(g_malloc(sizeof(ZipDecoderStream)));
	wrapper->user_data = user_data;
	wrapper->writer_func = writer_func;
    wrapper->stream.zalloc = Z_NULL;
    wrapper->stream.zfree = Z_NULL;
    wrapper->stream.opaque = Z_NULL;
    wrapper->stream.avail_in = 0;
    wrapper->stream.next_in = Z_NULL;
	int ret = inflateInit2(&wrapper->stream, ZLIB_INFLATE_WINDOW_BITS);
    if (ret != Z_OK) {
    	GST_ERROR("Got code %d when calling Zlib inflateInit2", (int) ret);
    }
	return wrapper;
}

void zipdec_stream_free(ZipDecoderStream* wrapper) {
	inflateEnd(&wrapper->stream);
	g_free(wrapper);
}

gboolean zipdec_stream_digest_buffer(void *w, GstBuffer* buf) {

	ZipDecoderStream *wrapper = ZIP_DECODER_STREAM(w);

	GST_INFO ("Processing one buffer for inflation: %" GST_PTR_FORMAT, buf);

	// unwrap components
	gpointer user_data = wrapper->user_data;
	ZStream* strm = &wrapper->stream;
	StreamWriterFunc writer_func = wrapper->writer_func;

	// processing state
    int ret;
    guint have;
    gboolean success = FALSE;

    // deflate output buffer
    guchar out[ZIP_DEC_STREAM_OUT_BUFFER_SIZE];
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

    GST_INFO("Input pointer is %p", buffer_data);
	GST_INFO ("Input chunk size: %d", (int) buffer_size);
	GST_INFO ("Total output buffer size: %d", out_size);

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
	    GST_INFO ("Running inflate func now");
	    if ((ret = inflate(strm, Z_NO_FLUSH)) == Z_STREAM_ERROR) {
	    	GST_ERROR("Zlib inflate returned Z_STREAM_ERROR");
	    	goto done;	
	    }
	    GST_INFO("Inflate returned %d", (int) ret);
	    switch (ret) {
	    case Z_NEED_DICT:
	        ret = Z_DATA_ERROR;/* and fall through */
	    	GST_WARNING("Zlib code: Z_NEED_DICT");
	    case Z_DATA_ERROR:
	    case Z_MEM_ERROR:
	    	GST_ERROR("Data/Memory error or missing dictionnary (code: %d)", ret);
	        goto done;
	    }

		GST_INFO ("Remaining output buffer bytes: %d", (int) strm->avail_out);

	    have = out_size - strm->avail_out;

	    GST_INFO("Have %d inflated bytes, writing to output stream", (int) have);

	    writer_func(user_data, out, have);
    }

    success = TRUE;

done:
#ifdef USE_GSTREAMER_1_DOT_0_API
    gst_buffer_unmap (buf, &map);
#endif
    return success;
}

