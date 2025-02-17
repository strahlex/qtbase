/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtGui module of the Qt Toolkit.
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

#ifndef QITEMSELECTIONMODEL_H
#define QITEMSELECTIONMODEL_H

#include <QtCore/qset.h>
#include <QtCore/qvector.h>
#include <QtCore/qlist.h>
#include <QtCore/qabstractitemmodel.h>

QT_BEGIN_NAMESPACE


#ifndef QT_NO_ITEMVIEWS

class Q_CORE_EXPORT QItemSelectionRange
{

public:
    inline QItemSelectionRange() {}
    inline QItemSelectionRange(const QItemSelectionRange &other)
        : tl(other.tl), br(other.br) {}
    inline QItemSelectionRange(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    explicit inline QItemSelectionRange(const QModelIndex &index)
        { tl = index; br = tl; }

    inline int top() const { return tl.row(); }
    inline int left() const { return tl.column(); }
    inline int bottom() const { return br.row(); }
    inline int right() const { return br.column(); }
    inline int width() const { return br.column() - tl.column() + 1; }
    inline int height() const { return br.row() - tl.row() + 1; }

    inline const QPersistentModelIndex &topLeft() const { return tl; }
    inline const QPersistentModelIndex &bottomRight() const { return br; }
    inline QModelIndex parent() const { return tl.parent(); }
    inline const QAbstractItemModel *model() const { return tl.model(); }

    inline bool contains(const QModelIndex &index) const
    {
        return (parent() == index.parent()
                && tl.row() <= index.row() && tl.column() <= index.column()
                && br.row() >= index.row() && br.column() >= index.column());
    }

    inline bool contains(int row, int column, const QModelIndex &parentIndex) const
    {
        return (parent() == parentIndex
                && tl.row() <= row && tl.column() <= column
                && br.row() >= row && br.column() >= column);
    }

    bool intersects(const QItemSelectionRange &other) const;
#if QT_DEPRECATED_SINCE(5, 0)
    inline QItemSelectionRange intersect(const QItemSelectionRange &other) const
        { return intersected(other); }
#endif
    QItemSelectionRange intersected(const QItemSelectionRange &other) const;


    inline bool operator==(const QItemSelectionRange &other) const
        { return (tl == other.tl && br == other.br); }
    inline bool operator!=(const QItemSelectionRange &other) const
        { return !operator==(other); }
    inline bool operator<(const QItemSelectionRange &other) const
        {
            // Comparing parents will compare the models, but if two equivalent ranges
            // in two different models have invalid parents, they would appear the same
            if (other.tl.model() == tl.model()) {
                // parent has to be calculated, so we only do so once.
                const QModelIndex topLeftParent = tl.parent();
                const QModelIndex otherTopLeftParent = other.tl.parent();
                if (topLeftParent == otherTopLeftParent) {
                    if (other.tl.row() == tl.row()) {
                        if (other.tl.column() == tl.column()) {
                            if (other.br.row() == br.row()) {
                                return br.column() < other.br.column();
                            }
                            return br.row() < other.br.row();
                        }
                        return tl.column() < other.tl.column();
                    }
                    return tl.row() < other.tl.row();
                }
                return topLeftParent < otherTopLeftParent;
            }
            return tl.model() < other.tl.model();
        }

    inline bool isValid() const
    {
        return (tl.isValid() && br.isValid() && tl.parent() == br.parent()
                && top() <= bottom() && left() <= right());
    }

    bool isEmpty() const;

    QModelIndexList indexes() const;

private:
    QPersistentModelIndex tl, br;
};
Q_DECLARE_TYPEINFO(QItemSelectionRange, Q_MOVABLE_TYPE);

inline QItemSelectionRange::QItemSelectionRange(const QModelIndex &atopLeft,
                                                const QModelIndex &abottomRight)
{ tl = atopLeft; br = abottomRight; }

class QItemSelection;
class QItemSelectionModelPrivate;

class Q_CORE_EXPORT QItemSelectionModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged STORED false DESIGNABLE false)
    Q_PROPERTY(QModelIndex currentIndex READ currentIndex NOTIFY currentChanged STORED false DESIGNABLE false)
    Q_PROPERTY(QItemSelection selection READ selection NOTIFY selectionChanged STORED false DESIGNABLE false)
    Q_PROPERTY(QModelIndexList selectedIndexes READ selectedIndexes NOTIFY selectionChanged STORED false DESIGNABLE false)

    Q_DECLARE_PRIVATE(QItemSelectionModel)

