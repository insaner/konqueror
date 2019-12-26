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

-Check for race condition in setSelection KDirModel

-bookmarks:/ doesn't seem to be working in the panel, but it does from the locationBar
-detect icon size for places panel

BUGS:
-(konq bug) loading session from cmdline causes crash, but not when konq is loaded fresh
-(konq bug-FIXED) higher resolution icon for window mgr
 
*/



#include "tree_module.h"
#include <konq_events.h>

#include <KLocalizedString>
#include <kpluginfactory.h>

#include <QAction>
#include <QKeyEvent>


#if KDIRMODEL
KDirModelPlus::KDirModelPlus(QObject *parent)
    : KDirModel(parent)
{}

// Re-implement hasChildren() in order to prevent needless arrows from showing in the tree
bool KDirModelPlus::hasChildren(const QModelIndex& parent) const
{
    KFileItem parentItem = itemForIndex(parent);

    if (parentItem.isDir()) {
        if (parentItem.isLocalFile()) { // I could not figure out how to make this work for other KIO schemes (eg, "applications:/", "remote:/")
           QDirIterator it(parentItem.localPath(), QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot,  QDirIterator::Subdirectories);
           return it.hasNext();
        } else {
           return true;
        }
    } else {
        return false;
    }
}
#endif


KonqSideBarTreeModule::KonqSideBarTreeModule(QWidget *parent,
        const KConfigGroup &configGroup)
    : KonqSidebarModule(parent, configGroup)
{
    m_initURL = QUrl(configGroup.readPathEntry("URL", QString()));
    treeView = new QTreeView(parent);
    treeView->setHeaderHidden(true);
        

#if KDIRMODEL
        model = new KDirModelPlus(this);

        sorted_model = new KDirSortFilterProxyModel(this);
        sorted_model->setSortFoldersFirst(true);
        sorted_model->setSourceModel(model); 
        model->dirLister()->setDirOnlyMode(true);
        if (m_initURL.scheme().isNull() || m_initURL.scheme() == "file") {
            model->dirLister()->openUrl(QUrl::fromLocalFile("/"), KDirLister::Keep);
        }
        else {
            model->dirLister()->openUrl(m_initURL, KDirLister::Keep);
        }

        treeView->setModel(sorted_model);
#else
        model = new QDirModel(this);

        model->setResolveSymlinks(false);	// NOTE FIXME: clicking on /bin/mh creates endless resolution
        model->setReadOnly(true); // Disable modifying file system
        model->setSorting(QDir::DirsFirst |
                        QDir::IgnoreCase |
                        QDir::Name);
        model->setFilter( QDir::Dirs |
                        QDir::NoDotAndDotDot);
        treeView->setModel(model);
#endif

        treeView->setColumnHidden(1, true);
        treeView->setColumnHidden(2, true);
        treeView->setColumnHidden(3, true);
#if KDIRMODEL
        treeView->setColumnHidden(4, true);
        treeView->setColumnHidden(5, true);
        treeView->setColumnHidden(6, true);
#endif

        // Set initial selection
        setSelection(m_initURL); // in KDirModel, this will have no effect, see: slotKDirCompleted
        treeView->resizeColumnToContents(0);
        
        QItemSelectionModel *selectionModel = treeView->selectionModel();
        connect(selectionModel, &QItemSelectionModel::selectionChanged,
                this, &KonqSideBarTreeModule::slotSelectionChanged);
        
#if KDIRMODEL
#else
        if (!m_initURL.scheme().isNull() && m_initURL.scheme() != "file") { // schemes other than "file" not supported by QDirModel
            model = NULL;
            treeView->setModel(model);
        }
#endif
}

void KonqSideBarTreeModule::customEvent(QEvent *ev) // active view has changed
{
    if (KonqActiveViewChangedEvent::test(ev)) {
        QUrl activeView_url = static_cast<KonqActiveViewChangedEvent *>(ev)->url();
        if (!activeView_url.isEmpty()) {
            setSelection(activeView_url, false);
        }
    }
}

KonqSideBarTreeModule::~KonqSideBarTreeModule()
{
}

QWidget *KonqSideBarTreeModule::getWidget()
{
    return treeView;
}

void KonqSideBarTreeModule::setSelection(const QString path)
{
#if KDIRMODEL
    setSelection(QUrl::fromLocalFile(path)); // QUrl
#else
    setSelection(model->index(path)); // index
#endif
}

