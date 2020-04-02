/*
 * This file is part of the KDE project.
 *
 * Copyright (C) 2008 Dirk Mueller <mueller@kde.org>
 * Copyright (C) 2008 - 2010 Urs Wolfer <uwolfer @ kde.org>
 * Copyright (C) 2009 Dawit Alemayehu <adawit@kde.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "webenginepage.h"

#include "webenginepart.h"
#include "websslinfo.h"
#include "webengineview.h"
#include "settings/webenginesettings.h"
#include "webenginepartdownloadmanager.h"
#include "webenginewallet.h"
#include <webenginepart_debug.h>

#include <QWebEngineCertificateError>
#include <QWebEngineSettings>
#include <QWebEngineProfile>

#include <KMessageBox>
#include <KRun>
#include <KLocalizedString>
#include <KShell>
#include <KAuthorized>
#include <KStringHandler>
#include <KUrlAuthorized>
#include <KSharedConfig>
#include <KIO/AuthInfo>
#include <KIO/Job>
#include <KIO/AccessManager>
#include <KIO/Scheduler>
#include <KParts/HtmlExtension>
#include <KUserTimestamp>
#include <KPasswdServerClient>

#include <QStandardPaths>
#include <QDesktopWidget>
#include <QFileDialog>

#include <QFile>
#include <QAuthenticator>
#include <QApplication>
#include <QTextDocument> // Qt::escape
#include <QNetworkReply>
#include <QWebEngineHistory>
#include <QWebEngineHistoryItem>
#include <QWebEngineDownloadItem>
#include <QUrlQuery>
#include <KConfigGroup>
//#include <QWebSecurityOrigin>
#include "utils.h"


WebEnginePage::WebEnginePage(WebEnginePart *part, QWidget *parent)
        : QWebEnginePage(parent),
         m_kioErrorCode(0),
         m_ignoreError(false),
         m_part(part),
         m_passwdServerClient(new KPasswdServerClient),
         m_wallet(nullptr)
{
    if (view())
        WebEngineSettings::self()->computeFontSizes(view()->logicalDpiY());

    //setForwardUnsupportedContent(true);

    connect(this, &QWebEnginePage::geometryChangeRequested,
            this, &WebEnginePage::slotGeometryChangeRequested);
//    connect(this, SIGNAL(unsupportedContent(QNetworkReply*)),
//            this, SLOT(slotUnsupportedContent(QNetworkReply*)));
    connect(this, &QWebEnginePage::featurePermissionRequested,
            this, &WebEnginePage::slotFeaturePermissionRequested);
    connect(this, &QWebEnginePage::loadFinished,
            this, &WebEnginePage::slotLoadFinished);
    connect(this, &QWebEnginePage::authenticationRequired,
            this, &WebEnginePage::slotAuthenticationRequired);
    if(!this->profile()->httpUserAgent().contains(QLatin1String("Konqueror")))
    {
        this->profile()->setHttpUserAgent(this->profile()->httpUserAgent() + " Konqueror (WebEnginePart)");
    }

    WebEnginePartDownloadManager::instance()->addPage(this);

    m_wallet = new WebEngineWallet(this, parent ? parent->window()->winId() : 0);
}

WebEnginePage::~WebEnginePage()
{
    //kDebug() << this;
}

const WebSslInfo& WebEnginePage::sslInfo() const
{
    return m_sslInfo;
}

void WebEnginePage::setSslInfo (const WebSslInfo& info)
{
    m_sslInfo = info;
}

static void checkForDownloadManager(QWidget* widget, QString& cmd)
{
    cmd.clear();
    KConfigGroup cfg (KSharedConfig::openConfig(QStringLiteral("konquerorrc"), KConfig::NoGlobals), "HTML Settings");
    const QString fileName (cfg.readPathEntry("DownloadManager", QString()));
    if (fileName.isEmpty())
        return;

    const QString exeName = QStandardPaths::findExecutable(fileName);
    if (exeName.isEmpty()) {
        KMessageBox::detailedSorry(widget,
                                   i18n("The download manager (%1) could not be found in your installation.", fileName),
                                   i18n("Try to reinstall it and make sure that it is available in $PATH. \n\nThe integration will be disabled."));
        cfg.writePathEntry("DownloadManager", QString());
        cfg.sync();
        return;
    }

    cmd = exeName;
}

void WebEnginePage::download(const QUrl& url, bool newWindow)
{
    // Integration with a download manager...
    if (!url.isLocalFile()) {
        QString managerExe;
        checkForDownloadManager(view(), managerExe);
        if (!managerExe.isEmpty()) {
            //kDebug() << "Calling command" << cmd;
            KRun::runCommand((managerExe + QLatin1Char(' ') + KShell::quoteArg(url.url())), view());
            return;
        }
    }
    KParts::BrowserArguments bArgs;
    bArgs.setForcesNewWindow(newWindow);
    emit part()->browserExtension()->openUrlRequest(url, KParts::OpenUrlArguments(), bArgs);
}

QWebEnginePage *WebEnginePage::createWindow(WebWindowType type)
{
    //qCDebug(WEBENGINEPART_LOG) << "window type:" << type;
    // Crete an instance of NewWindowPage class to capture all the
    // information we need to create a new window. See documentation of
    // the class for more information...
    NewWindowPage* page = new NewWindowPage(type, part());
    return page;
}

// Returns true if the scheme and domain of the two urls match...
static bool domainSchemeMatch(const QUrl& u1, const QUrl& u2)
{
    if (u1.scheme() != u2.scheme())
        return false;

    QStringList u1List = u1.host().split(QL1C('.'), QString::SkipEmptyParts);
    QStringList u2List = u2.host().split(QL1C('.'), QString::SkipEmptyParts);

    if (qMin(u1List.count(), u2List.count()) < 2)
        return false;  // better safe than sorry...

    while (u1List.count() > 2)
        u1List.removeFirst();

    while (u2List.count() > 2)
        u2List.removeFirst();

    return (u1List == u2List);
}

bool WebEnginePage::acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame)
{
    if (m_urlLoadedByPart != url) {
        m_urlLoadedByPart = QUrl();
        
        //Don't open local files using WebEnginePart except if configured to do so by the user. For example
        //for example, this ensures that the "Home" link in the introduction page is opened in Dolphin part 
        //(or whichever part the user has chosen to open directories instead of WebEnginePart
        if (url.isLocalFile()) {
            emit m_part->browserExtension()->openUrlRequest(url);
            return false;
        }
    }
//     qCDebug(WEBENGINEPART_LOG) << url << "type=" << type;
    QUrl reqUrl(url);

    // Handle "mailto:" url here...
    if (handleMailToUrl(reqUrl, type))
        return false;

    const bool isTypedUrl = property("NavigationTypeUrlEntered").toBool();

    /*
      NOTE: We use a dynamic QObject property called "NavigationTypeUrlEntered"
      to distinguish between requests generated by user entering a url vs those
      that were generated programmatically through javascript (AJAX requests).
    */
    if (isMainFrame && isTypedUrl)
      setProperty("NavigationTypeUrlEntered", QVariant());

    // inPage requests are those generarted within the current page through
    // link clicks, javascript queries, and button clicks (form submission).
    bool inPageRequest = true;
    switch (type) {
        case QWebEnginePage::NavigationTypeFormSubmitted:
            if (!checkFormData(url))
               return false;
            m_wallet->saveFormData(this);
            break;
#if 0
        case QWebEnginePage::NavigationTypeFormResubmitted:
            if (!checkFormData(request))
                return false;
            if (KMessageBox::warningContinueCancel(view(),
                            i18n("<qt><p>To display the requested web page again, "
                                  "the browser needs to resend information you have "
                                  "previously submitted.</p>"
                                  "<p>If you were shopping online and made a purchase, "
                                  "click the Cancel button to prevent a duplicate purchase."
                                  "Otherwise, click the Continue button to display the web"
                                  "page again.</p>"),
                            i18n("Resubmit Information")) == KMessageBox::Cancel) {
                return false;
            }
            break;
#endif
        case QWebEnginePage::NavigationTypeBackForward:
            // If history navigation is locked, ignore all such requests...
            if (property("HistoryNavigationLocked").toBool()) {
                setProperty("HistoryNavigationLocked", QVariant());
                qCDebug(WEBENGINEPART_LOG) << "Rejected history navigation because 'HistoryNavigationLocked' property is set!";
                return false;
            }
            //kDebug() << "Navigating to item (" << history()->currentItemIndex()
            //         << "of" << history()->count() << "):" << history()->currentItem().url();
            inPageRequest = false;
            break;
        case QWebEnginePage::NavigationTypeReload:
//            setRequestMetaData(QL1S("cache"), QL1S("reload"));
            inPageRequest = false;
            break;
        case QWebEnginePage::NavigationTypeOther: // triggered by javascript
            qCDebug(WEBENGINEPART_LOG) << "Triggered by javascript";
            inPageRequest = !isTypedUrl;
            break;
        default:
            break;
    }

    if (inPageRequest) {
        // if (!checkLinkSecurity(request, type))
        //      return false;

        //  if (m_sslInfo.isValid())
        //      setRequestMetaData(QL1S("ssl_was_in_use"), QL1S("TRUE"));
    }


    // Honor the enabling/disabling of plugins per host.
    settings()->setAttribute(QWebEngineSettings::PluginsEnabled, WebEngineSettings::self()->isPluginsEnabled(reqUrl.host()));