public:

    enum SelectionFlag {
        NoUpdate       = 0x0000,
        Clear          = 0x0001,
        Select         = 0x0002,
        Deselect       = 0x0004,
        Toggle         = 0x0008,
        Current        = 0x0010,
        Rows           = 0x0020,
        Columns        = 0x0040,
        SelectCurrent  = Select | Current,
        ToggleCurrent  = Toggle | Current,
        ClearAndSelect = Clear | Select
    };

    Q_DECLARE_FLAGS(SelectionFlags, SelectionFlag)
    Q_FLAG(SelectionFlags)

    explicit QItemSelectionModel(QAbstractItemModel *model = Q_NULLPTR);
    explicit QItemSelectionModel(QAbstractItemModel *model, QObject *parent);
    virtual ~QItemSelectionModel();

    QModelIndex currentIndex() const;

    Q_INVOKABLE bool isSelected(const QModelIndex &index) const;
    Q_INVOKABLE bool isRowSelected(int row, const QModelIndex &parent) const;
    Q_INVOKABLE bool isColumnSelected(int column, const QModelIndex &parent) const;

    Q_INVOKABLE bool rowIntersectsSelection(int row, const QModelIndex &parent) const;
    Q_INVOKABLE bool columnIntersectsSelection(int column, const QModelIndex &parent) const;

    bool hasSelection() const;

    QModelIndexList selectedIndexes() const;
    Q_INVOKABLE QModelIndexList selectedRows(int column = 0) const;
    Q_INVOKABLE QModelIndexList selectedColumns(int row = 0) const;
    const QItemSelection selection() const;

    // ### Qt 6: Merge these two as "QAbstractItemModel *model() const"
    const QAbstractItemModel *model() const;
    QAbstractItemModel *model();

    void setModel(QAbstractItemModel *model);

public Q_SLOTS:
    virtual void setCurrentIndex(const QModelIndex &index, QItemSelectionModel::SelectionFlags command);
    virtual void select(const QModelIndex &index, QItemSelectionModel::SelectionFlags command);
    virtual void select(const QItemSelection &selection, QItemSelectionModel::SelectionFlags command);
    virtual void clear();
    virtual void reset();

    void clearSelection();
    virtual void clearCurrentIndex();

Q_SIGNALS:
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void currentChanged(const QModelIndex &current, const QModelIndex &previous);
    void currentRowChanged(const QModelIndex &current, const QModelIndex &previous);
    void currentColumnChanged(const QModelIndex &current, const QModelIndex &previous);
    void modelChanged(QAbstractItemModel *model);

protected:
    QItemSelectionModel(QItemSelectionModelPrivate &dd, QAbstractItemModel *model);
    void emitSelectionChanged(const QItemSelection &newSelection, const QItemSelection &oldSelection);

private:
    Q_DISABLE_COPY(QItemSelectionModel)
    Q_PRIVATE_SLOT(d_func(), void _q_columnsAboutToBeRemoved(const QModelIndex&, int, int))
    Q_PRIVATE_SLOT(d_func(), void _q_rowsAboutToBeRemoved(const QModelIndex&, int, int))
    Q_PRIVATE_SLOT(d_func(), void _q_columnsAboutToBeInserted(const QModelIndex&, int, int))
    Q_PRIVATE_SLOT(d_func(), void _q_rowsAboutToBeInserted(const QModelIndex&, int, int))
    Q_PRIVATE_SLOT(d_func(), void _q_layoutAboutToBeChanged(const QList<QPersistentModelIndex> &parents = QList<QPersistentModelIndex>(), QAbstractItemModel::LayoutChangeHint hint = QAbstractItemModel::NoHint))
    Q_PRIVATE_SLOT(d_func(), void _q_layoutChanged(const QList<QPersistentModelIndex> &parents = QList<QPersistentModelIndex>(), QAbstractItemModel::LayoutChangeHint hint = QAbstractItemModel::NoHint))
};

Q_DECLARE_OPERATORS_FOR_FLAGS(QItemSelectionModel::SelectionFlags)

// dummy implentation of qHash() necessary for instantiating QList<QItemSelectionRange>::toSet() with MSVC
inline uint qHash(const QItemSelectionRange &) { return 0; }

class Q_CORE_EXPORT QItemSelection : public QList<QItemSelectionRange>
{
public:
    QItemSelection() {}
    QItemSelection(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    void select(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    bool contains(const QModelIndex &index) const;
    QModelIndexList indexes() const;
    void merge(const QItemSelection &other, QItemSelectionModel::SelectionFlags command);
    static void split(const QItemSelectionRange &range,
                      const QItemSelectionRange &other,
                      QItemSelection *result);
};

#ifndef QT_NO_DEBUG_STREAM
Q_CORE_EXPORT QDebug operator<<(QDebug, const QItemSelectionRange &);
#endif

#endif // QT_NO_ITEMVIEWS

QT_END_NAMESPACE

Q_DECLARE_METATYPE(QItemSelectionRange)
Q_DECLARE_METATYPE(QItemSelection)

#endif // QITEMSELECTIONMODEL_H
