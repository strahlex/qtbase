/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qxcbscreen.h"
#include "qxcbwindow.h"
#include "qxcbcursor.h"
#include "qxcbimage.h"
#include "qnamespace.h"
#include "qxcbxsettings.h"

#include <stdio.h>

#include <QDebug>

#include <qpa/qwindowsysteminterface.h>
#include <private/qmath_p.h>

QT_BEGIN_NAMESPACE

QXcbVirtualDesktop::QXcbVirtualDesktop(QXcbConnection *connection, xcb_screen_t *screen, int number)
    : QXcbObject(connection)
    , m_screen(screen)
    , m_number(number)
    , m_xSettings(Q_NULLPTR)
{
}

QXcbVirtualDesktop::~QXcbVirtualDesktop()
{
    delete m_xSettings;
}

QXcbXSettings *QXcbVirtualDesktop::xSettings() const
{
    if (!m_xSettings) {
        QXcbVirtualDesktop *self = const_cast<QXcbVirtualDesktop *>(this);
        self->m_xSettings = new QXcbXSettings(self);
    }
    return m_xSettings;
}

QXcbScreen::QXcbScreen(QXcbConnection *connection, QXcbVirtualDesktop *virtualDesktop,
                       xcb_randr_output_t outputId, xcb_randr_get_output_info_reply_t *output,
                       QString outputName)
    : QXcbObject(connection)
    , m_virtualDesktop(virtualDesktop)
    , m_output(outputId)
    , m_crtc(output ? output->crtc : 0)
    , m_mode(XCB_NONE)
    , m_primary(false)
    , m_rotation(XCB_RANDR_ROTATION_ROTATE_0)
    , m_outputName(outputName)
    , m_outputSizeMillimeters(output ? QSize(output->mm_width, output->mm_height) : QSize())
    , m_virtualSize(virtualDesktop->size())
    , m_virtualSizeMillimeters(virtualDesktop->physicalSize())
    , m_orientation(Qt::PrimaryOrientation)
    , m_refreshRate(60)
    , m_forcedDpi(-1)
    , m_devicePixelRatio(1)
    , m_hintStyle(QFontEngine::HintStyle(-1))
    , m_noFontHinting(false)
    , m_subpixelType(QFontEngine::SubpixelAntialiasingType(-1))
    , m_antialiasingEnabled(-1)
{
    if (connection->hasXRandr()) {
        xcb_randr_select_input(xcb_connection(), screen()->root, true);
        xcb_randr_get_crtc_info_cookie_t crtcCookie =
            xcb_randr_get_crtc_info_unchecked(xcb_connection(), m_crtc, output ? output->timestamp : 0);
        xcb_randr_get_crtc_info_reply_t *crtc =
            xcb_randr_get_crtc_info_reply(xcb_connection(), crtcCookie, NULL);
        if (crtc) {
            updateGeometry(QRect(crtc->x, crtc->y, crtc->width, crtc->height), crtc->rotation);
            updateRefreshRate(crtc->mode);
            free(crtc);
        }
    } else {
        updateGeometry(output ? output->timestamp : 0);
    }

    const int dpr = int(devicePixelRatio());
    if (m_geometry.isEmpty()) {
        m_geometry = QRect(QPoint(), m_virtualSize/dpr);
        m_nativeGeometry = QRect(QPoint(), m_virtualSize);
    }
    if (m_availableGeometry.isEmpty())
        m_availableGeometry = m_geometry;

    readXResources();

    // disable font hinting when we do UI scaling
    static bool dpr_scaling_enabled = (qgetenv("QT_DEVICE_PIXEL_RATIO").toInt() > 1
                           || qgetenv("QT_DEVICE_PIXEL_RATIO").toLower() == "auto");
    if (dpr_scaling_enabled)
        m_noFontHinting = true;

    QScopedPointer<xcb_get_window_attributes_reply_t, QScopedPointerPodDeleter> rootAttribs(
        xcb_get_window_attributes_reply(xcb_connection(),
            xcb_get_window_attributes_unchecked(xcb_connection(), screen()->root), NULL));
    const quint32 existingEventMask = rootAttribs.isNull() ? 0 : rootAttribs->your_event_mask;

    const quint32 mask = XCB_CW_EVENT_MASK;
    const quint32 values[] = {
        // XCB_CW_EVENT_MASK
        XCB_EVENT_MASK_ENTER_WINDOW
        | XCB_EVENT_MASK_LEAVE_WINDOW
        | XCB_EVENT_MASK_PROPERTY_CHANGE
        | XCB_EVENT_MASK_STRUCTURE_NOTIFY // for the "MANAGER" atom (system tray notification).
        | existingEventMask // don't overwrite the event mask on the root window
    };

    xcb_change_window_attributes(xcb_connection(), screen()->root, mask, values);

    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(xcb_connection(),
            xcb_get_property_unchecked(xcb_connection(), false, screen()->root,
                             atom(QXcbAtom::_NET_SUPPORTING_WM_CHECK),
                             XCB_ATOM_WINDOW, 0, 1024), NULL);

    if (reply && reply->format == 32 && reply->type == XCB_ATOM_WINDOW) {
        xcb_window_t windowManager = *((xcb_window_t *)xcb_get_property_value(reply));

        if (windowManager != XCB_WINDOW_NONE) {
            xcb_get_property_reply_t *windowManagerReply =
                xcb_get_property_reply(xcb_connection(),
                    xcb_get_property_unchecked(xcb_connection(), false, windowManager,
                                     atom(QXcbAtom::_NET_WM_NAME),
                                     atom(QXcbAtom::UTF8_STRING), 0, 1024), NULL);
            if (windowManagerReply && windowManagerReply->format == 8 && windowManagerReply->type == atom(QXcbAtom::UTF8_STRING)) {
                m_windowManagerName = QString::fromUtf8((const char *)xcb_get_property_value(windowManagerReply), xcb_get_property_value_length(windowManagerReply));
            }

            free(windowManagerReply);
        }
    }
    free(reply);

    const xcb_query_extension_reply_t *sync_reply = xcb_get_extension_data(xcb_connection(), &xcb_sync_id);
    if (!sync_reply || !sync_reply->present)
        m_syncRequestSupported = false;
    else
        m_syncRequestSupported = true;

    m_clientLeader = xcb_generate_id(xcb_connection());
    Q_XCB_CALL2(xcb_create_window(xcb_connection(),
                                  XCB_COPY_FROM_PARENT,
                                  m_clientLeader,
                                  screen()->root,
                                  0, 0, 1, 1,
                                  0,
                                  XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                  screen()->root_visual,
                                  0, 0), connection);
#ifndef QT_NO_DEBUG
    QByteArray ba("Qt client leader window for screen ");
    ba += m_outputName.toUtf8();
    Q_XCB_CALL2(xcb_change_property(xcb_connection(),
                                   XCB_PROP_MODE_REPLACE,
                                   m_clientLeader,
                                   atom(QXcbAtom::_NET_WM_NAME),
                                   atom(QXcbAtom::UTF8_STRING),
                                   8,
                                   ba.length(),
                                   ba.constData()), connection);
#endif

    Q_XCB_CALL2(xcb_change_property(xcb_connection(),
                                    XCB_PROP_MODE_REPLACE,
                                    m_clientLeader,
                                    atom(QXcbAtom::WM_CLIENT_LEADER),
                                    XCB_ATOM_WINDOW,
                                    32,
                                    1,
                                    &m_clientLeader), connection);

    xcb_depth_iterator_t depth_iterator =
        xcb_screen_allowed_depths_iterator(screen());

    while (depth_iterator.rem) {
        xcb_depth_t *depth = depth_iterator.data;
        xcb_visualtype_iterator_t visualtype_iterator =
            xcb_depth_visuals_iterator(depth);

        while (visualtype_iterator.rem) {
            xcb_visualtype_t *visualtype = visualtype_iterator.data;
            m_visuals.insert(visualtype->visual_id, *visualtype);
            m_visualDepths.insert(visualtype->visual_id, depth->depth);
            xcb_visualtype_next(&visualtype_iterator);
        }

        xcb_depth_next(&depth_iterator);
    }

    m_cursor = new QXcbCursor(connection, this);
}

