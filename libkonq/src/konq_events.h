/* This file is part of the KDE project
   Copyright (C) 2000      Simon Hausmann <hausmann@kde.org>
   Copyright (C) 2000-2007 David Faure <faure@kde.org>
   Copyright (C) 2019      Raphael Rosch <kde-dev@insaner.com>

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

#ifndef __konq_events_h__
#define __konq_events_h__

#include <kparts/event.h>
#include <QList>
#include <libkonq_export.h>
#include <kfileitem.h>
#include <kconfigbase.h>
#include <QUrl>

namespace KParts
{
class ReadOnlyPart;
}

class KonqView;

class LIBKONQ_EXPORT KonqFileSelectionEvent : public KParts::Event
{
public:
    KonqFileSelectionEvent(const KFileItemList &selection, KParts::ReadOnlyPart *part);
    ~KonqFileSelectionEvent() override;

    KFileItemList selection() const
    {
        return m_selection;
    }
    KParts::ReadOnlyPart *part() const
    {
        return m_part;
    }

    static bool test(const QEvent *event)
    {
        return KParts::Event::test(event, s_fileItemSelectionEventName);
    }

private:
    Q_DISABLE_COPY(KonqFileSelectionEvent)
    static const char *const s_fileItemSelectionEventName;

    KFileItemList m_selection;
    KParts::ReadOnlyPart *m_part;
};

class LIBKONQ_EXPORT KonqFileMouseOverEvent : public KParts::Event
{
public:
    KonqFileMouseOverEvent(const KFileItem &item, KParts::ReadOnlyPart *part);
    ~KonqFileMouseOverEvent() override;

    const KFileItem &item() const
    {
        return m_item;
    }
    KParts::ReadOnlyPart *part() const
    {
        return m_part;
    }

    static bool test(const QEvent *event)
    {
        return KParts::Event::test(event, s_fileItemMouseOverEventName);
    }

private:
    Q_DISABLE_COPY(KonqFileMouseOverEvent)
    static const char *const s_fileItemMouseOverEventName;

    KFileItem m_item;
    KParts::ReadOnlyPart *m_part;
};

class LIBKONQ_EXPORT KonqActiveViewChangedEvent : public KParts::Event
{
public:
    KonqActiveViewChangedEvent(const QUrl &url, KonqView *view);
    ~KonqActiveViewChangedEvent() override;

    const QUrl &url() const
    {
        return m_url;
    }
    
    KonqView *view() const
    {
        return m_view;
    }

    static bool test(const QEvent *event)
    {
        return KParts::Event::test(event, s_activeViewChangedEventName);
    }

private:
    Q_DISABLE_COPY(KonqActiveViewChangedEvent)
    static const char *const s_activeViewChangedEventName;

    KParts::ReadOnlyPart *m_part;
    KonqView *m_view;
    QUrl m_url;
};

#endif