#ifndef DOWNLOADITEM_KNOWS_PAGE
    emit navigationRequested(this, url);
#endif
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

#if 0
static int errorCodeFromReply(QNetworkReply* reply)
{
    // First check if there is a KIO error code sent back and use that,
    // if not attempt to convert QNetworkReply's NetworkError to KIO::Error.
    QVariant attr = reply->attribute(static_cast<QNetworkRequest::Attribute>(KIO::AccessManager::KioError));
    if (attr.isValid() && attr.type() == QVariant::Int)
        return attr.toInt();

    switch (reply->error()) {
        case QNetworkReply::ConnectionRefusedError:
            return KIO::ERR_CANNOT_CONNECT;
        case QNetworkReply::HostNotFoundError:
            return KIO::ERR_UNKNOWN_HOST;
        case QNetworkReply::TimeoutError:
            return KIO::ERR_SERVER_TIMEOUT;
        case QNetworkReply::OperationCanceledError:
            return KIO::ERR_USER_CANCELED;
        case QNetworkReply::ProxyNotFoundError:
            return KIO::ERR_UNKNOWN_PROXY_HOST;
        case QNetworkReply::ContentAccessDenied:
            return KIO::ERR_ACCESS_DENIED;
        case QNetworkReply::ContentOperationNotPermittedError:
            return KIO::ERR_WRITE_ACCESS_DENIED;
        case QNetworkReply::ContentNotFoundError:
            return KIO::ERR_NO_CONTENT;
        case QNetworkReply::AuthenticationRequiredError:
            return KIO::ERR_CANNOT_AUTHENTICATE;
        case QNetworkReply::ProtocolUnknownError:
            return KIO::ERR_UNSUPPORTED_PROTOCOL;
        case QNetworkReply::ProtocolInvalidOperationError:
            return KIO::ERR_UNSUPPORTED_ACTION;
        case QNetworkReply::UnknownNetworkError:
            return KIO::ERR_UNKNOWN;
        case QNetworkReply::NoError:
        default:
            break;
    }

    return 0;
}
#endif

