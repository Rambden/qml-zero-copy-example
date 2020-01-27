/******************************************************************************
 *
 * Copyright (c) 2020
 * Konstantin Ripak, kostya.ripak<at>gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

#include "v4l2source.h"
#include <QThread>
#include <QtDebug>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <QOpenGLContext>
#include <QtPlatformHeaders/qeglnativecontext.h>
#include <libdrm/drm_fourcc.h>

#include <glib-object.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/gstvideometa.h>

#define MAX_ATTRIBUTES_COUNT 30

static int gst_video_format_to_drm_code(GstVideoFormat format)
{
    switch (format) {
    case GST_VIDEO_FORMAT_I420:
        return DRM_FORMAT_YUV420;
    case GST_VIDEO_FORMAT_NV16:
        return DRM_FORMAT_NV16;
    case GST_VIDEO_FORMAT_NV12:
        return DRM_FORMAT_NV12;
    case GST_VIDEO_FORMAT_UYVY:
        return DRM_FORMAT_UYVY;
    case GST_VIDEO_FORMAT_YVYU:
        return DRM_FORMAT_YVYU;
    case GST_VIDEO_FORMAT_YUY2:
        return DRM_FORMAT_YUYV;
    case GST_VIDEO_FORMAT_YV12:
        return DRM_FORMAT_YVU422;
    case GST_VIDEO_FORMAT_RGB:
        return DRM_FORMAT_RGB888;
    case GST_VIDEO_FORMAT_BGR:
        return DRM_FORMAT_BGR888;
    case GST_VIDEO_FORMAT_ARGB:
        return DRM_FORMAT_ARGB8888;
    case GST_VIDEO_FORMAT_RGBA:
        return DRM_FORMAT_RGBA8888;
    case GST_VIDEO_FORMAT_xRGB:
        return DRM_FORMAT_XBGR8888;
    case GST_VIDEO_FORMAT_BGRx:
        return DRM_FORMAT_BGRX8888;
    case GST_VIDEO_FORMAT_GRAY8:
        return DRM_FORMAT_R8;
    case GST_VIDEO_FORMAT_I420_10LE:
        return DRM_FORMAT_YUV420;
    default:
        qCritical() << "Unsupported format";
    }
    return 0;
}

static QVideoFrame::PixelFormat
gst_video_format_to_qvideoformat(GstVideoFormat format)
{
    switch (format) {
    case GST_VIDEO_FORMAT_I420:
        return QVideoFrame::PixelFormat::Format_IMC3;
    case GST_VIDEO_FORMAT_NV12:
        return QVideoFrame::PixelFormat::Format_NV12;
    case GST_VIDEO_FORMAT_UYVY:
        return QVideoFrame::PixelFormat::Format_UYVY;
    case GST_VIDEO_FORMAT_YUY2:
        return QVideoFrame::PixelFormat::Format_YUYV;
    case GST_VIDEO_FORMAT_YV12:
        return QVideoFrame::PixelFormat::Format_YV12;
    case GST_VIDEO_FORMAT_RGB:
        return QVideoFrame::PixelFormat::Format_RGB24;
    case GST_VIDEO_FORMAT_BGR:
        return QVideoFrame::PixelFormat::Format_BGR24;
    case GST_VIDEO_FORMAT_ARGB:
        return QVideoFrame::PixelFormat::Format_ARGB32;
    case GST_VIDEO_FORMAT_RGBA:
        return QVideoFrame::PixelFormat::Format_RGB32;
    case GST_VIDEO_FORMAT_xRGB:
        return QVideoFrame::PixelFormat::Format_ARGB32;
    case GST_VIDEO_FORMAT_BGRx:
        return QVideoFrame::PixelFormat::Format_BGR32;
    case GST_VIDEO_FORMAT_GRAY8:
        return QVideoFrame::PixelFormat::Format_Y8;
    default:
        qCritical() << "Unsupported format";
    }
    return QVideoFrame::PixelFormat::Format_Invalid;
}

#define GST_BUFFER_GET_DMAFD(buffer, plane)                                    \
    (((plane) < gst_buffer_n_memory((buffer))) ?                               \
         gst_dmabuf_memory_get_fd(gst_buffer_peek_memory((buffer), (plane))) : \
         gst_dmabuf_memory_get_fd(gst_buffer_peek_memory((buffer), 0)))

class GstDmaVideoBuffer : public QAbstractVideoBuffer
{
public:
    // This  should be called from renderer thread
    GstDmaVideoBuffer(GstBuffer* buffer, GstVideoMeta* videoMeta) :
        QAbstractVideoBuffer(HandleType::EGLImageHandle),
        buffer(gst_buffer_ref(buffer)), m_videoMeta(videoMeta)

    {
        static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR =
            reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
                eglGetProcAddress("eglCreateImageKHR"));
        int idx = 0;
        EGLint attribs[MAX_ATTRIBUTES_COUNT];

        attribs[idx++] = EGL_WIDTH;
        attribs[idx++] = m_videoMeta->width;
        attribs[idx++] = EGL_HEIGHT;
        attribs[idx++] = m_videoMeta->height;
        attribs[idx++] = EGL_LINUX_DRM_FOURCC_EXT;
        attribs[idx++] = gst_video_format_to_drm_code(m_videoMeta->format);
        attribs[idx++] = EGL_DMA_BUF_PLANE0_FD_EXT;
        attribs[idx++] = GST_BUFFER_GET_DMAFD(buffer, 0);
        attribs[idx++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
        attribs[idx++] = m_videoMeta->offset[0];
        attribs[idx++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
        attribs[idx++] = m_videoMeta->stride[0];
        if (m_videoMeta->n_planes > 1) {
            attribs[idx++] = EGL_DMA_BUF_PLANE1_FD_EXT;
            attribs[idx++] = GST_BUFFER_GET_DMAFD(buffer, 1);
            attribs[idx++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
            attribs[idx++] = m_videoMeta->offset[1];
            attribs[idx++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
            attribs[idx++] = m_videoMeta->stride[1];
        }
        if (m_videoMeta->n_planes > 2) {
            attribs[idx++] = EGL_DMA_BUF_PLANE2_FD_EXT;
            attribs[idx++] = GST_BUFFER_GET_DMAFD(buffer, 2);
            attribs[idx++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
            attribs[idx++] = m_videoMeta->offset[2];
            attribs[idx++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
            attribs[idx++] = m_videoMeta->stride[2];
        }
        attribs[idx++] = EGL_NONE;

        auto m_qOpenGLContext = QOpenGLContext::currentContext();
        QEGLNativeContext qEglContext =
            qvariant_cast<QEGLNativeContext>(m_qOpenGLContext->nativeHandle());

        EGLDisplay dpy = qEglContext.display();
        Q_ASSERT(dpy != EGL_NO_DISPLAY);

        image = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                                  (EGLClientBuffer) nullptr, attribs);
        Q_ASSERT(image != EGL_NO_IMAGE_KHR);
    }

    QVariant handle() const override
    {
        return QVariant::fromValue<EGLImage>(image);
    }

    void release() override
    {
    }

    uchar* map(MapMode mode, int* numBytes, int* bytesPerLine) override
    {
        return nullptr;
    }

    MapMode mapMode() const override
    {
        return NotMapped;
    }

    void unmap() override
    {
    }

    // This should be called from renderer thread
    virtual ~GstDmaVideoBuffer() override
    {
        static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR =
            reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
                eglGetProcAddress("eglDestroyImageKHR"));

        auto m_qOpenGLContext = QOpenGLContext::currentContext();
        QEGLNativeContext qEglContext =
            qvariant_cast<QEGLNativeContext>(m_qOpenGLContext->nativeHandle());

        EGLDisplay dpy = qEglContext.display();
        Q_ASSERT(dpy != EGL_NO_DISPLAY);
        eglDestroyImageKHR(dpy, image);
        gst_buffer_unref(buffer);
    }

private:
    EGLImage image;
    GstBuffer* buffer;
    GstVideoMeta* m_videoMeta;
};

class GstVideoBuffer : public QAbstractPlanarVideoBuffer
{
public:
    // This  should be called from renderer thread
    GstVideoBuffer(GstBuffer* buffer, GstVideoMeta* videoMeta) :
        QAbstractPlanarVideoBuffer(HandleType::NoHandle),
        m_buffer(gst_buffer_ref(buffer)), m_videoMeta(videoMeta),
        m_mode(QAbstractVideoBuffer::MapMode::NotMapped)
    {
    }

    QVariant handle() const override
    {
        return QVariant();
    }

    void release() override
    {
    }

    int map(MapMode mode,
            int* numBytes,
            int bytesPerLine[4],
            uchar* data[4]) override
    {
        int size = 0;
        const GstMapFlags flags =
            GstMapFlags(((mode & ReadOnly) ? GST_MAP_READ : 0) |
                        ((mode & WriteOnly) ? GST_MAP_WRITE : 0));
        if (mode == NotMapped || m_mode != NotMapped) {
            return 0;
        } else {
            for (int i = 0; i < m_videoMeta->n_planes; i++) {
                gst_video_meta_map(m_videoMeta, i, &m_mapInfo[i],
                                   (gpointer*)&data[i], &bytesPerLine[i],
                                   flags);
                size += m_mapInfo[i].size;
            }
        }
        m_mode = mode;
        *numBytes = size;
        return m_videoMeta->n_planes;
    }

    MapMode mapMode() const override
    {
        return m_mode;
    }

    void unmap() override
    {
        if (m_mode != NotMapped) {
            for (int i = 0; i < m_videoMeta->n_planes; i++) {
                gst_video_meta_unmap(m_videoMeta, i, &m_mapInfo[i]);
            }
        }
        m_mode = NotMapped;
    }

    // This should be called from renderer thread
    virtual ~GstVideoBuffer() override
    {
        unmap();
        gst_buffer_unref(m_buffer);
    }

private:
    GstBuffer* m_buffer;
    MapMode m_mode;
    GstVideoMeta* m_videoMeta;
    GstMapInfo m_mapInfo[4];
};

static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer data);

GstAppSinkCallbacks V4L2Source::callbacks = {.eos = nullptr,
                                             .new_preroll = nullptr,
                                             .new_sample =
                                                 &V4L2Source::on_new_sample};

// Request v4l2src allocator to add GstVideoMeta to buffers
static GstPadProbeReturn
appsink_pad_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data)
{
    if (info->type & GST_PAD_PROBE_TYPE_QUERY_BOTH) {
        GstQuery* query = gst_pad_probe_info_get_query(info);
        if (GST_QUERY_TYPE(query) == GST_QUERY_ALLOCATION) {
            gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
        }
    }
    return GST_PAD_PROBE_OK;
}

V4L2Source::V4L2Source(QQuickItem* parent) : QQuickItem(parent)
{
    m_surface = nullptr;
    connect(this, &QQuickItem::windowChanged, this, &V4L2Source::setWindow);

    pipeline = gst_pipeline_new("V4L2Source::pipeline");
    v4l2src = gst_element_factory_make("v4l2src", nullptr);
    appsink = gst_element_factory_make("appsink", nullptr);

    GstPad* pad = gst_element_get_static_pad(appsink, "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_QUERY_BOTH, appsink_pad_probe,
                      nullptr, nullptr);
    gst_object_unref(pad);

    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, this,
                               nullptr);

    gst_bin_add_many(GST_BIN(pipeline), v4l2src, appsink, nullptr);
    gst_element_link(v4l2src, appsink);

    context = g_main_context_new();
    loop = g_main_loop_new(context, FALSE);
}

V4L2Source::~V4L2Source()
{
    stop();
    gst_object_unref(pipeline);
    g_main_context_unref(context);
    g_main_loop_unref(loop);
}

void V4L2Source::setVideoSurface(QAbstractVideoSurface* surface)
{
    if (m_surface != surface && m_surface && m_surface->isActive()) {
        m_surface->stop();
    }
    m_surface = surface;
    if (surface
            ->supportedPixelFormats(
                QAbstractVideoBuffer::HandleType::EGLImageHandle)
            .size() > 0) {
        EGLImageSupported = true;
    } else {
        EGLImageSupported = false;
    }

    if (m_surface && m_device.length() > 0) {
        start();
    }
}

void V4L2Source::setDevice(QString device)
{
    m_device = device;
    if (m_surface && m_device.length() > 0) {
        start();
    }
}

static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer data)
{
    GMainLoop* loop = (GMainLoop*)data;

    switch (GST_MESSAGE_TYPE(msg)) {

    case GST_MESSAGE_EOS:
        qDebug() << "End of stream";
        g_main_loop_quit(loop);
        break;

    case GST_MESSAGE_ERROR: {
        gchar* debug;
        GError* error;

        gst_message_parse_error(msg, &error, &debug);
        g_free(debug);

        qWarning() << "Error: " << error->message;
        g_error_free(error);

        g_main_loop_quit(loop);
        break;
    }
    default:
        break;
    }

    return TRUE;
}

void V4L2Source::run()
{
    g_main_context_push_thread_default(g_main_loop_get_context(loop));
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_main_loop_run(loop);
    gst_element_set_state(pipeline, GST_STATE_NULL);
}

void V4L2Source::start()
{
    if (worker_handle.valid()) {
        stop();
    }

    g_object_set(v4l2src, "device", m_device.toStdString().c_str(), nullptr);

    if (EGLImageSupported) {
        g_object_set(v4l2src, "io-mode  ", 4, nullptr);
    }

    worker_handle = std::async(std::launch::async, &V4L2Source::run, this);
}

void V4L2Source::stop()
{
    GstBus* bus = gst_element_get_bus(GST_ELEMENT(pipeline));
    bool ret = gst_bus_post(bus, gst_message_new_eos(GST_OBJECT(pipeline)));
    worker_handle.wait();
    g_clear_pointer(&bus, gst_object_unref);
}

void V4L2Source::setWindow(QQuickWindow* win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this,
                &V4L2Source::sync, Qt::DirectConnection);
    }
}

static bool buffer_is_dmabuf(GstBuffer* buffer)
{
    guint n_mem = gst_buffer_n_memory(buffer);
    for (guint i = 0; i < n_mem; i++) {
        GstMemory* memory = gst_buffer_peek_memory(buffer, 0);
        if (!gst_is_dmabuf_memory(memory)) {
            return false;
        }
    }
    return true;
}

// Make sure this callback is invoked from rendering thread
void V4L2Source::sync()
{
    {
        QMutexLocker locker(&mutex);
        if (!ready) {
            return;
        }
        // reset ready flag
        ready = false;
    }
    // pull available sample and convert GstBuffer into a QAbstractVideoBuffer
    GstElement* _appsink = gst_bin_get_by_name((GstBin*)pipeline, "appsink0");
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(_appsink));
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstVideoMeta* videoMeta = gst_buffer_get_video_meta(buffer);

    // if memory is DMABUF and EGLImage is supported by the backend,
    // create video buffer with EGLImage handle
    videoFrame.reset();
    if (EGLImageSupported && buffer_is_dmabuf(buffer)) {
        videoBuffer.reset(new GstDmaVideoBuffer(buffer, videoMeta));
    } else {
        // TODO: support other memory types, probably GL textures?
        // just map memory
        videoBuffer.reset(new GstVideoBuffer(buffer, videoMeta));
    }

    QSize size = QSize(videoMeta->width, videoMeta->height);
    QVideoFrame::PixelFormat format =
        gst_video_format_to_qvideoformat(videoMeta->format);

    videoFrame.reset(new QVideoFrame(
        static_cast<QAbstractVideoBuffer*>(videoBuffer.get()), size, format));

    if (!m_surface->isActive()) {
        m_format = QVideoSurfaceFormat(size, format);
        Q_ASSERT(m_surface->start(m_format) == true);
    }
    m_surface->present(*videoFrame);
    gst_sample_unref(sample);
}

GstFlowReturn V4L2Source::on_new_sample(GstAppSink* sink, gpointer data)
{
    Q_UNUSED(sink)
    V4L2Source* self = (V4L2Source*)data;
    QMutexLocker locker(&self->mutex);
    self->ready = true;
    self->frameReady();
    return GST_FLOW_OK;
}