QXcbScreen::~QXcbScreen()
{
    delete m_cursor;
}


QWindow *QXcbScreen::topLevelAt(const QPoint &p) const
{
    xcb_window_t root = screen()->root;

    int dpr = int(devicePixelRatio());
    int x = p.x() / dpr;
    int y = p.y() / dpr;

    xcb_window_t parent = root;
    xcb_window_t child = root;

    do {
        xcb_translate_coordinates_cookie_t translate_cookie =
            xcb_translate_coordinates_unchecked(xcb_connection(), parent, child, x, y);

        xcb_translate_coordinates_reply_t *translate_reply =
            xcb_translate_coordinates_reply(xcb_connection(), translate_cookie, NULL);

        if (!translate_reply) {
            return 0;
        }

        parent = child;
        child = translate_reply->child;
        x = translate_reply->dst_x;
        y = translate_reply->dst_y;

        free(translate_reply);

        if (!child || child == root)
            return 0;

        QPlatformWindow *platformWindow = connection()->platformWindowFromId(child);
        if (platformWindow)
            return platformWindow->window();
    } while (parent != child);

    return 0;
}


QPoint QXcbScreen::mapToNative(const QPoint &pos) const
{
    const int dpr = int(devicePixelRatio());
    return (pos - m_geometry.topLeft()) * dpr + m_nativeGeometry.topLeft();
}