bool WebEnginePage::certificateError(const QWebEngineCertificateError& ce)
{
    if (m_urlLoadedByPart == ce.url()) {
        m_urlLoadedByPart = QUrl();
        return true;
    } else if (ce.isOverridable()) {
        QString translatedDesc = i18n(ce.errorDescription().toUtf8());
        QString text = i18n("<p>The server failed the authenticity check (%1). The error is:</p><p><tt>%2</tt></p>Do you want to ignore this error?",
                            ce.url().host(), translatedDesc);
        KMessageBox::ButtonCode ans = KMessageBox::questionYesNo(view(), text, i18n("Authentication error"));
        return ans == KMessageBox::Yes;
    } else {
        return false;
    }
}

WebEnginePart* WebEnginePage::part() const
{
    return m_part.data();
}

void WebEnginePage::setPart(WebEnginePart* part)
{
    m_part = part;
}

void WebEnginePage::slotLoadFinished(bool ok)
{
    QUrl requestUrl = url();
    requestUrl.setUserInfo(QString());
    const bool shouldResetSslInfo = (m_sslInfo.isValid() && !domainSchemeMatch(requestUrl, m_sslInfo.url()));
#if 0
    QWebFrame* frame = qobject_cast<QWebFrame *>(reply->request().originatingObject());
    if (!frame)
        return;
    const bool isMainFrameRequest = (frame == mainFrame());
#else
    // PORTING_TODO
    const bool isMainFrameRequest = true;
#endif

#if 0
    // Only deal with non-redirect responses...
    const QVariant redirectVar = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);

    if (isMainFrameRequest && redirectVar.isValid()) {
        m_sslInfo.restoreFrom(reply->attribute(static_cast<QNetworkRequest::Attribute>(KIO::AccessManager::MetaData)),
                              reply->url(), shouldResetSslInfo);
        return;
    }

    const int errCode = errorCodeFromReply(reply);
    kDebug() << frame << "is main frame request?" << isMainFrameRequest << requestUrl;
