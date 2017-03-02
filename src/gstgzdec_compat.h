#pragma once

#define USE_GSTATIC_REC_MUTEX FALSE // WARNING: GStaticRecMutex is deprecated !!!
// Meanwhile this is what is actually expected by the GStreamer 0.10.x
// GstTask API. The two implementations might however compatible.
#define USE_GSTREAMER_1_DOT_0_API TRUE

#if USE_GSTREAMER_1_DOT_0_API

void buffer_set_data (GstBuffer* buf, gpointer data, gsize size) {
	GstMapInfo map;
	if (!gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
		GST_ERROR ("Error mapping buffer for write access: %" GST_PTR_FORMAT, buf);
		return;
	}

	if (size > map.size) {
		GST_WARNING ("Mapped memory is smaller than data!");
	}

	memcpy(map.data, data,
	       size >= map.size ? map.size : size);

	gst_buffer_unmap (buf, &map);
}

	#define CREATE_TASK(func, data) gst_task_new(func, data, NULL) // just monkey-patch this
	#define BUFFER_SET_DATA(buf, data, size) buffer_set_data(buf, data, size)
	#define BUFFER_ALLOC(size) gst_buffer_new_allocate(NULL, size, NULL)
  #define BUFFER_SIZE gst_buffer_get_size

#else // fallback to default: GStreamer 0.10.x API

	#define CREATE_TASK(func, data) gst_task_create(func, data)
	#define BUFFER_SET_DATA(buf, data, size) gst_buffer_set_data(buf, data, size)
	#define BUFFER_ALLOC(size) gst_buffer_new_and_alloc(size)
  #define BUFFER_SIZE GST_BUFFER_SIZE

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