QPoint QXcbScreen::mapFromNative(const QPoint &pos) const
{
    const int dpr = int(devicePixelRatio());
    return (pos - m_nativeGeometry.topLeft()) / dpr + m_geometry.topLeft();
}

QPointF QXcbScreen::mapToNative(const QPointF &pos) const
{
    const int dpr = int(devicePixelRatio());
    return (pos - m_geometry.topLeft()) * dpr + m_nativeGeometry.topLeft();
}

QPointF QXcbScreen::mapFromNative(const QPointF &pos) const
{
    const int dpr = int(devicePixelRatio());
    return (pos - m_nativeGeometry.topLeft()) / dpr + m_geometry.topLeft();
}

QRect QXcbScreen::mapToNative(const QRect &rect) const
{
    const int dpr = int(devicePixelRatio());
    return QRect(mapToNative(rect.topLeft()), rect.size() * dpr);
}

QRect QXcbScreen::mapFromNative(const QRect &rect) const
{
    const int dpr = int(devicePixelRatio());
    return QRect(mapFromNative(rect.topLeft()), rect.size() / dpr);
}

void QXcbScreen::windowShown(QXcbWindow *window)
{
    // Freedesktop.org Startup Notification
    if (!connection()->startupId().isEmpty() && window->window()->isTopLevel()) {
        sendStartupMessage(QByteArrayLiteral("remove: ID=") + connection()->startupId());
        connection()->clearStartupId();
    }
}

void QXcbScreen::sendStartupMessage(const QByteArray &message) const
{
    xcb_window_t rootWindow = root();

    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 8;
    ev.type = connection()->atom(QXcbAtom::_NET_STARTUP_INFO_BEGIN);
    ev.window = rootWindow;
    int sent = 0;
    int length = message.length() + 1; // include NUL byte
    const char *data = message.constData();
    do {
        if (sent == 20)
            ev.type = connection()->atom(QXcbAtom::_NET_STARTUP_INFO);

        const int start = sent;
        const int numBytes = qMin(length - start, 20);
        memcpy(ev.data.data8, data + start, numBytes);
        xcb_send_event(connection()->xcb_connection(), false, rootWindow, XCB_EVENT_MASK_PROPERTY_CHANGE, (const char *) &ev);

        sent += numBytes;
    } while (sent < length);
}