#endif

    if (ok) {
        if (isMainFrameRequest) {
#if 0
            m_sslInfo.restoreFrom(reply->attribute(static_cast<QNetworkRequest::Attribute>(KIO::AccessManager::MetaData)),
                    reply->url(), shouldResetSslInfo);
#endif
            setPageJScriptPolicy(url());
        }
    } else {
    // Handle any error...
#if 0
    switch (errCode) {
        case 0:
        case KIO::ERR_NO_CONTENT:
            break;
        case KIO::ERR_ABORTED:
        case KIO::ERR_USER_CANCELED: // Do nothing if request is cancelled/aborted
            //kDebug() << "User aborted request!";
            m_ignoreError = true;
            emit loadAborted(QUrl());
            return;
        // Handle the user clicking on a link that refers to a directory
        // Since KIO cannot automatically convert a GET request to a LISTDIR one.
        case KIO::ERR_IS_DIRECTORY:
            m_ignoreError = true;
            emit loadAborted(reply->url());
            return;
        default:
            // Make sure the saveFrameStateRequested signal is emitted so
            // the page can restored properly.
            if (isMainFrameRequest)
                emit saveFrameStateRequested(frame, 0);

            m_ignoreError = (reply->attribute(QNetworkRequest::User).toInt() == QNetworkReply::ContentAccessDenied);
            m_kioErrorCode = errCode;
            break;
#endif
    }

    if (isMainFrameRequest) {
        const WebEnginePageSecurity security = (m_sslInfo.isValid() ? PageEncrypted : PageUnencrypted);
        emit m_part->browserExtension()->setPageSecurity(security);
    }
}

void WebEnginePage::slotUnsupportedContent(QNetworkReply* reply)
{
#if 0
    //kDebug() << reply->url();
    QString mimeType;
    KIO::MetaData metaData;

    KIO::AccessManager::putReplyOnHold(reply);
    QString downloadCmd;
    checkForDownloadManager(view(), downloadCmd);
    if (!downloadCmd.isEmpty()) {
        reply->setProperty("DownloadManagerExe", downloadCmd);
    }

    if (QWePage::handleReply(reply, &mimeType, &metaData)) {
        reply->deleteLater();
        if (qobject_cast<NewWindowPage*>(this) && isBlankUrl(m_part->url())) {
            m_part->closeUrl();
            if (m_part->arguments().metaData().contains(QL1S("new-window"))) {
                m_part->widget()->topLevelWidget()->close();
            } else {
                delete m_part;
            }
        }
        return;
    }

    //kDebug() << "mimetype=" << mimeType << "metadata:" << metaData;

    if (reply->request().originatingObject() == this->mainFrame()) {
        KParts::OpenUrlArguments args;
        args.setMimeType(mimeType);
        args.metaData() = metaData;
        emit m_part->browserExtension()->openUrlRequest(reply->url(), args, KParts::BrowserArguments());
        return;
    }
#endif
    reply->deleteLater();

}
void WebEnginePage::slotFeaturePermissionRequested(const QUrl& url, QWebEnginePage::Feature feature)
{
    if (url == this->url()) {
        part()->slotShowFeaturePermissionBar(feature);
        return;
    }
    switch(feature) {
    case QWebEnginePage::Notifications:
        // FIXME: We should have a setting to tell if this is enabled, but so far it is always enabled.
        setFeaturePermission(url, feature, QWebEnginePage::PermissionGrantedByUser);
        break;
    case QWebEnginePage::Geolocation:
        if (KMessageBox::warningContinueCancel(nullptr, i18n("This site is attempting to "
                                                       "access information about your "
                                                       "physical location.\n"
                                                       "Do you want to allow it access?"),
                                            i18n("Network Transmission"),
                                            KGuiItem(i18n("Allow access")),
                                            KStandardGuiItem::cancel(),
                                            QStringLiteral("WarnGeolocation")) == KMessageBox::Cancel) {
            setFeaturePermission(url, feature, QWebEnginePage::PermissionDeniedByUser);
        } else {
            setFeaturePermission(url, feature, QWebEnginePage::PermissionGrantedByUser);
        }
        break;
    default:
        setFeaturePermission(url, feature, QWebEnginePage::PermissionUnknown);
        break;
    }
}

