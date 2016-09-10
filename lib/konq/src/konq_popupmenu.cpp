/* This file is part of the KDE project
   Copyright (C) 1998-2008 David Faure <faure@kde.org>
   Copyright (C) 2001 Holger Freyther <freyther@yahoo.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "konq_popupmenu.h"

#include <KActionCollection>

#include <kfileitemlistproperties.h>
#include "konq_copytomenu.h"
#include "kfileitemactions.h"
#include "kabstractfileitemactionplugin.h"
#include "kpropertiesdialog.h"

#include <klocalizedstring.h>
#include <kbookmarkmanager.h>
#include <kbookmarkdialog.h>
#include <krun.h>
#include <kprotocolmanager.h>
#include <knewfilemenu.h>
#include <kmimetypetrader.h>
#include <kconfiggroup.h>
#include <KSharedConfig>
#include <kdesktopfile.h>
#include <kauthorized.h>
#include <kacceleratormanager.h>
#include <KIO/EmptyTrashJob>
#include <KIO/JobUiDelegate>
#include <KIO/RestoreJob>
#include <KJobWidgets>
#include <KJobUiDelegate>
#include <KMimeTypeEditor>
#include <KPluginMetaData>

#include <QIcon>
#include <QFileInfo>
#include <QMimeDatabase>

/*
 Test cases:
  iconview file: background
  iconview file: file (with and without servicemenus)
  iconview file: directory
  iconview remote protocol (e.g. ftp: or fish:)
  iconview trash:/
  sidebar directory tree
  sidebar Devices / Hard Disc
  khtml background
  khtml link
  khtml image (www.kde.org RMB on K logo)
  khtmlimage (same as above, then choose View image, then RMB)
  selected text in khtml
  embedded katepart
  folder on the desktop
  trash link on the desktop
  trashed file or directory
  application .desktop file
 Then the same after uninstalling kdeaddons/konq-plugins (arkplugin in particular)
*/

class KonqPopupMenuPrivate
{
public:
    KonqPopupMenuPrivate(KonqPopupMenu *qq, KActionCollection &actions, QWidget *parentWidget)
        : q(qq),
          m_parentWidget(parentWidget),
          m_popupFlags(KonqPopupMenu::DefaultPopupItems),
          m_pMenuNew(0),
          m_copyToMenu(parentWidget),
          m_bookmarkManager(0),
          m_actions(actions)
    {
    }

    ~KonqPopupMenuPrivate()
    {
        qDeleteAll(m_ownActions);
    }

    void addNamedAction(const char *name);
    void addGroup(KonqPopupMenu::ActionGroup group);
    void addPlugins();
    void populate();
    void aboutToShow();

    void slotPopupNewDir();
    void slotPopupNewView();
    void slotPopupEmptyTrashBin();
    void slotConfigTrashBin();
    void slotPopupRestoreTrashedItems();
    void slotPopupAddToBookmark();
    void slotPopupMimeType();
    void slotPopupProperties();
    void slotShowOriginalFile();

    KonqPopupMenu *q;
    QWidget *m_parentWidget;
    QString m_urlTitle;
    KonqPopupMenu::Flags m_popupFlags;
    KNewFileMenu *m_pMenuNew;
    QUrl m_sViewURL;
    KFileItemListProperties m_popupItemProperties;
    KFileItemActions m_menuActions;
    KonqCopyToMenu m_copyToMenu;
    KBookmarkManager *m_bookmarkManager;
    KActionCollection &m_actions;
    QList<QAction *> m_ownActions;
    KonqPopupMenu::ActionGroupMap m_actionGroups;
};

//////////////////

KonqPopupMenu::KonqPopupMenu(const KFileItemList &items,
                             const QUrl &viewURL,
                             KActionCollection &actions,
                             Flags popupFlags,
                             QWidget *parentWidget)
    : QMenu(parentWidget),
      d(new KonqPopupMenuPrivate(this, actions, parentWidget))
{
    d->m_sViewURL = viewURL;
    d->m_popupItemProperties.setItems(items);
    d->m_menuActions.setParentWidget(parentWidget);
    d->m_popupFlags = popupFlags;

    connect(this, &QMenu::aboutToShow, this, [this]() { d->aboutToShow(); });
}

