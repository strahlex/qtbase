/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtNetwork module of the Qt Toolkit.
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

#ifndef QHTTPMULTIPART_H
#define QHTTPMULTIPART_H

#include <QtCore/QSharedDataPointer>
#include <QtCore/QByteArray>
#include <QtCore/QIODevice>
#include <QtNetwork/QNetworkRequest>

QT_BEGIN_NAMESPACE


class QHttpPartPrivate;
class QHttpMultiPart;

class Q_NETWORK_EXPORT QHttpPart
{
public:
    QHttpPart();
    QHttpPart(const QHttpPart &other);
    ~QHttpPart();
#ifdef Q_COMPILER_RVALUE_REFS
    QHttpPart &operator=(QHttpPart &&other) Q_DECL_NOTHROW { swap(other); return *this; }
#endif
    QHttpPart &operator=(const QHttpPart &other);

    void swap(QHttpPart &other) Q_DECL_NOTHROW { qSwap(d, other.d); }

    bool operator==(const QHttpPart &other) const;
    inline bool operator!=(const QHttpPart &other) const
    { return !operator==(other); }

    void setHeader(QNetworkRequest::KnownHeaders header, const QVariant &value);
    void setRawHeader(const QByteArray &headerName, const QByteArray &headerValue);

    void setBody(const QByteArray &body);
    void setBodyDevice(QIODevice *device);

private:
    QSharedDataPointer<QHttpPartPrivate> d;

    friend class QHttpMultiPartIODevice;
};

Q_DECLARE_SHARED(QHttpPart)

class QHttpMultiPartPrivate;

class Q_NETWORK_EXPORT QHttpMultiPart : public QObject
{
    Q_OBJECT

public:

    enum ContentType {
        MixedType,
        RelatedType,
        FormDataType,
        AlternativeType
    };

    explicit QHttpMultiPart(QObject *parent = 0);
    explicit QHttpMultiPart(ContentType contentType, QObject *parent = 0);
    ~QHttpMultiPart();

    void append(const QHttpPart &httpPart);

    void setContentType(ContentType contentType);

    QByteArray boundary() const;
    void setBoundary(const QByteArray &boundary);

private:
    Q_DECLARE_PRIVATE(QHttpMultiPart)
    Q_DISABLE_COPY(QHttpMultiPart)

    friend class QNetworkAccessManager;
    friend class QNetworkAccessManagerPrivate;
};

QT_END_NAMESPACE

#endif // QHTTPMULTIPART_H