void WebEnginePage::slotGeometryChangeRequested(const QRect & rect)
{
    const QString host = url().host();

    // NOTE: If a new window was created from another window which is in
    // maximized mode and its width and/or height were not specified at the
    // time of its creation, which is always the case in QWebEnginePage::createWindow,
    // then any move operation will seem not to work. That is because the new
    // window will be in maximized mode where moving it will not be possible...
    if (WebEngineSettings::self()->windowMovePolicy(host) == KParts::HtmlSettingsInterface::JSWindowMoveAllow &&
        (view()->x() != rect.x() || view()->y() != rect.y()))
        emit m_part->browserExtension()->moveTopLevelWidget(rect.x(), rect.y());

    const int height = rect.height();
    const int width = rect.width();

    // parts of following code are based on kjs_window.cpp
    // Security check: within desktop limits and bigger than 100x100 (per spec)
    if (width < 100 || height < 100) {
        qCWarning(WEBENGINEPART_LOG) << "Window resize refused, window would be too small (" << width << "," << height << ")";
        return;
    }

    QRect sg = QApplication::desktop()->screenGeometry(view());

    if (width > sg.width() || height > sg.height()) {
        qCWarning(WEBENGINEPART_LOG) << "Window resize refused, window would be too big (" << width << "," << height << ")";
        return;
    }

    if (WebEngineSettings::self()->windowResizePolicy(host) == KParts::HtmlSettingsInterface::JSWindowResizeAllow) {
        //kDebug() << "resizing to " << width << "x" << height;
        emit m_part->browserExtension()->resizeTopLevelWidget(width, height);
    }

    // If the window is out of the desktop, move it up/left
    // (maybe we should use workarea instead of sg, otherwise the window ends up below kicker)
    const int right = view()->x() + view()->frameGeometry().width();
    const int bottom = view()->y() + view()->frameGeometry().height();
    int moveByX = 0, moveByY = 0;
    if (right > sg.right())
        moveByX = - right + sg.right(); // always <0
    if (bottom > sg.bottom())
        moveByY = - bottom + sg.bottom(); // always <0

    if ((moveByX || moveByY) && WebEngineSettings::self()->windowMovePolicy(host) == KParts::HtmlSettingsInterface::JSWindowMoveAllow)
        emit m_part->browserExtension()->moveTopLevelWidget(view()->x() + moveByX, view()->y() + moveByY);
}

bool WebEnginePage::checkLinkSecurity(const QNetworkRequest &req, NavigationType type) const
{
    // Check whether the request is authorized or not...
    if (!KUrlAuthorized::authorizeUrlAction(QStringLiteral("redirect"), url(), req.url())) {

        //kDebug() << "*** Failed security check: base-url=" << mainFrame()->url() << ", dest-url=" << req.url();
        QString buttonText, title, message;

        int response = KMessageBox::Cancel;
        QUrl linkUrl (req.url());

        if (type == QWebEnginePage::NavigationTypeLinkClicked) {
            message = i18n("<qt>This untrusted page links to<br/><b>%1</b>."
                           "<br/>Do you want to follow the link?</qt>", linkUrl.url());
            title = i18n("Security Warning");
            buttonText = i18nc("follow link despite of security warning", "Follow");
        } else {
            title = i18n("Security Alert");
            message = i18n("<qt>Access by untrusted page to<br/><b>%1</b><br/> denied.</qt>",
                           linkUrl.toDisplayString().toHtmlEscaped());
        }

        if (buttonText.isEmpty()) {
            KMessageBox::error( nullptr, message, title);
        } else {
            // Dangerous flag makes the Cancel button the default
            response = KMessageBox::warningContinueCancel(nullptr, message, title,
                                                          KGuiItem(buttonText),
                                                          KStandardGuiItem::cancel(),
                                                          QString(), // no don't ask again info
                                                          KMessageBox::Notify | KMessageBox::Dangerous);
        }

        return (response == KMessageBox::Continue);
    }

    return true;
}

bool WebEnginePage::checkFormData(const QUrl &url) const
{
    const QString scheme (url.scheme());

    if (m_sslInfo.isValid() &&
        !scheme.compare(QL1S("https")) && !scheme.compare(QL1S("mailto")) &&
        (KMessageBox::warningContinueCancel(nullptr,
                                           i18n("Warning: This is a secure form "
                                                "but it is attempting to send "
                                                "your data back unencrypted.\n"
                                                "A third party may be able to "
                                                "intercept and view this "
                                                "information.\nAre you sure you "
                                                "want to send the data unencrypted?"),
                                           i18n("Network Transmission"),
                                           KGuiItem(i18n("&Send Unencrypted")))  == KMessageBox::Cancel)) {

        return false;
    }


    if (scheme.compare(QL1S("mailto")) == 0 &&
        (KMessageBox::warningContinueCancel(nullptr, i18n("This site is attempting to "
                                                    "submit form data via email.\n"
                                                    "Do you want to continue?"),
                                            i18n("Network Transmission"),
                                            KGuiItem(i18n("&Send Email")),
                                            KStandardGuiItem::cancel(),
                                            QStringLiteral("WarnTriedEmailSubmit")) == KMessageBox::Cancel)) {
        return false;
    }

    return true;
}