const xcb_visualtype_t *QXcbScreen::visualForId(xcb_visualid_t visualid) const
{
    QMap<xcb_visualid_t, xcb_visualtype_t>::const_iterator it = m_visuals.find(visualid);
    if (it == m_visuals.constEnd())
        return 0;
    return &*it;
}

quint8 QXcbScreen::depthOfVisual(xcb_visualid_t visualid) const
{
    QMap<xcb_visualid_t, quint8>::const_iterator it = m_visualDepths.find(visualid);
    if (it == m_visualDepths.constEnd())
        return 0;
    return *it;
}

QImage::Format QXcbScreen::format() const
{
    return QImage::Format_RGB32;
}

QDpi QXcbScreen::virtualDpi() const
{
    return QDpi(Q_MM_PER_INCH * m_virtualSize.width() / m_virtualSizeMillimeters.width(),
                Q_MM_PER_INCH * m_virtualSize.height() / m_virtualSizeMillimeters.height());
}

QDpi QXcbScreen::logicalDpi() const
{
    static const int overrideDpi = qEnvironmentVariableIntValue("QT_FONT_DPI");
    if (overrideDpi)
        return QDpi(overrideDpi, overrideDpi);

    int primaryDpr = int(connection()->screens().at(0)->devicePixelRatio());
    if (m_forcedDpi > 0)
        return QDpi(m_forcedDpi/primaryDpr, m_forcedDpi/primaryDpr);
    QDpi vDpi = virtualDpi();
    return QDpi(vDpi.first/primaryDpr, vDpi.second/primaryDpr);
}


qreal QXcbScreen::devicePixelRatio() const
{
    static int override_dpr = qEnvironmentVariableIntValue("QT_DEVICE_PIXEL_RATIO");
    static bool auto_dpr = qgetenv("QT_DEVICE_PIXEL_RATIO").toLower() == "auto";
    if (override_dpr > 0)
        return override_dpr;
    if (auto_dpr)
        return m_devicePixelRatio;
    return 1.0;
}

QPlatformCursor *QXcbScreen::cursor() const
{
    return m_cursor;
}

