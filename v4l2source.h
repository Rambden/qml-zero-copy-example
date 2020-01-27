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
#ifndef V4L2SOURCE_H
#define V4L2SOURCE_H

#include <QAbstractVideoSurface>
#include <QMutex>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QVideoSurfaceFormat>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

class V4L2SourceWorker;

class V4L2Source : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QAbstractVideoSurface* videoSurface READ videoSurface WRITE
                   setVideoSurface)
    Q_PROPERTY(QString device MEMBER m_device READ device WRITE setDevice)
    Q_PROPERTY(QString caps MEMBER m_caps)

public:
    V4L2Source(QQuickItem* parent = nullptr);
    virtual ~V4L2Source();

    void setVideoSurface(QAbstractVideoSurface* surface);
    void setDevice(QString device);

public slots:
    void start();
    void stop();

private slots:
    void setWindow(QQuickWindow* win);
    void sync();

signals:
    void frameReady();

protected:
    QAbstractVideoSurface* videoSurface() const
    {
        return m_surface;
    }

    QString device() const
    {
        return m_device;
    }

private:
    void run();
    GstFlowReturn static on_new_sample(GstAppSink* sink, gpointer data);

    static GstAppSinkCallbacks callbacks;

    // properties:
    QAbstractVideoSurface* m_surface;
    QString m_device;
    QString m_caps;

    // state:
    bool EGLImageSupported;
    int fd;
    bool ready;
    QVideoSurfaceFormat m_format;
    QSharedPointer<QAbstractVideoBuffer> videoBuffer;
    QSharedPointer<QVideoFrame> videoFrame;
    QMutex mutex;
    std::future<void> worker_handle;

    GMainContext* context;
    GMainLoop* loop;
    GstElement* pipeline;
    GstElement* v4l2src;
    GstElement* appsink;
};

#endif // V4L2SOURCE_H