// Sanitizes the "mailto:" url, e.g. strips out any "attach" parameters.
static QUrl sanitizeMailToUrl(const QUrl &url, QStringList& files) {
    QUrl sanitizedUrl;

    // NOTE: This is necessary to ensure we can properly use QUrl's query
    // related APIs to process 'mailto:' urls of form 'mailto:foo@bar.com'.
    if (url.hasQuery())
      sanitizedUrl = url;
    else
      sanitizedUrl = QUrl(url.scheme() + QL1S(":?") + url.path());

    QUrlQuery query(sanitizedUrl);
    const QList<QPair<QString, QString> > items (query.queryItems());

    QUrlQuery sanitizedQuery;
    for(auto queryItem : items) {
        if (queryItem.first.contains(QL1C('@')) && queryItem.second.isEmpty()) {
            // ### DF: this hack breaks mailto:faure@kde.org, kmail doesn't expect mailto:?to=faure@kde.org
            queryItem.second = queryItem.first;
            queryItem.first = QStringLiteral("to");
        } else if (QString::compare(queryItem.first, QL1S("attach"), Qt::CaseInsensitive) == 0) {
            files << queryItem.second;
            continue;
        }
        sanitizedQuery.addQueryItem(queryItem.first, queryItem.second);
    }

    sanitizedUrl.setQuery(sanitizedQuery);
    return sanitizedUrl;
}

bool WebEnginePage::handleMailToUrl (const QUrl &url, NavigationType type) const
{
    if (url.scheme() == QL1S("mailto")) {
        QStringList files;
        QUrl mailtoUrl (sanitizeMailToUrl(url, files));

        switch (type) {
            case QWebEnginePage::NavigationTypeLinkClicked:
                if (!files.isEmpty() && KMessageBox::warningContinueCancelList(nullptr,
                                                                               i18n("<qt>Do you want to allow this site to attach "
                                                                                    "the following files to the email message?</qt>"),
                                                                               files, i18n("Email Attachment Confirmation"),
                                                                               KGuiItem(i18n("&Allow attachments")),
                                                                               KGuiItem(i18n("&Ignore attachments")), QL1S("WarnEmailAttachment")) == KMessageBox::Continue) {

                   // Re-add the attachments...
                    QStringListIterator filesIt (files);
                    QUrlQuery query(mailtoUrl);
                    while (filesIt.hasNext()) {
                        query.addQueryItem(QL1S("attach"), filesIt.next());
                    }
                    mailtoUrl.setQuery(query);
                }
                break;
            case QWebEnginePage::NavigationTypeFormSubmitted:
            //case QWebEnginePage::NavigationTypeFormResubmitted:
                if (!files.isEmpty()) {
                    KMessageBox::information(nullptr, i18n("This site attempted to attach a file from your "
                                                     "computer in the form submission. The attachment "
                                                     "was removed for your protection."),
                                             i18n("Attachment Removed"), QStringLiteral("InfoTriedAttach"));
                }
                break;
            default:
                 break;
        }

        //kDebug() << "Emitting openUrlRequest with " << mailtoUrl;
        emit m_part->browserExtension()->openUrlRequest(mailtoUrl);
        return true;
    }

    return false;
}

void WebEnginePage::setPageJScriptPolicy(const QUrl &url)
{
    const QString hostname (url.host());
    settings()->setAttribute(QWebEngineSettings::JavascriptEnabled,
                             WebEngineSettings::self()->isJavaScriptEnabled(hostname));

    const KParts::HtmlSettingsInterface::JSWindowOpenPolicy policy = WebEngineSettings::self()->windowOpenPolicy(hostname);
    settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows,
                             (policy != KParts::HtmlSettingsInterface::JSWindowOpenDeny &&
                              policy != KParts::HtmlSettingsInterface::JSWindowOpenSmart));
}

void WebEnginePage::slotAuthenticationRequired(const QUrl &requestUrl, QAuthenticator *auth)
{
    KIO::AuthInfo info;
    info.url = requestUrl;
    info.username = auth->user();
    info.realmValue = auth->realm();
    // If no realm metadata, then make sure path matching is turned on.
    info.verifyPath = info.realmValue.isEmpty();

    const QString errorMsg = QString();
    const int ret = m_passwdServerClient->queryAuthInfo(&info, errorMsg, view()->window()->winId(), KUserTimestamp::userTimestamp());
    if (ret == KJob::NoError) {
        auth->setUser(info.username);
        auth->setPassword(info.password);
    } else {
        // Set authenticator null if dialog is cancelled
        // or if we couldn't communicate with kpasswdserver
        *auth = QAuthenticator();
    }
}



