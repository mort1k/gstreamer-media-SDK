#include "gstmfxtask.h"
#include "gstmfxtaskaggregator.h"
#include "gstmfxutils_vaapi.h"
#include "video-utils.h"

#define DEBUG 1
#include "gstmfxdebug.h"

struct _GstMfxTask {
	GstMfxMiniObject		parent_instance;

	GstMfxDisplay          *display;
	VASurfaceID            *surfaces;
	mfxMemId               *surface_ids;
	mfxFrameInfo            frame_info;
	mfxU16                  num_surfaces;
	GQueue				   *surface_queue;
	guint					task_type;
	mfxFrameAllocResponse  *response;
	VAImage					image;

	mfxSession              session;
	gboolean                use_system_memory;
};

mfxStatus
gst_mfx_task_frame_alloc(mfxHDL pthis, mfxFrameAllocRequest *req,
	mfxFrameAllocResponse *resp)
{
	GstMfxTask *task = pthis;
	VAStatus sts;
	guint i;

	if (task->response) {
        *resp = *task->response;
		return MFX_ERR_NONE;
	}

	memset(resp, 0, sizeof (mfxFrameAllocResponse));

	/*if (!(req->Type & MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET)) {
		GST_ERROR("Unsupported surface type: %d\n", req->Type);
		return MFX_ERR_UNSUPPORTED;
	}

	if (req->Info.FourCC != MFX_FOURCC_NV12 ||
		req->Info.ChromaFormat != MFX_CHROMAFORMAT_YUV420) {
		GST_ERROR("Unsupported surface properties.\n");
		return MFX_ERR_UNSUPPORTED;
	}*/

	task->num_surfaces = req->NumFrameSuggested;
	task->frame_info = req->Info;

    task->surfaces = g_slice_alloc(task->num_surfaces * sizeof(*task->surfaces));
    task->surface_ids = g_slice_alloc(task->num_surfaces * sizeof(*task->surface_ids));
    task->surface_queue = g_queue_new();

    if (!task->surfaces || !task->surface_ids || !task->surface_queue)
        goto fail;

    GST_MFX_DISPLAY_LOCK(task->display);
    sts = vaCreateSurfaces(GST_MFX_DISPLAY_VADISPLAY(task->display),
        task->frame_info.FourCC == MFX_FOURCC_RGB4 ? VA_RT_FORMAT_RGB32 : VA_RT_FORMAT_YUV420,
        req->Info.Width, req->Info.Height,
        task->surfaces, task->num_surfaces,
        NULL, 0);
    GST_MFX_DISPLAY_UNLOCK(task->display);
    if (!vaapi_check_status(sts, "vaCreateSurfaces()")) {
        GST_ERROR("Error allocating VA surfaces\n");
        goto fail;
    }

    for (i = 0; i < task->num_surfaces; i++) {
        task->surface_ids[i] = &task->surfaces[i];
        g_queue_push_tail(task->surface_queue, task->surface_ids[i]);
    }

    resp->mids = task->surface_ids;
	resp->NumFrameActual = task->num_surfaces;

	if (task->task_type & GST_MFX_TASK_DECODER) {
		task->response = (mfxFrameAllocResponse *)
			g_slice_copy(sizeof(mfxFrameAllocResponse), resp);
	}

	return MFX_ERR_NONE;
fail:
	g_slice_free1(task->num_surfaces * sizeof(*task->surfaces), task->surfaces);
	g_slice_free1(task->num_surfaces * sizeof(*task->surface_ids), task->surface_ids);
	g_queue_free(task->surface_queue);

	return MFX_ERR_MEMORY_ALLOC;
}

mfxStatus
gst_mfx_task_frame_free(mfxHDL pthis, mfxFrameAllocResponse *resp)
{
	GstMfxTask *task = pthis;

	GST_MFX_DISPLAY_LOCK(task->display);
	if (task->surfaces)
		vaDestroySurfaces(GST_MFX_DISPLAY_VADISPLAY(task->display), task->surfaces, task->num_surfaces);
	GST_MFX_DISPLAY_UNLOCK(task->display);

	g_slice_free1(task->num_surfaces * sizeof(*task->surfaces), task->surfaces);
	g_slice_free1(task->num_surfaces * sizeof(*task->surface_ids), task->surface_ids);
	g_queue_free(task->surface_queue);
	if (task->response)
        g_slice_free1(sizeof(mfxFrameAllocResponse), task->response);

	task->num_surfaces = 0;

	return MFX_ERR_NONE;
}