void KonqPopupMenuPrivate::addNamedAction(const char *name)
{
    QAction *act = m_actions.action(QString::fromLatin1(name));
    if (act) {
        q->addAction(act);
    }
}

void KonqPopupMenuPrivate::aboutToShow()
{
    populate();
    KAcceleratorManager::manage(q);
}

void KonqPopupMenuPrivate::populate()
{
    Q_ASSERT(m_popupItemProperties.items().count() >= 1);

    bool bTrashIncluded = false;

    const KFileItemList lstItems = m_popupItemProperties.items();
    KFileItemList::const_iterator it = lstItems.constBegin();
    const KFileItemList::const_iterator kend = lstItems.constEnd();
    for (; it != kend; ++it) {
        const QUrl url = (*it).url();
        if (!bTrashIncluded && ((url.scheme() == QLatin1String("trash") && url.path().length() <= 1))) {
            bTrashIncluded = true;
        }
    }

    const bool isDirectory = m_popupItemProperties.isDirectory();
    const bool sReading = m_popupItemProperties.supportsReading();
    bool sDeleting = (m_popupFlags & KonqPopupMenu::NoDeletion) == 0
                     && m_popupItemProperties.supportsDeleting();
    const bool sWriting = m_popupItemProperties.supportsWriting();
    const bool sMoving = sDeleting && m_popupItemProperties.supportsMoving();

    QUrl url = m_sViewURL.adjusted(QUrl::NormalizePathSegments);

    bool isTrashLink     = false;
    bool isCurrentTrash = false;
    bool currentDir     = false;
    bool isSymLink = false;
    bool isSymLinkInSameDir = false; // true for "ln -s foo bar", false for links to foo/sub or /foo

    //check if url is current directory
    if (lstItems.count() == 1) {
        KFileItem firstPopupItem(lstItems.first());
        if (firstPopupItem.isLink()) {
            isSymLink = true;
            isSymLinkInSameDir = !firstPopupItem.linkDest().contains(QLatin1Char('/'));
        }
        QUrl firstPopupURL(firstPopupItem.url().adjusted(QUrl::NormalizePathSegments));
        //kDebug(1203) << "View path is " << url.url();
        //kDebug(1203) << "First popup path is " << firstPopupURL.url();
        currentDir = (firstPopupURL.matches(url, QUrl::StripTrailingSlash));
        if (firstPopupItem.isDesktopFile()) {
            KDesktopFile desktopFile(firstPopupItem.localPath());
            const KConfigGroup cfg = desktopFile.desktopGroup();
            isTrashLink = cfg.readEntry("Type") == QLatin1String("Link")
                          && cfg.readEntry("URL") == QLatin1String("trash:/");
        }

        if (isTrashLink) {
            sDeleting = false;
        }

        // isCurrentTrash: popup on trash:/ itself, or on the trash.desktop link
        isCurrentTrash = (firstPopupURL.scheme() == QLatin1String("trash") && firstPopupURL.path().length() <= 1)
                         || isTrashLink;
    }

    const bool isIntoTrash = (url.scheme() == QLatin1String("trash")) && !isCurrentTrash; // trashed file, not trash:/ itself

    const bool bIsLink  = (m_popupFlags & KonqPopupMenu::IsLink);

    //kDebug() << "isLocal=" << isLocal << " url=" << url << " isCurrentTrash=" << isCurrentTrash << " isIntoTrash=" << isIntoTrash << " bTrashIncluded=" << bTrashIncluded;

    //////////////////////////////////////////////////////////////////////////

    addGroup(KonqPopupMenu::TopActions); // used e.g. for ShowMenuBar. includes a separator at the end

    QAction *act;

    QAction *actNewWindow = 0;

#if 0 // TODO in the desktop code itself.
    if ((flags & KonqPopupMenu::ShowProperties) && isOnDesktop &&
            !KAuthorized::authorizeAction("editable_desktop_icons")) {
        flags &= ~KonqPopupMenu::ShowProperties; // remove flag
    }
#endif

    // Either 'newview' is in the actions we're given (probably in the tabhandling group)
    // or we need to insert it ourselves (e.g. for the desktop).
    // In the first case, actNewWindow must remain 0.
    if (((m_popupFlags & KonqPopupMenu::ShowNewWindow) != 0) && sReading) {
        const QString openStr = i18n("&Open");
        actNewWindow = new QAction(m_parentWidget /*for status tips*/);
        m_ownActions.append(actNewWindow);
        actNewWindow->setIcon(QIcon::fromTheme(QStringLiteral("window-new")));
        actNewWindow->setText(openStr);
        QObject::connect(actNewWindow, &QAction::triggered, [this]() {
            slotPopupNewView();
        });
    }

    if (isDirectory && sWriting && !isCurrentTrash) { // A dir, and we can create things into it
        const bool mkdirRequested = m_popupFlags & KonqPopupMenu::ShowCreateDirectory;
        if ((currentDir || mkdirRequested) && m_pMenuNew) { // Current dir -> add the "new" menu
            // As requested by KNewFileMenu :
            m_pMenuNew->checkUpToDate();
            m_pMenuNew->setPopupFiles(m_popupItemProperties.urlList());

            q->addAction(m_pMenuNew);
            q->addSeparator();
        } else if (mkdirRequested) {
            QAction *actNewDir = new QAction(m_parentWidget);
            m_ownActions.append(actNewDir);
            actNewDir->setIcon(QIcon::fromTheme(QStringLiteral("folder-new")));
            actNewDir->setText(i18n("Create &Folder..."));
            QObject::connect(actNewDir, &QAction::triggered, [this]() {
                slotPopupNewDir();
            });
            q->addAction(actNewDir);
            q->addSeparator();
        }
    } else if (isIntoTrash) {
        // Trashed item, offer restoring
        act = new QAction(m_parentWidget /*for status tips*/);
        m_ownActions.append(act);
        act->setText(i18n("&Restore"));
        //PORT QT5 act->setHelpText(i18n("Restores this file or directory, back to the location where it was deleted from initially"));
        QObject::connect(act, &QAction::triggered, [this]() {
            slotPopupRestoreTrashedItems();
        });
        q->addAction(act);
    }

    if (m_popupFlags & KonqPopupMenu::ShowNavigationItems) {
        if (m_popupFlags & KonqPopupMenu::ShowUp) {
            addNamedAction("go_up");
        }
        addNamedAction("go_back");
        addNamedAction("go_forward");
        if (m_popupFlags & KonqPopupMenu::ShowReload) {
            addNamedAction("reload");
        }
        q->addSeparator();
    }

    if (!currentDir && isSymLink && !isSymLinkInSameDir) {
        // #65151: offer to open the target's parent dir
        act = new QAction(m_parentWidget);
        m_ownActions.append(act);
        act->setText(isDirectory ? i18n("Show Original Directory") : i18n("Show Original File"));
        //PORT TO QT5 act->setHelpText(i18n("Opens a new file manager window showing the target of this link, in its parent directory."));
        QObject::connect(act, &QAction::triggered, [this]() {
            slotShowOriginalFile();
        });
        q->addAction(act);
    }

    // "open in new window" is either provided by us, or by the tabhandling group
    if (actNewWindow) {
        q->addAction(actNewWindow);
        q->addSeparator();
    }
    addGroup(KonqPopupMenu::TabHandlingActions);   // includes a separator at the end

    if (m_popupFlags & KonqPopupMenu::ShowUrlOperations) {
        if (!currentDir && sReading) {
            if (sDeleting) {
                addNamedAction("cut");
            }
            addNamedAction("copy");
        }

        if (isDirectory && sWriting) {
            if (currentDir) {
                addNamedAction("paste");
            } else {
                addNamedAction("pasteto");
            }
        }
    }
    if (isCurrentTrash) {
        act = new QAction(m_parentWidget);
        m_ownActions.append(act);
        act->setIcon(QIcon::fromTheme(QStringLiteral("trash-empty")));
        act->setText(i18n("&Empty Trash Bin"));
        KConfig trashConfig(QStringLiteral("trashrc"), KConfig::SimpleConfig);
        act->setEnabled(!trashConfig.group("Status").readEntry("Empty", true));
        QObject::connect(act, &QAction::triggered, [this]() {
            slotPopupEmptyTrashBin();
        });
        q->addAction(act);
    }
    if (isCurrentTrash) {
        act = new QAction(m_parentWidget);
        m_ownActions.append(act);
        act->setIcon(QIcon::fromTheme(QStringLiteral("trash-empty")));
        act->setText(i18n("&Configure Trash Bin"));
        QObject::connect(act, &QAction::triggered, [this]() {
            slotConfigTrashBin();
        });
        q->addAction(act);
    }

    // This is used by KHTML, see khtml_popupmenu.rc (copy, selectAll, searchProvider etc.)
    // and by DolphinPart (rename, trash, delete)
    addGroup(KonqPopupMenu::EditActions);

    if (m_popupFlags & KonqPopupMenu::ShowTextSelectionItems) {
        // OK, we have to stop here.

        // Anything else that is provided by the part
        addGroup(KonqPopupMenu::CustomActions);
        return;
    }

    if (!isCurrentTrash && !isIntoTrash && (m_popupFlags & KonqPopupMenu::ShowBookmark)) {
        QString caption;
        if (currentDir) {
            const bool httpPage = m_sViewURL.scheme().startsWith(QLatin1String("http"), Qt::CaseInsensitive);
            if (httpPage) {
                caption = i18n("&Bookmark This Page");
            } else {
                caption = i18n("&Bookmark This Location");
            }
        } else if (isDirectory) {
            caption = i18n("&Bookmark This Folder");
        } else if (bIsLink) {
            caption = i18n("&Bookmark This Link");
        } else {
            caption = i18n("&Bookmark This File");
        }

        act = new QAction(m_parentWidget);
        m_ownActions.append(act);
        act->setObjectName(QLatin1String("bookmark_add"));   // for unittest
        act->setIcon(QIcon::fromTheme(QStringLiteral("bookmark-new")));
        act->setText(caption);
        QObject::connect(act, &QAction::triggered, [this]() {
            slotPopupAddToBookmark();
        });
        if (lstItems.count() > 1) {
            act->setEnabled(false);
        }
        if (KAuthorized::authorizeAction(QStringLiteral("bookmarks"))) {
            q->addAction(act);
        }
        if (bIsLink) {
            addGroup(KonqPopupMenu::LinkActions);    // see khtml
        }
    }

    // "Open With" actions

    m_menuActions.setItemListProperties(m_popupItemProperties);

    if (sReading) {
        m_menuActions.addOpenWithActionsTo(q, QStringLiteral("DesktopEntryName != 'kfmclient' and DesktopEntryName != 'kfmclient_dir' and DesktopEntryName != 'kfmclient_html'"));

        QList<QAction *> previewActions = m_actionGroups.value(KonqPopupMenu::PreviewActions);
        if (!previewActions.isEmpty()) {
            if (previewActions.count() == 1) {
                q->addAction(previewActions.first());
            } else {
                QMenu *subMenu = new QMenu(i18n("Preview In"), q);
                subMenu->menuAction()->setObjectName(QLatin1String("preview_submenu"));   // for the unittest
                q->addMenu(subMenu);
                subMenu->addActions(previewActions);
            }
        }
    }

    // Second block, builtin + user
    m_menuActions.addServiceActionsTo(q);

    q->addSeparator();

    // Use the Dolphin setting for showing the "Copy To" and "Move To" actions
    KSharedConfig::Ptr dolphin = KSharedConfig::openConfig(QStringLiteral("dolphinrc"));

    // CopyTo/MoveTo menus
    if (m_popupFlags & KonqPopupMenu::ShowUrlOperations &&
            KConfigGroup(dolphin, "General").readEntry("ShowCopyMoveMenu", false)) {

        m_copyToMenu.setItems(lstItems);
        m_copyToMenu.setReadOnly(sMoving == false);
        m_copyToMenu.addActionsTo(q);
        q->addSeparator();
    }

    if (!isCurrentTrash && !isIntoTrash && sReading &&
            (m_popupFlags & KonqPopupMenu::NoPlugins) == 0) {
        addPlugins(); // now it's time to add plugins
    }

    if ((m_popupFlags & KonqPopupMenu::ShowProperties) && KPropertiesDialog::canDisplay(lstItems)) {
        act = new QAction(m_parentWidget);
        m_ownActions.append(act);
        act->setObjectName(QLatin1String("properties"));   // for unittest
        act->setText(i18n("&Properties"));
        QObject::connect(act, &QAction::triggered, [this]() {
            slotPopupProperties();
        });
        q->addAction(act);
    }

    while (!q->actions().isEmpty() &&
            q->actions().last()->isSeparator()) {
        delete q->actions().last();
    }

    // Anything else that is provided by the part
    addGroup(KonqPopupMenu::CustomActions);

    QObject::connect(&m_menuActions, &KFileItemActions::openWithDialogAboutToBeShown, q, &KonqPopupMenu::openWithDialogAboutToBeShown);
}

