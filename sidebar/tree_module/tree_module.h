/*
    Copyright (C) 2019 Raphael Rosch <kde-dev@insaner.com>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2 of the License or ( at
    your option ) version 3 or, at the discretion of KDE e.V. ( which shall
    act as a proxy as in section 14 of the GPLv3 ), any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#ifndef TREE_MODULE_H
#define TREE_MODULE_H

#include <konqsidebarplugin.h>

#include <QTreeView>
#include <QDebug>

#include <QUrl>
#include <QDir>


// Set to 0 to use QDirModel instead
#define KDIRMODEL 1



#if KDIRMODEL
#include <KDirModel>
#include <KDirLister>
#include <KDirSortFilterProxyModel>
#include <QDirIterator>
#else
#include <QDirModel>
#endif




#if KDIRMODEL
class KDirModelPlus : public KDirModel
{
public:
    KDirModelPlus(QObject *parent=nullptr);
    ~KDirModelPlus(){};
    bool hasChildren (const QModelIndex &parent=QModelIndex()) const override;
};
#endif



class KonqSideBarTreeModule : public KonqSidebarModule
{
    Q_OBJECT

public:
    KonqSideBarTreeModule(QWidget *parent,
                            const KConfigGroup &configGroup);
    virtual ~KonqSideBarTreeModule();

    virtual QWidget *getWidget() override;
    void handleURL(const QUrl &hand_url) override;
    QUrl getLastURL() const; 

private slots:
    void slotSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
#if KDIRMODEL
    void slotKDirCompleted_setSelection_h(bool do_openURLreq=true);
    void slotKDirCompleted_setSelection();
    void slotKDirCompleted_setSelection_noOpen();
#endif
    void customEvent(QEvent *ev) override;

private:
    void setSelection(const QString path);
    void setSelection(QUrl target_url, bool do_openURLreq=true);
    //void setSelection(const QUrl target_url);
    void setSelection(const QModelIndex index);
    QUrl getUrlFromIndex(const QModelIndex index);

    QTreeView *treeView;
    QUrl m_initURL;
    QUrl m_lastURL;
    bool m_ignoreHandle = false;

#if KDIRMODEL
    KDirModelPlus *model;
    KDirSortFilterProxyModel *sorted_model;
#else
    QDirModel *model;
#endif
    QModelIndex resolveIndex(const QModelIndex index);
};

#endif
