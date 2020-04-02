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

/*

TODO:
-places panel does not respond to view location changes 
-detect icon size for places panel

BUGS:
-(konq bug) sftp cannot save file being edited, because: "A file named sftp://hostname/path/to/file already exists."
	maybe: https://phabricator.kde.org/D23384 , or https://phabricator.kde.org/D15318
-(konq bug) loading session from cmdline causes crash, but not when konq is loaded fresh

*/



#include "tree_module.h"
#include <konq_events.h>

#include <KLocalizedString>
#include <kpluginfactory.h>

#include <QAction>
#include <QKeyEvent>
#include <QDir>

#include <QHeaderView>


KonqSideBarTreeModule::KonqSideBarTreeModule(QWidget *parent,
        const KConfigGroup &configGroup)
    : KonqSidebarModule(parent, configGroup)
{
    m_initURL = cleanupURL(configGroup.readPathEntry("URL", QString())); // because the .desktop file url might be "~"
    treeView = new QTreeView(parent);
    treeView->setHeaderHidden(true);
    treeView->header()->setStretchLastSection(false);
    treeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    model = new KDirModel(this);
    sorted_model = new KDirSortFilterProxyModel(this);
    sorted_model->setSortFoldersFirst(true);
    sorted_model->setSourceModel(model); 
    model->dirLister()->setDirOnlyMode(true);

#if KDIRMODEL_HAS_ROOT_NODE
    if (m_initURL.scheme() == "file") {
        model->openUrl(m_initURL, KDirModel::ShowRoot);
    } else {
        model->dirLister()->openUrl(m_initURL, KDirLister::Keep);
    }
#else
    model->dirLister()->openUrl(m_initURL, KDirLister::Keep);
#endif

    treeView->setModel(sorted_model);

    treeView->setColumnHidden(1, true);
    treeView->setColumnHidden(2, true);
    treeView->setColumnHidden(3, true);
    treeView->setColumnHidden(4, true);
    treeView->setColumnHidden(5, true);
    treeView->setColumnHidden(6, true);

    model->expandToUrl(m_initURL); // KDir is async, we'll just have to wait for slotKDirCompleted()
    connect(model, &KDirModel::expand,
            this, &KonqSideBarTreeModule::slotKDirCompleted_setRootIndex);
        
    QItemSelectionModel *selectionModel = treeView->selectionModel();
    connect(selectionModel, &QItemSelectionModel::selectionChanged,
            this, &KonqSideBarTreeModule::slotSelectionChanged);
}

void KonqSideBarTreeModule::customEvent(QEvent *ev) // active view has changed
{
    if (KParts::PartActivateEvent::test(ev)) {
        KParts::ReadOnlyPart* rpart = static_cast<KParts::ReadOnlyPart *>( static_cast<KParts::PartActivateEvent *>(ev)->part() ); // is it possible for "ev" or "part()" to ever be null?
        if (!rpart->url().isEmpty()) {
            setSelection(rpart->url());
        }
    }
}

QUrl KonqSideBarTreeModule::cleanupURL(const QString &dirtyURL)
{
    return cleanupURL(QUrl(dirtyURL));
}

QUrl KonqSideBarTreeModule::cleanupURL(const QUrl &dirtyURL)
{
    if (!dirtyURL.isValid()) {
        return dirtyURL;
    }
    QUrl url = dirtyURL;
    if (url.isRelative()) {
        url.setScheme("file");
        if (url.path() == "~") {
            if (!QDir::homePath().endsWith("/")) {
                url.setPath(QDir::homePath().append("/"));
            } else {
                url.setPath(QDir::homePath());
            }
        }
    }
    return url;
}

KonqSideBarTreeModule::~KonqSideBarTreeModule()
{
}

QWidget *KonqSideBarTreeModule::getWidget()
{
    return treeView;
}

void KonqSideBarTreeModule::handleURL(const QUrl &url)
{
    QUrl handleURL = url;
    
    if (handleURL.scheme().isNull()) {
        setSelectionIndex(QModelIndex());
        m_lastURL = QUrl("");
        return;
    }

    m_lastURL = handleURL;
    setSelection(handleURL);
}