/*!
    \brief handle the XCB screen change event and update properties

    On a mobile device, the ideal use case is that the accelerometer would
    drive the orientation. This could be achieved by using QSensors to read the
    accelerometer and adjusting the rotation in QML, or by reading the
    orientation from the QScreen object and doing the same, or in many other
    ways. However, on X we have the XRandR extension, which makes it possible
    to have the whole screen rotated, so that individual apps DO NOT have to
    rotate themselves. Apps could optionally use the
    QScreen::primaryOrientation property to optimize layout though.
    Furthermore, there is no support in X for accelerometer events anyway. So
    it makes more sense on a Linux system running X to just run a daemon which
    monitors the accelerometer and runs xrandr automatically to do the rotation,
    then apps do not have to be aware of it (but probably the window manager
    would resize them accordingly). updateGeometry() is written with this
    design in mind. Therefore the physical geometry, available geometry,
    virtual geometry, orientation and primaryOrientation should all change at
    the same time.  On a system which cannot rotate the whole screen, it would
    be correct for only the orientation (not the primary orientation) to
    change.
*/
void QXcbScreen::handleScreenChange(xcb_randr_screen_change_notify_event_t *change_event)
{
    // No need to do anything when screen rotation did not change - if any
    // xcb output geometry has changed, we will get RRCrtcChangeNotify and
    // RROutputChangeNotify events next
    if (change_event->rotation == m_rotation)
        return;

    m_rotation = change_event->rotation;
    switch (m_rotation) {
    case XCB_RANDR_ROTATION_ROTATE_0: // xrandr --rotate normal
        m_orientation = Qt::LandscapeOrientation;
        m_virtualSize.setWidth(change_event->width);
        m_virtualSize.setHeight(change_event->height);
        m_virtualSizeMillimeters.setWidth(change_event->mwidth);
        m_virtualSizeMillimeters.setHeight(change_event->mheight);
        break;
    case XCB_RANDR_ROTATION_ROTATE_90: // xrandr --rotate left
        m_orientation = Qt::PortraitOrientation;
        m_virtualSize.setWidth(change_event->height);
        m_virtualSize.setHeight(change_event->width);
        m_virtualSizeMillimeters.setWidth(change_event->mheight);
        m_virtualSizeMillimeters.setHeight(change_event->mwidth);
        break;
    case XCB_RANDR_ROTATION_ROTATE_180: // xrandr --rotate inverted
        m_orientation = Qt::InvertedLandscapeOrientation;
        m_virtualSize.setWidth(change_event->width);
        m_virtualSize.setHeight(change_event->height);
        m_virtualSizeMillimeters.setWidth(change_event->mwidth);
        m_virtualSizeMillimeters.setHeight(change_event->mheight);
        break;
    case XCB_RANDR_ROTATION_ROTATE_270: // xrandr --rotate right
        m_orientation = Qt::InvertedPortraitOrientation;
        m_virtualSize.setWidth(change_event->height);
        m_virtualSize.setHeight(change_event->width);
        m_virtualSizeMillimeters.setWidth(change_event->mheight);
        m_virtualSizeMillimeters.setHeight(change_event->mwidth);
        break;
    // We don't need to do anything with these, since QScreen doesn't store reflection state,
    // and Qt-based applications probably don't need to care about it anyway.
    case XCB_RANDR_ROTATION_REFLECT_X: break;
    case XCB_RANDR_ROTATION_REFLECT_Y: break;
    }

    updateGeometry(change_event->timestamp);

    QWindowSystemInterface::handleScreenGeometryChange(QPlatformScreen::screen(), geometry(), availableGeometry());
    QWindowSystemInterface::handleScreenOrientationChange(QPlatformScreen::screen(), m_orientation);

    QDpi ldpi = logicalDpi();
    QWindowSystemInterface::handleScreenLogicalDotsPerInchChange(QPlatformScreen::screen(), ldpi.first, ldpi.second);

    // Windows which had null screens have already had expose events by now.
    // They need to be told the screen is back, it's OK to render.
    foreach (QWindow *window, QGuiApplication::topLevelWindows()) {
        QXcbWindow *xcbWin = static_cast<QXcbWindow*>(window->handle());
        if (xcbWin)
            xcbWin->maybeSetScreen(this);
    }
}

void QXcbScreen::updateGeometry(xcb_timestamp_t timestamp)
{
    if (!connection()->hasXRandr())
        return;

    xcb_randr_get_crtc_info_cookie_t crtcCookie =
        xcb_randr_get_crtc_info_unchecked(xcb_connection(), m_crtc, timestamp);
    xcb_randr_get_crtc_info_reply_t *crtc =
        xcb_randr_get_crtc_info_reply(xcb_connection(), crtcCookie, NULL);
    if (crtc) {
        updateGeometry(QRect(crtc->x, crtc->y, crtc->width, crtc->height), crtc->rotation);
        free(crtc);
    }
}

