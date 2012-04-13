/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include "qwaylandshmbackingstore.h"

#include <QtCore/qdebug.h>

#include "qwaylanddisplay.h"
#include "qwaylandshmwindow.h"
#include "qwaylandscreen.h"
#include "qwaylanddecoration.h"

#include <QtGui/QPainter>

#include <wayland-client.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

QT_BEGIN_NAMESPACE

QWaylandShmBuffer::QWaylandShmBuffer(QWaylandDisplay *display,
                     const QSize &size, QImage::Format format)
    : mMarginsImage(0)
{
    int stride = size.width() * 4;
    int alloc = stride * size.height();
    char filename[] = "/tmp/wayland-shm-XXXXXX";
    int fd = mkstemp(filename);
    if (fd < 0)
        qWarning("open %s failed: %s", filename, strerror(errno));
    if (ftruncate(fd, alloc) < 0) {
        qWarning("ftruncate failed: %s", strerror(errno));
        close(fd);
        return;
    }
    uchar *data = (uchar *)
            mmap(NULL, alloc, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    unlink(filename);

    if (data == (uchar *) MAP_FAILED) {
        qWarning("mmap /dev/zero failed: %s", strerror(errno));
        close(fd);
        return;
    }

    mImage = QImage(data, size.width(), size.height(), stride, format);
    mShmPool = wl_shm_create_pool(display->shm(), fd, alloc);
    mBuffer = wl_shm_pool_create_buffer(mShmPool,0, size.width(), size.height(),
                                       stride, WL_SHM_FORMAT_ARGB8888);
    close(fd);
}

QWaylandShmBuffer::~QWaylandShmBuffer(void)
{
    munmap((void *) mImage.constBits(), mImage.byteCount());
    wl_buffer_destroy(mBuffer);
    wl_shm_pool_destroy(mShmPool);
}

QImage *QWaylandShmBuffer::imageInsideMargins(const QMargins &margins)
{
    if (!margins.isNull() && margins != mMargins) {
        if (mMarginsImage) {
            delete mMarginsImage;
        }
        uchar *bits = const_cast<uchar *>(mImage.constBits());
        uchar *b_s_data = bits + margins.top() * mImage.bytesPerLine() + margins.left() * 4;
        int b_s_width = mImage.size().width() - margins.left() - margins.right();
        int b_s_height = mImage.size().height() - margins.top() - margins.bottom();
        mMarginsImage = new QImage(b_s_data, b_s_width,b_s_height,mImage.bytesPerLine(),mImage.format());
    }
    if (margins.isNull()) {
        delete mMarginsImage;
        mMarginsImage = 0;
    }

    mMargins = margins;
    if (!mMarginsImage)
        return &mImage;

    return mMarginsImage;

}

QWaylandShmBackingStore::QWaylandShmBackingStore(QWindow *window)
    : QPlatformBackingStore(window)
    , mDisplay(QWaylandScreen::waylandScreenFromWindow(window)->display())
    , mFrontBuffer(0)
    , mBackBuffer(0)
    , mFrontBufferIsDirty(false)
    , mPainting(false)
    , mWindowDecoration(0)
    , mFrameCallback(0)
{
}

QWaylandShmBackingStore::~QWaylandShmBackingStore()
{
    if (mFrameCallback)
        wl_callback_destroy(mFrameCallback);

//    if (mFrontBuffer == waylandWindow()->attached())
//        waylandWindow()->attach(0);

    if (mFrontBuffer != mBackBuffer)
        delete mFrontBuffer;

    delete mBackBuffer;
}

QPaintDevice *QWaylandShmBackingStore::paintDevice()
{
    if (!mWindowDecoration)
        return mBackBuffer->image();
    return mBackBuffer->imageInsideMargins(mWindowDecoration->margins());
}

void QWaylandShmBackingStore::beginPaint(const QRegion &)
{
    mPainting = true;
    ensureSize();

    if (waylandWindow()->attached() && mBackBuffer == waylandWindow()->attached()) {
        QWaylandShmWindow *waylandWindow = static_cast<QWaylandShmWindow *>(window()->handle());
        Q_ASSERT(waylandWindow->windowType() == QWaylandWindow::Shm);
        waylandWindow->waitForFrameSync();
    }

}

void QWaylandShmBackingStore::endPaint()
{
    mPainting = false;
}

void QWaylandShmBackingStore::ensureSize()
{
    bool decoration = false;
    switch (window()->windowType()) {
        case Qt::Window:
    case Qt::Widget:
    case Qt::Dialog:
    case Qt::Tool:
    case Qt::Drawer:
        decoration = true;
        break;
    default:
        break;
    }
    if (window()->windowFlags() & Qt::FramelessWindowHint) {
        decoration = false;
    }

    if (decoration) {
        if (!mWindowDecoration) {
            mWindowDecoration = new QWaylandDecoration(window(), this);
        }
    } else {
        delete mWindowDecoration;
        mWindowDecoration = 0;
    }
    resize(mRequestedSize);
}

void QWaylandShmBackingStore::flush(QWindow *window, const QRegion &region, const QPoint &offset)
{
    Q_UNUSED(window);
    Q_UNUSED(offset);
    Q_ASSERT(waylandWindow()->windowType() == QWaylandWindow::Shm);

    mFrontBuffer = mBackBuffer;

    if (mFrameCallback) {
        mFrontBufferIsDirty = true;
        return;
    }

    mFrameCallback = wl_surface_frame(waylandWindow()->wl_surface());
    wl_callback_add_listener(mFrameCallback,&frameCallbackListener,this);
    QMargins margins = windowDecorationMargins();

    if (waylandWindow()->attached() != mFrontBuffer) {
        delete waylandWindow()->attached();
        waylandWindow()->attach(mFrontBuffer);
    }

    QVector<QRect> rects = region.rects();
    for (int i = 0; i < rects.size(); i++) {
        QRect rect = rects.at(i);
        rect.translate(margins.left(),margins.top());
        waylandWindow()->damage(rect);
    }
    mFrontBufferIsDirty = false;
}

void QWaylandShmBackingStore::resize(const QSize &size, const QRegion &)
{
    mRequestedSize = size;
}

void QWaylandShmBackingStore::resize(const QSize &size)
{

    QMargins margins = windowDecorationMargins();
    QSize sizeWithMargins = size + QSize(margins.left()+margins.right(),margins.top()+margins.bottom());

    QImage::Format format = QPlatformScreen::platformScreenForWindow(window())->format();

    if (mBackBuffer != NULL && mBackBuffer->size() == sizeWithMargins)
        return;

    if (mBackBuffer != mFrontBuffer) {
        delete mBackBuffer; //we delete the attached buffer when we flush
    }

    mBackBuffer = new QWaylandShmBuffer(mDisplay, sizeWithMargins, format);

    if (mWindowDecoration)
        mWindowDecoration->paintDecoration();
}

QImage *QWaylandShmBackingStore::entireSurface() const
{
    return mBackBuffer->image();
}

void QWaylandShmBackingStore::done(void *data, wl_callback *callback, uint32_t time)
{
    Q_UNUSED(callback);
    Q_UNUSED(time);
    QWaylandShmBackingStore *self =
            static_cast<QWaylandShmBackingStore *>(data);
    QWaylandWindow *window = self->waylandWindow();
    wl_callback_destroy(self->mFrameCallback);
    self->mFrameCallback = 0;
    if (self->mFrontBuffer != window->attached()) {
        delete window->attached();
        window->attach(self->mFrontBuffer);
    }

    if (self->mFrontBufferIsDirty && !self->mPainting) {
        self->mFrontBufferIsDirty = false;
        self->mFrameCallback = wl_surface_frame(window->wl_surface());
        wl_callback_add_listener(self->mFrameCallback,&self->frameCallbackListener,self);
        window->damage(QRect(QPoint(0,0),self->mFrontBuffer->size()));
    }
}

const struct wl_callback_listener QWaylandShmBackingStore::frameCallbackListener = {
    QWaylandShmBackingStore::done
};

QT_END_NAMESPACE