KonqPopupMenu::~KonqPopupMenu()
{
    delete d;
    //kDebug(1203) << "~KonqPopupMenu leave";
}

void KonqPopupMenu::setNewFileMenu(KNewFileMenu *newMenu)
{
    d->m_pMenuNew = newMenu;
}

void KonqPopupMenu::setBookmarkManager(KBookmarkManager *manager)
{
    d->m_bookmarkManager = manager;
}

void KonqPopupMenu::setActionGroups(const KonqPopupMenu::ActionGroupMap &actionGroups)
{
    d->m_actionGroups = actionGroups;
}

void KonqPopupMenu::setURLTitle(const QString &urlTitle)
{
    d->m_urlTitle = urlTitle;
}

void KonqPopupMenuPrivate::slotPopupNewView()
{
    Q_FOREACH (const QUrl &url, m_popupItemProperties.urlList()) {
        (void) new KRun(url, m_parentWidget);
    }
}

void KonqPopupMenuPrivate::slotPopupNewDir()
{
    m_pMenuNew->createDirectory();
}

void KonqPopupMenuPrivate::slotPopupEmptyTrashBin()
{
    KIO::JobUiDelegate uiDelegate;
    uiDelegate.setWindow(m_parentWidget);
    if (uiDelegate.askDeleteConfirmation(QList<QUrl>(), KIO::JobUiDelegate::EmptyTrash, KIO::JobUiDelegate::DefaultConfirmation)) {
        KIO::Job *job = KIO::emptyTrash();
        KJobWidgets::setWindow(job, m_parentWidget);
        job->ui()->setAutoErrorHandlingEnabled(true); // or connect to the result signal
    }
}