void QXcbScreen::updateGeometry(const QRect &geom, uint8_t rotation)
{
    QRect xGeometry = geom;
    QRect xAvailableGeometry = xGeometry;
    switch (rotation) {
    case XCB_RANDR_ROTATION_ROTATE_0: // xrandr --rotate normal
        m_orientation = Qt::LandscapeOrientation;
        m_sizeMillimeters = m_outputSizeMillimeters;
        break;
    case XCB_RANDR_ROTATION_ROTATE_90: // xrandr --rotate left
        m_orientation = Qt::PortraitOrientation;
        m_sizeMillimeters = m_outputSizeMillimeters.transposed();
        break;
    case XCB_RANDR_ROTATION_ROTATE_180: // xrandr --rotate inverted
        m_orientation = Qt::InvertedLandscapeOrientation;
        m_sizeMillimeters = m_outputSizeMillimeters;
        break;
    case XCB_RANDR_ROTATION_ROTATE_270: // xrandr --rotate right
        m_orientation = Qt::InvertedPortraitOrientation;
        m_sizeMillimeters = m_outputSizeMillimeters.transposed();
        break;
    }

    // It can be that physical size is unknown while virtual size
    // is known (probably back-calculated from DPI and resolution),
    // e.g. on VNC or with some hardware.
    if (m_sizeMillimeters.isEmpty()) {
        QDpi dpi = virtualDpi();
        m_sizeMillimeters = QSizeF(Q_MM_PER_INCH * xGeometry.width() / dpi.first,
                                   Q_MM_PER_INCH * xGeometry.width() / dpi.second);
    }

    xcb_get_property_reply_t * workArea =
        xcb_get_property_reply(xcb_connection(),
            xcb_get_property_unchecked(xcb_connection(), false, screen()->root,
                             atom(QXcbAtom::_NET_WORKAREA),
                             XCB_ATOM_CARDINAL, 0, 1024), NULL);

    if (workArea && workArea->type == XCB_ATOM_CARDINAL && workArea->format == 32 && workArea->value_len >= 4) {
        // If workArea->value_len > 4, the remaining ones seem to be for virtual desktops.
        // But QScreen doesn't know about that concept.  In reality there could be a
        // "docked" panel (with _NET_WM_STRUT_PARTIAL atom set) on just one desktop.
        // But for now just assume the first 4 values give us the geometry of the
        // "work area", AKA "available geometry"
        uint32_t *geom = (uint32_t*)xcb_get_property_value(workArea);
        QRect virtualAvailableGeometry(geom[0], geom[1], geom[2], geom[3]);
        // Take the intersection of the desktop's available geometry with this screen's geometry
        // to get the part of the available geometry which belongs to this screen.
        xAvailableGeometry = xGeometry & virtualAvailableGeometry;
    }
    free(workArea);

    qreal dpi = xGeometry.width() / physicalSize().width() * qreal(25.4);
    m_devicePixelRatio = qRound(dpi/96);
    const int dpr = int(devicePixelRatio()); // we may override m_devicePixelRatio
    m_geometry = QRect(xGeometry.topLeft(), xGeometry.size()/dpr);
    m_nativeGeometry = QRect(xGeometry.topLeft(), xGeometry.size());
    m_availableGeometry = QRect(mapFromNative(xAvailableGeometry.topLeft()), xAvailableGeometry.size()/dpr);
    QWindowSystemInterface::handleScreenGeometryChange(QPlatformScreen::screen(), m_geometry, m_availableGeometry);
}

void QXcbScreen::updateRefreshRate(xcb_randr_mode_t mode)
{
    if (!connection()->hasXRandr())
        return;

    if (m_mode == mode)
        return;

    // we can safely use get_screen_resources_current here, because in order to
    // get here, we must have called get_screen_resources before
    xcb_randr_get_screen_resources_current_cookie_t resourcesCookie =
        xcb_randr_get_screen_resources_current_unchecked(xcb_connection(), screen()->root);
    xcb_randr_get_screen_resources_current_reply_t *resources =
        xcb_randr_get_screen_resources_current_reply(xcb_connection(), resourcesCookie, NULL);
    if (resources) {
        xcb_randr_mode_info_iterator_t modesIter =
            xcb_randr_get_screen_resources_current_modes_iterator(resources);
        for (; modesIter.rem; xcb_randr_mode_info_next(&modesIter)) {
            xcb_randr_mode_info_t *modeInfo = modesIter.data;
            if (modeInfo->id == mode) {
                m_refreshRate = modeInfo->dot_clock / (modeInfo->htotal * modeInfo->vtotal);
                m_mode = mode;
                break;
            }
        }

        free(resources);
        QWindowSystemInterface::handleScreenRefreshRateChange(QPlatformScreen::screen(), m_refreshRate);
    }
}

