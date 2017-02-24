#pragma once

#define OUT_BUFFER_SIZE 16*1024

typedef void (*StreamWriterFunc) (gpointer user_data, gpointer data, gsize bytes);

typedef z_stream ZStream;

struct _ZipDecoderStreamCtx {
	ZStream stream;
	gpointer user_data;
	StreamWriterFunc writer_func;
};

typedef struct _ZipDecoderStreamCtx ZipDecoderStreamCtx;

#define ZIP_DECODER_STREAM(ptr) ((ZipDecoderStreamCtx*)ptr) 

ZipDecoderStreamCtx* zipdec_stream_new(gpointer user_data, StreamWriterFunc writer_func) {
	ZipDecoderStreamCtx* ctx = ZIP_DECODER_STREAM(g_malloc(sizeof(ZipDecoderStreamCtx)));
	ctx->user_data = user_data;
	ctx->writer_func = writer_func;
	return ctx;
}

void zipdec_stream_free(ZipDecoderStreamCtx* ctx) {
	g_free(ctx);
}

int zipdec_stream_digest_buffer(ZipDecoderStreamCtx *ctx, GstBuffer* buf) {

	gpointer user_data = ctx->user_data;
	ZStream* strm = &ctx->stream;
	StreamWriterFunc writer_func = ctx->writer_func;

    int ret;
    unsigned have;
    unsigned char out[OUT_BUFFER_SIZE];

    /* allocate inflate state */
    strm->zalloc = Z_NULL;
    strm->zfree = Z_NULL;
    strm->opaque = Z_NULL;
    strm->avail_in = 0;
    strm->next_in = Z_NULL;
    ret = inflateInit(strm);
    if (ret != Z_OK)
        return ret;

    guint buffer_size = GST_BUFFER_SIZE(buf);
    gpointer buffer_data = BUFFER_GET_DATA(buf);

    /* decompress until deflate stream ends or end of file */
    do {
        strm->avail_in = buffer_size;
        if (!buffer_data) {
            (void)inflateEnd(strm);
            return Z_ERRNO;
        }

        if (strm->avail_in == 0)
            break;
        
        strm->next_in = buffer_data;

        /* run inflate() on input until output buffer not full */
        do {
            strm->avail_out = OUT_BUFFER_SIZE;
            strm->next_out = out;
            ret = inflate(strm, Z_NO_FLUSH);
            g_assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(strm);
                return ret;
            }
            have = OUT_BUFFER_SIZE - strm->avail_out;

            writer_func(user_data, out, have);

            //FIXME: handle errors?
            /*
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(strm);
                return Z_ERRNO;
            }
            */
        } while (strm->avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
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
    strm->zalloc = Z_NULL;
    strm->zfree = Z_NULL;
    strm->opaque = Z_NULL;
    strm->avail_in = 0;
    strm->next_in = Z_NULL;
    ret = inflateInit(strm);
    if (ret != Z_OK)
        return ret;

    /* decompress until deflate stream ends or end of file */
    do {
        strm->avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)inflateEnd(strm);
            return Z_ERRNO;
        }
        if (strm->avail_in == 0)
            break;
        strm->next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm->avail_out = CHUNK;
            strm->next_out = out;
            ret = inflate(strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(strm);
                return ret;
            }
            have = CHUNK - strm->avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(strm);
                return Z_ERRNO;
            }
        } while (strm->avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

#endif