void KonqSideBarTreeModule::setSelection(QUrl target_url, bool do_openURLreq) // do_openURLreq=true)
{
    if (target_url.isRelative()) {
        target_url.setScheme("file");
    }

#if KDIRMODEL
    QModelIndex index = sorted_model->mapFromSource(model->indexForUrl(target_url));
    if (!index.isValid() && target_url != QUrl::fromLocalFile("/")) { // NOTE: there is no node (or index) for "/" in KDirModel (yet?)
        if (do_openURLreq) {
            connect(model->dirLister(), QOverload<>::of(&KDirLister::completed),
                    this, &KonqSideBarTreeModule::slotKDirCompleted_setSelection_noOpen );
        } else {
            connect(model->dirLister(), QOverload<>::of(&KDirLister::completed),
                    this, &KonqSideBarTreeModule::slotKDirCompleted_setSelection );
        }
        model->expandToUrl(target_url); // KDir is async, we'll just have to wait for slotKDirCompleted()
        return;
    }
#else
    QModelIndex index = model->index(target_url.toLocalFile());
#endif

    m_lastURL = target_url;
    setSelection(index);
}

void KonqSideBarTreeModule::setSelection(const QModelIndex index)
{
    treeView->expand(index);
    treeView->scrollTo(index);
    treeView->setCurrentIndex(index);
}

void KonqSideBarTreeModule::handleURL(const QUrl &url)
{
    QUrl handleURL = url;

    if (handleURL.isRelative()) {
        handleURL.setScheme("file");
        if (handleURL.path() == "~") {
            handleURL.setPath(QDir::homePath());
        }
    }
    //if (handleURL.scheme() != "file") {
    if (handleURL.scheme().isNull()) {
        setSelection(QModelIndex());
        m_lastURL = QUrl("");
        return;
    }

    m_lastURL = handleURL;
    setSelection(handleURL);
}

QUrl KonqSideBarTreeModule::getLastURL() const
{
    return m_lastURL;
}

void KonqSideBarTreeModule::slotSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected) 
{
    QModelIndex index = treeView->selectionModel()->currentIndex();

    QUrl urlFromIndex = getUrlFromIndex(index);
    if (index.isValid() && m_lastURL != urlFromIndex) {
        emit openUrlRequest(urlFromIndex);
    }
}


#if KDIRMODEL
// needed because KDirModel is async
void KonqSideBarTreeModule::slotKDirCompleted_setSelection_noOpen()
{
    slotKDirCompleted_setSelection_h(false); // do not emit the openUrlRequest(), because it pollutes our history stack
}

void KonqSideBarTreeModule::slotKDirCompleted_setSelection()
{
    slotKDirCompleted_setSelection_h();
}

void KonqSideBarTreeModule::slotKDirCompleted_setSelection_h(bool do_openURLreq) // do_openURLreq=true)
{
    disconnect(model->dirLister(), QOverload<>::of(&KDirLister::completed),
               this, &KonqSideBarTreeModule::slotKDirCompleted_setSelection );
    
    QModelIndex index = treeView->selectionModel()->currentIndex();

    if (!index.isValid() || getUrlFromIndex(index) !=  m_lastURL) {
        setSelection(m_lastURL, do_openURLreq);
    }
}

// resolves index to the correct model (due to use of KDirSortFilterProxyModel)
QModelIndex KonqSideBarTreeModule::resolveIndex(const QModelIndex index)
{
    if (!index.isValid()) {
        return index;
    }

    if ( index.model() != model && model != nullptr ) {
        return static_cast<const KDirSortFilterProxyModel*>(index.model())->mapToSource(index);
    } else {
        return index;
    }
}
#endif


QUrl KonqSideBarTreeModule::getUrlFromIndex(const QModelIndex index)
{
    QUrl resolvedUrl;

    if (!index.isValid() ) {
        return resolvedUrl;
    }
    
#if KDIRMODEL
    KFileItem itemForIndex = model->itemForIndex(resolveIndex(index));
    if (!itemForIndex.isNull()) {
        resolvedUrl = itemForIndex.url();
    }
#else
    resolvedUrl = QUrl::fromLocalFile(model->filePath(index));
#endif

    return resolvedUrl;
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