QPixmap QXcbScreen::grabWindow(WId window, int x, int y, int width, int height) const
{
    if (width == 0 || height == 0)
        return QPixmap();

    // TODO: handle multiple screens
    QXcbScreen *screen = const_cast<QXcbScreen *>(this);
    xcb_window_t root = screen->root();

    if (window == 0)
        window = root;

    xcb_get_geometry_cookie_t geometry_cookie = xcb_get_geometry_unchecked(xcb_connection(), window);

    xcb_get_geometry_reply_t *reply =
        xcb_get_geometry_reply(xcb_connection(), geometry_cookie, NULL);

    if (!reply) {
        return QPixmap();
    }

    if (width < 0)
        width = reply->width - x;
    if (height < 0)
        height = reply->height - y;

    geometry_cookie = xcb_get_geometry_unchecked(xcb_connection(), root);
    xcb_get_geometry_reply_t *root_reply =
        xcb_get_geometry_reply(xcb_connection(), geometry_cookie, NULL);

    if (!root_reply) {
        free(reply);
        return QPixmap();
    }

    if (reply->depth == root_reply->depth) {
        // if the depth of the specified window and the root window are the
        // same, grab pixels from the root window (so that we get the any
        // overlapping windows and window manager frames)

        // map x and y to the root window
        xcb_translate_coordinates_cookie_t translate_cookie =
            xcb_translate_coordinates_unchecked(xcb_connection(), window, root, x, y);

        xcb_translate_coordinates_reply_t *translate_reply =
            xcb_translate_coordinates_reply(xcb_connection(), translate_cookie, NULL);

        if (!translate_reply) {
            free(reply);
            free(root_reply);
            return QPixmap();
        }

        x = translate_reply->dst_x;
        y = translate_reply->dst_y;

        window = root;

        free(translate_reply);
        free(reply);
        reply = root_reply;
    } else {
        free(root_reply);
        root_reply = 0;
    }

    xcb_get_window_attributes_reply_t *attributes_reply =
        xcb_get_window_attributes_reply(xcb_connection(), xcb_get_window_attributes_unchecked(xcb_connection(), window), NULL);

    if (!attributes_reply) {
        free(reply);
        return QPixmap();
    }

    const xcb_visualtype_t *visual = screen->visualForId(attributes_reply->visual);
    free(attributes_reply);

    xcb_pixmap_t pixmap = xcb_generate_id(xcb_connection());
    xcb_create_pixmap(xcb_connection(), reply->depth, pixmap, window, width, height);

    uint32_t gc_value_mask = XCB_GC_SUBWINDOW_MODE;
    uint32_t gc_value_list[] = { XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS };

    xcb_gcontext_t gc = xcb_generate_id(xcb_connection());
    xcb_create_gc(xcb_connection(), gc, pixmap, gc_value_mask, gc_value_list);

    xcb_copy_area(xcb_connection(), window, pixmap, gc, x, y, 0, 0, width, height);

    QPixmap result = qt_xcb_pixmapFromXPixmap(connection(), pixmap, width, height, reply->depth, visual);

    free(reply);
    xcb_free_gc(xcb_connection(), gc);
    xcb_free_pixmap(xcb_connection(), pixmap);

    return result;
}

static bool parseXftInt(const QByteArray& stringValue, int *value)
{
    Q_ASSERT(value != 0);
    bool ok;
    *value = stringValue.toInt(&ok);
    return ok;
}

static QFontEngine::HintStyle parseXftHintStyle(const QByteArray& stringValue)
{
    if (stringValue == "hintfull")
        return QFontEngine::HintFull;
    else if (stringValue == "hintnone")
        return QFontEngine::HintNone;
    else if (stringValue == "hintmedium")
        return QFontEngine::HintMedium;
    else if (stringValue == "hintslight")
        return QFontEngine::HintLight;

    return QFontEngine::HintStyle(-1);
}

static QFontEngine::SubpixelAntialiasingType parseXftRgba(const QByteArray& stringValue)
{
    if (stringValue == "none")
        return QFontEngine::Subpixel_None;
    else if (stringValue == "rgb")
        return QFontEngine::Subpixel_RGB;
    else if (stringValue == "bgr")
        return QFontEngine::Subpixel_BGR;
    else if (stringValue == "vrgb")
        return QFontEngine::Subpixel_VRGB;
    else if (stringValue == "vbgr")
        return QFontEngine::Subpixel_VBGR;

    return QFontEngine::SubpixelAntialiasingType(-1);
}