void KonqPopupMenuPrivate::slotConfigTrashBin()
{
    KRun::run(QStringLiteral("kcmshell5 kcmtrash"), QList<QUrl>(), m_parentWidget);
}

void KonqPopupMenuPrivate::slotPopupRestoreTrashedItems()
{
    KIO::RestoreJob *job = KIO::restoreFromTrash(m_popupItemProperties.urlList());
    KJobWidgets::setWindow(job, m_parentWidget);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
}

void KonqPopupMenuPrivate::slotPopupAddToBookmark()
{
    KBookmarkGroup root;
    if (m_popupItemProperties.urlList().count() == 1) {
        const QUrl url = m_popupItemProperties.urlList().first();
        const QString title = m_urlTitle.isEmpty() ? url.toDisplayString() : m_urlTitle;
        KBookmarkDialog dlg(m_bookmarkManager, m_parentWidget);
        dlg.addBookmark(title, url, QString());
    } else {
        root = m_bookmarkManager->root();
        Q_FOREACH (const QUrl &url, m_popupItemProperties.urlList()) {
            root.addBookmark(url.toDisplayString(), url, QString());
        }
        m_bookmarkManager->emitChanged(root);
    }
}

void KonqPopupMenuPrivate::slotPopupMimeType()
{
    KMimeTypeEditor::editMimeType(m_popupItemProperties.mimeType(), m_parentWidget);
}