mfxStatus
gst_mfx_task_frame_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
	return MFX_ERR_UNSUPPORTED;
}

mfxStatus
gst_mfx_task_frame_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
	return MFX_ERR_UNSUPPORTED;
}

mfxStatus
gst_mfx_task_frame_get_hdl(mfxHDL pthis, mfxMemId mid, mfxHDL *hdl)
{
	*hdl = mid;
	return MFX_ERR_NONE;
}

GstMfxDisplay *
gst_mfx_task_get_display(GstMfxTask * task)
{
	g_return_val_if_fail(task != NULL, 0);

	return task->display;
}

mfxSession
gst_mfx_task_get_session(GstMfxTask * task)
{
	g_return_val_if_fail(task != NULL, 0);

	return task->session;
}

GQueue *
gst_mfx_task_get_surfaces(GstMfxTask * task)
{
	g_return_val_if_fail(task != NULL, 0);

	return task->surface_queue;
}

mfxFrameInfo *
gst_mfx_task_get_frame_info(GstMfxTask * task)
{
	g_return_val_if_fail(task != NULL, 0);

	return &task->frame_info;
}

void
gst_mfx_task_set_task_type (GstMfxTask * task, guint flags)
{
	g_return_if_fail(task != NULL);

	task->task_type |= flags;
}

guint
gst_mfx_task_get_task_type (GstMfxTask * task)
{
	g_return_val_if_fail(task != NULL, GST_MFX_TASK_INVALID);

	return task->task_type;
}

gboolean
gst_mfx_task_has_type(GstMfxTask * task, guint flags)
{
	return (task->task_type & flags);
}

void
gst_mfx_task_use_system_memory(GstMfxTask * task)
{
    task->use_system_memory = TRUE;
}

gboolean
gst_mfx_task_has_system_memory(GstMfxTask * task)
{
    return task->use_system_memory;
}

static void
gst_mfx_task_finalize(GstMfxTask * task)
{
	//MFXClose(task->session);
	gst_mfx_display_unref(task->display);
}


static inline const GstMfxMiniObjectClass *
gst_mfx_task_class(void)
{
	static const GstMfxMiniObjectClass GstMfxTaskClass = {
		sizeof(GstMfxTask),
		(GDestroyNotify)gst_mfx_task_finalize
	};
	return &GstMfxTaskClass;
}

static void
gst_mfx_task_init(GstMfxTask * task, GstMfxTaskAggregator * aggregator,
	mfxSession * session, guint type_flags)
{
    task->task_type = type_flags;
	task->display = gst_mfx_display_ref(
		gst_mfx_task_aggregator_get_display(aggregator));
	task->session = *session;

    MFXVideoCORE_SetHandle(task->session, MFX_HANDLE_VA_DISPLAY,
		GST_MFX_DISPLAY_VADISPLAY(task->display));
}

GstMfxTask *
gst_mfx_task_new(GstMfxTaskAggregator * aggregator, guint type_flags)
{
	mfxSession *session;

	g_return_val_if_fail(aggregator != NULL, NULL);

	session = gst_mfx_task_aggregator_create_session(aggregator);

	return gst_mfx_task_new_with_session(aggregator, session, type_flags);
}

GstMfxTask *
gst_mfx_task_new_with_session(GstMfxTaskAggregator * aggregator,
	mfxSession * session, guint type_flags)
{
	GstMfxTask *task;

	g_return_val_if_fail(aggregator != NULL, NULL);
	g_return_val_if_fail(session != NULL, NULL);

	task = gst_mfx_mini_object_new0(gst_mfx_task_class());
	if (!task)
		return NULL;

	gst_mfx_task_init(task, aggregator, session, type_flags);

	return task;
}

GstMfxTask *
gst_mfx_task_ref(GstMfxTask * task)
{
	g_return_val_if_fail(task != NULL, NULL);

	return gst_mfx_mini_object_ref(GST_MFX_MINI_OBJECT(task));
}

void
gst_mfx_task_unref(GstMfxTask * task)
{
	gst_mfx_mini_object_unref(GST_MFX_MINI_OBJECT(task));
}

void
gst_mfx_task_replace(GstMfxTask ** old_task_ptr,
	GstMfxTask * new_task)
{
	g_return_if_fail(old_task_ptr != NULL);

	gst_mfx_mini_object_replace((GstMfxMiniObject **)old_task_ptr,
		GST_MFX_MINI_OBJECT(new_task));
}