void KonqSideBarTreeModule::setSelection(const QUrl &target_url, bool do_openURLreq) // do_openURLreq=true)
{
    QModelIndex index = sorted_model->mapFromSource(model->indexForUrl(target_url));

    m_lastURL = target_url;

#if KDIRMODEL_HAS_ROOT_NODE
    if (!index.isValid() && target_url.scheme() == m_initURL.scheme()) {
#else
    if (!index.isValid() && target_url.scheme() == m_initURL.scheme() && target_url != QUrl::fromLocalFile("/")) {
#endif
        if (do_openURLreq) {
            // treeView->setCursor(Qt::WaitCursor);
            connect(model, &KDirModel::expand,
                this,  &KonqSideBarTreeModule::slotKDirCompleted_setSelection   );
            model->expandToUrl(target_url); // KDir is async, we'll just have to wait for slotKDirCompleted_setSelection()          
        }
    }

    setSelectionIndex(index);
}

void KonqSideBarTreeModule::setSelectionIndex(const QModelIndex &index)
{
    if (index == treeView->selectionModel()->currentIndex()) {
        return;
    }
    treeView->expand(index);
    treeView->scrollTo(index);
    treeView->setCurrentIndex(index);
}

void KonqSideBarTreeModule::slotSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected) 
{
    QModelIndex index = treeView->selectionModel()->currentIndex();

    QUrl urlFromIndex = getUrlFromIndex(index);
    if (index.isValid() && m_lastURL != urlFromIndex) {
        emit openUrlRequest(urlFromIndex);
    }
}


// needed because KDirModel is async
void KonqSideBarTreeModule::slotKDirCompleted_setRootIndex()
{
    QModelIndex index = getIndexFromUrl(m_initURL);
    if (index.isValid()) {
        disconnect(model, &KDirModel::expand,
            this, &KonqSideBarTreeModule::slotKDirCompleted_setRootIndex);
        treeView->setRootIndex(index.parent());
        treeView->expand(index);
    }
}

void KonqSideBarTreeModule::slotKDirCompleted_setSelection(const QModelIndex &index)
{
    QUrl urlFromIndex = getUrlFromIndex(index); // these are only going to be valid
    if (urlFromIndex == m_lastURL) {
        disconnect(model, &KDirModel::expand,
            this, &KonqSideBarTreeModule::slotKDirCompleted_setSelection);
        setSelection(m_lastURL, false);
        // treeView->setCursor(Qt::ArrowCursor);
    }
}


// resolves index to the correct model (due to use of KDirSortFilterProxyModel)
QModelIndex KonqSideBarTreeModule::resolveIndex(const QModelIndex &index)
{
    if (index.isValid() && index.model() != model && model != nullptr) {
        return static_cast<const KDirSortFilterProxyModel*>(index.model())->mapToSource(index);
    } else {
        return index;
    }
}

QUrl KonqSideBarTreeModule::getUrlFromIndex(const QModelIndex &index)
{
    QUrl resolvedUrl;

    if (index.isValid()) {
        KFileItem itemForIndex = model->itemForIndex(resolveIndex(index));
        if (!itemForIndex.isNull()) {
            resolvedUrl = itemForIndex.url();
        }
    }

    return resolvedUrl;
}

QModelIndex KonqSideBarTreeModule::getIndexFromUrl(const QUrl &url)
{
    return sorted_model->mapFromSource(model->indexForUrl(url));
}


class KonqSidebarTreePlugin : public KonqSidebarPlugin
{
public:
    KonqSidebarTreePlugin(QObject *parent, const QVariantList &args)
        : KonqSidebarPlugin(parent, args) {}
    virtual ~KonqSidebarTreePlugin() {}

    virtual KonqSidebarModule *createModule(QWidget *parent,
                                            const KConfigGroup &configGroup,
                                            const QString &desktopname,
                                            const QVariant &unused) override
    {
        Q_UNUSED(desktopname);
        Q_UNUSED(unused);

        return new KonqSideBarTreeModule(parent, configGroup);
    }

    virtual QList<QAction *> addNewActions(QObject *parent,
                                           const QList<KConfigGroup> &existingModules,
                                           const QVariant &unused) override
    {
        Q_UNUSED(existingModules);
        Q_UNUSED(unused);
        QAction *action = new QAction(parent);
        action->setText(i18nc("@action:inmenu Add", "Tree Sidebar Module"));
        action->setIcon(QIcon::fromTheme("folder-favorites"));
        return QList<QAction *>() << action;
    }

    virtual QString templateNameForNewModule(const QVariant &actionData,
            const QVariant &unused) const override
    {
        Q_UNUSED(actionData);
        Q_UNUSED(unused);
        return QString::fromLatin1("treesidebarplugin%1.desktop");
    }

    virtual bool createNewModule(const QVariant &actionData,
                                 KConfigGroup &configGroup,
                                 QWidget *parentWidget,
                                 const QVariant &unused) override
    {
        Q_UNUSED(actionData);
        Q_UNUSED(parentWidget);
        Q_UNUSED(unused);
        configGroup.writeEntry("Type", "Link");
        configGroup.writeEntry("Icon", "folder-favorites");
        configGroup.writeEntry("Name", i18nc("@title:tab", "Tree"));
        configGroup.writeEntry("X-KDE-KonqSidebarModule", "konqsidebar_tree");
        return true;
    }
};

K_PLUGIN_FACTORY(KonqSidebarTreePluginFactory, registerPlugin<KonqSidebarTreePlugin>();)
// K_EXPORT_PLUGIN(KonqSidebarTreePluginFactory())

#include "tree_module.moc"