/************************************* Begin NewWindowPage ******************************************/

NewWindowPage::NewWindowPage(WebWindowType type, WebEnginePart* part, QWidget* parent)
              :WebEnginePage(part, parent) , m_type(type) , m_createNewWindow(true)
{
    Q_ASSERT_X (part, "NewWindowPage", "Must specify a valid KPart");

    connect(this, SIGNAL(menuBarVisibilityChangeRequested(bool)),
            this, SLOT(slotMenuBarVisibilityChangeRequested(bool)));
    connect(this, SIGNAL(toolBarVisibilityChangeRequested(bool)),
            this, SLOT(slotToolBarVisibilityChangeRequested(bool)));
    connect(this, SIGNAL(statusBarVisibilityChangeRequested(bool)),
            this, SLOT(slotStatusBarVisibilityChangeRequested(bool)));
    connect(this, SIGNAL(loadFinished(bool)), this, SLOT(slotLoadFinished(bool)));
#if QTWEBENGINE_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    if (m_type == WebBrowserBackgroundTab) {
        m_windowArgs.setLowerWindow(true);
    }
#endif
}

NewWindowPage::~NewWindowPage()
{
}

static KParts::BrowserArguments browserArgs(WebEnginePage::WebWindowType type)
{
    KParts::BrowserArguments bargs;
    switch (type) {
        case WebEnginePage::WebDialog:
        case WebEnginePage::WebBrowserWindow:
            bargs.setForcesNewWindow(true);
            break;
        case WebEnginePage::WebBrowserTab:
#if QTWEBENGINE_VERSION >= QT_VERSION_CHECK(5, 7, 0)
        case WebEnginePage::WebBrowserBackgroundTab:
#endif
            // let konq decide, based on user configuration
            //bargs.setNewTab(true);
            break;
    }
    return bargs;
}