bool QXcbScreen::xResource(const QByteArray &identifier,
                           const QByteArray &expectedIdentifier,
                           QByteArray& stringValue)
{
    if (identifier.startsWith(expectedIdentifier)) {
        stringValue = identifier.mid(expectedIdentifier.size());
        return true;
    }
    return false;
}

void QXcbScreen::readXResources()
{
    int offset = 0;
    QByteArray resources;
    while(1) {
        xcb_get_property_reply_t *reply =
            xcb_get_property_reply(xcb_connection(),
                xcb_get_property_unchecked(xcb_connection(), false, screen()->root,
                                 XCB_ATOM_RESOURCE_MANAGER,
                                 XCB_ATOM_STRING, offset/4, 8192), NULL);
        bool more = false;
        if (reply && reply->format == 8 && reply->type == XCB_ATOM_STRING) {
            resources += QByteArray((const char *)xcb_get_property_value(reply), xcb_get_property_value_length(reply));
            offset += xcb_get_property_value_length(reply);
            more = reply->bytes_after != 0;
        }

        if (reply)
            free(reply);

        if (!more)
            break;
    }

    QList<QByteArray> split = resources.split('\n');
    for (int i = 0; i < split.size(); ++i) {
        const QByteArray &r = split.at(i);
        int value;
        QByteArray stringValue;
        if (xResource(r, "Xft.dpi:\t", stringValue)) {
            if (parseXftInt(stringValue, &value))
                m_forcedDpi = value;
        } else if (xResource(r, "Xft.hintstyle:\t", stringValue)) {
            m_hintStyle = parseXftHintStyle(stringValue);
        } else if (xResource(r, "Xft.antialias:\t", stringValue)) {
            if (parseXftInt(stringValue, &value))
                m_antialiasingEnabled = value;
        } else if (xResource(r, "Xft.rgba:\t", stringValue)) {
            m_subpixelType = parseXftRgba(stringValue);
        }
    }
}

QXcbXSettings *QXcbScreen::xSettings() const
{
    return m_virtualDesktop->xSettings();
}

static inline void formatRect(QDebug &debug, const QRect r)
{
    debug << r.width() << 'x' << r.height()
        << forcesign << r.x() << r.y() << noforcesign;
}

static inline void formatSizeF(QDebug &debug, const QSizeF s)
{
    debug << s.width() << 'x' << s.height() << "mm";
}

Q_XCB_EXPORT QDebug operator<<(QDebug debug, const QXcbScreen *screen)
{
    const QDebugStateSaver saver(debug);
    debug.nospace();
    debug << "QXcbScreen(" << (const void *)screen;
    if (screen) {
        debug << fixed << qSetRealNumberPrecision(1);
        debug << ", name=" << screen->name();
        debug << ", geometry=";
        formatRect(debug, screen->geometry());
        debug << ", availableGeometry=";
        formatRect(debug, screen->availableGeometry());
        debug << ", devicePixelRatio=" << screen->devicePixelRatio();
        debug << ", logicalDpi=" << screen->logicalDpi();
        debug << ", physicalSize=";
        formatSizeF(debug, screen->physicalSize());
        // TODO 5.6 if (debug.verbosity() > 2) {
        debug << ", screenNumber=" << screen->screenNumber();
        debug << ", virtualSize=" << screen->virtualSize().width() << "x" << screen->virtualSize().height() << " (";
        formatSizeF(debug, screen->virtualSize());
        debug << "), nativeGeometry=";
        formatRect(debug, screen->nativeGeometry());
        debug << ", orientation=" << screen->orientation();
        debug << ", depth=" << screen->depth();
        debug << ", refreshRate=" << screen->refreshRate();
        debug << ", root=" << hex << screen->root();
        debug << ", windowManagerName=" << screen->windowManagerName();
    }
    debug << ')';
    return debug;
}

QT_END_NAMESPACE