void KonqPopupMenuPrivate::slotPopupProperties()
{
    KPropertiesDialog::showDialog(m_popupItemProperties.items(), m_parentWidget, false);
}

void KonqPopupMenuPrivate::addGroup(KonqPopupMenu::ActionGroup group)
{
    q->addActions(m_actionGroups.value(group));
}

void KonqPopupMenuPrivate::addPlugins()
{
    QString commonMimeType = m_popupItemProperties.mimeType();
    if (commonMimeType.isEmpty()) {
        commonMimeType = QLatin1String("application/octet-stream");
    }

    const KService::List fileItemPlugins = KMimeTypeTrader::self()->query(commonMimeType, QStringLiteral("KFileItemAction/Plugin"), QStringLiteral("exist Library"));

    QSet<QString> addedPlugins;
    const KConfig config(QStringLiteral("kservicemenurc"), KConfig::NoGlobals);
    const KConfigGroup showGroup = config.group("Show");

    foreach (const auto &service, fileItemPlugins) {
        if (!showGroup.readEntry(service->desktopEntryName(), true)) {
            // The plugin has been disabled
            continue;
        }

        KAbstractFileItemActionPlugin *abstractPlugin = service->createInstance<KAbstractFileItemActionPlugin>();
        if (abstractPlugin) {
            abstractPlugin->setParent(q);
            q->addActions(abstractPlugin->actions(m_popupItemProperties, m_parentWidget));
            addedPlugins << service->desktopEntryName();
        }
    }

    const auto jsonPlugins = KPluginLoader::findPlugins(QStringLiteral("kf5/kfileitemaction"), [=](const KPluginMetaData& metaData) {
        if (!metaData.serviceTypes().contains(QStringLiteral("KFileItemAction/Plugin"))) {
            return false;
        }

        auto mimeType = QMimeDatabase().mimeTypeForName(commonMimeType);
        foreach (const auto &supportedMimeType, metaData.mimeTypes()) {
            if (mimeType.inherits(supportedMimeType)) {
                return true;
            }
        }

        return false;
    });

    foreach (const auto &jsonMetadata, jsonPlugins) {
        // The plugin has been disabled
        if (!showGroup.readEntry(jsonMetadata.pluginId(), true)) {
            continue;
        }

        // The plugin also has a .desktop file and has already been added.
        if (addedPlugins.contains(jsonMetadata.pluginId())) {
            continue;
        }

        KPluginFactory *factory = KPluginLoader(jsonMetadata.fileName()).factory();
        KAbstractFileItemActionPlugin* abstractPlugin = factory->create<KAbstractFileItemActionPlugin>();
        if (abstractPlugin) {
            abstractPlugin->setParent(q);
            q->addActions(abstractPlugin->actions(m_popupItemProperties, m_parentWidget));
            addedPlugins << jsonMetadata.pluginId();
        }
    }
}

void KonqPopupMenuPrivate::slotShowOriginalFile()
{
    const KFileItem item = m_popupItemProperties.items().first();
    QUrl destUrl = QUrl::fromLocalFile(item.linkDest());

    if (!destUrl.isValid()) {
        return;
    }

    // Now destUrl points to the target file, let's go up to parent dir
    destUrl = destUrl.adjusted(QUrl::RemoveFilename);
    KRun::runUrl(destUrl, QStringLiteral("inode/directory"), m_parentWidget);
}