bool NewWindowPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame)
{
    //qCDebug(WEBENGINEPART_LOG) << "url:" << url << ", type:" << type << ", isMainFrame:" << isMainFrame << "m_createNewWindow=" << m_createNewWindow;
    if (m_createNewWindow) {
        const QUrl reqUrl (url);

        const bool actionRequestedByUser = type != QWebEnginePage::NavigationTypeOther;
        const bool actionRequestsNewTab = m_type == QWebEnginePage::WebBrowserBackgroundTab ||
                                          m_type == QWebEnginePage::WebBrowserTab;

        if (actionRequestedByUser && !actionRequestsNewTab) {
            if (!part() && !isMainFrame) {
                return false;
            }
            const KParts::HtmlSettingsInterface::JSWindowOpenPolicy policy = WebEngineSettings::self()->windowOpenPolicy(reqUrl.host());
            switch (policy) {
            case KParts::HtmlSettingsInterface::JSWindowOpenDeny:
                // TODO: Implement support for dealing with blocked pop up windows.
                this->deleteLater();
                return false;
            case KParts::HtmlSettingsInterface::JSWindowOpenAsk: {
                const QString message = (reqUrl.isEmpty() ?
                                          i18n("This site is requesting to open a new popup window.\n"
                                               "Do you want to allow this?") :
                                          i18n("<qt>This site is requesting to open a popup window to"
                                               "<p>%1</p><br/>Do you want to allow this?</qt>",
                                               KStringHandler::rsqueeze(reqUrl.toDisplayString().toHtmlEscaped(), 100)));
                if (KMessageBox::questionYesNo(view(), message,
                                               i18n("Javascript Popup Confirmation"),
                                               KGuiItem(i18n("Allow")),
                                               KGuiItem(i18n("Do Not Allow"))) == KMessageBox::No) {
                    // TODO: Implement support for dealing with blocked pop up windows.
                    this->deleteLater();
                    return false;
                }
               break;
            }
            default:
                break;
            }
        }

        // Browser args...
        KParts::BrowserArguments bargs = browserArgs(m_type);

        // OpenUrl args...
        KParts::OpenUrlArguments uargs;
        uargs.setMimeType(QL1S("text/html"));
        uargs.setActionRequestedByUser(actionRequestedByUser);

        // Window args...
        KParts::WindowArgs wargs (m_windowArgs);

        KParts::ReadOnlyPart* newWindowPart =nullptr;
        emit part()->browserExtension()->createNewWindow(QUrl(), uargs, bargs, wargs, &newWindowPart);
        qCDebug(WEBENGINEPART_LOG) << "Created new window" << newWindowPart;

        if (!newWindowPart) {
            return false;
        } else if (newWindowPart->widget()->topLevelWidget() != part()->widget()->topLevelWidget()) {
            KParts::OpenUrlArguments args;
            args.metaData().insert(QL1S("new-window"), QL1S("true"));
            newWindowPart->setArguments(args);
        }

        // Get the webview...
        WebEnginePart* webenginePart = qobject_cast<WebEnginePart*>(newWindowPart);
        WebEngineView* webView = webenginePart ? qobject_cast<WebEngineView*>(webenginePart->view()) : nullptr;

        // If the newly created window is NOT a webenginepart...
        if (!webView) {
            qCDebug(WEBENGINEPART_LOG) << "Opening URL on" << newWindowPart;
            newWindowPart->openUrl(reqUrl);
            this->deleteLater();
            return false;
        }
        // Reparent this page to the new webview to prevent memory leaks.
        setParent(webView);
        // Replace the webpage of the new webview with this one. Nice trick...
        webView->setPage(this);
        // Set the new part as the one this page will use going forward.
        setPart(webenginePart);
        // Connect all the signals from this page to the slots in the new part.
        webenginePart->connectWebEnginePageSignals(this);
        //Set the create new window flag to false...
        m_createNewWindow = false;

    }

#ifndef DOWNLOADITEM_KNOWS_PAGE
    emit navigationRequested(this, url);
#endif
    return WebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

void NewWindowPage::slotGeometryChangeRequested(const QRect & rect)
{
    if (!rect.isValid())
        return;

    if (!m_createNewWindow) {
        WebEnginePage::slotGeometryChangeRequested(rect);
        return;
    }

    m_windowArgs.setX(rect.x());
    m_windowArgs.setY(rect.y());
    m_windowArgs.setWidth(qMax(rect.width(), 100));
    m_windowArgs.setHeight(qMax(rect.height(), 100));
}

void NewWindowPage::slotMenuBarVisibilityChangeRequested(bool visible)
{
    //kDebug() << visible;
    m_windowArgs.setMenuBarVisible(visible);
}

void NewWindowPage::slotStatusBarVisibilityChangeRequested(bool visible)
{
    //kDebug() << visible;
    m_windowArgs.setStatusBarVisible(visible);
}

void NewWindowPage::slotToolBarVisibilityChangeRequested(bool visible)
{
    //kDebug() << visible;
    m_windowArgs.setToolBarsVisible(visible);
}

// When is this called? (and acceptNavigationRequest is not called?)
// The only case I found is Ctrl+click on link to data URL (like in konqviewmgrtest), that's quite specific...
// Everything else seems to work with this method being commented out...
void NewWindowPage::slotLoadFinished(bool ok)
{
    Q_UNUSED(ok)
    if (!m_createNewWindow)
        return;

    const bool actionRequestedByUser = true; // ### we don't have the information here, unlike in acceptNavigationRequest

    // Browser args...
    KParts::BrowserArguments bargs = browserArgs(m_type);
    //bargs.frameName = mainFrame()->frameName();

    // OpenUrl args...
    KParts::OpenUrlArguments uargs;
    uargs.setMimeType(QL1S("text/html"));
    uargs.setActionRequestedByUser(actionRequestedByUser);

    // Window args...
    KParts::WindowArgs wargs (m_windowArgs);

    KParts::ReadOnlyPart* newWindowPart =nullptr;
    emit part()->browserExtension()->createNewWindow(QUrl(), uargs, bargs, wargs, &newWindowPart);

    qCDebug(WEBENGINEPART_LOG) << "Created new window or tab" << newWindowPart;

    // Get the webview...
    WebEnginePart* webenginePart = newWindowPart ? qobject_cast<WebEnginePart*>(newWindowPart) : nullptr;
    WebEngineView* webView = webenginePart ? qobject_cast<WebEngineView*>(webenginePart->view()) : nullptr;

    if (webView) {
        // if a new window is created, set a new window meta-data flag.
        if (newWindowPart->widget()->topLevelWidget() != part()->widget()->topLevelWidget()) {
            KParts::OpenUrlArguments args;
            args.metaData().insert(QL1S("new-window"), QL1S("true"));
            newWindowPart->setArguments(args);
        }
        // Reparent this page to the new webview to prevent memory leaks.
        setParent(webView);
        // Replace the webpage of the new webview with this one. Nice trick...
        webView->setPage(this);
        // Set the new part as the one this page will use going forward.
        setPart(webenginePart);
        // Connect all the signals from this page to the slots in the new part.
        webenginePart->connectWebEnginePageSignals(this);
    }

    //Set the create new window flag to false...
    m_createNewWindow = false;
}

/****************************** End NewWindowPage *************************************************/

