/* This file is part of the KDE project
   Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include "konq_listview.h"
#include "konq_listviewitems.h"
#include "konq_listviewwidget.h"
#include "konq_propsview.h"

#include <qdragobject.h>
#include <qheader.h>
#include <qcursor.h>

#include <kcursor.h>
#include <kdebug.h>
#include <konqdirlister.h>
#include <kglobal.h>
#include <kglobalsettings.h>
#include <kio/job.h>
#include <konqoperations.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <kprotocolinfo.h>
#include <konqsettings.h>

#include <stdlib.h>
#include <assert.h>

ColumnInfo::ColumnInfo()
   :displayInColumn(-1)
   ,name("")
   ,desktopFileName("")
   ,udsId(0)
   ,displayThisOne(FALSE)
   ,toggleThisOne(0)
{};


ColumnInfo::ColumnInfo(const char* n, const char* desktopName, int kioUds,int count,bool enabled,KToggleAction* someAction)
   :displayInColumn(count)
   ,name(n)
   ,desktopFileName(desktopName)
   ,udsId(kioUds)
   ,displayThisOne(enabled)
   ,toggleThisOne(someAction)
{}

void ColumnInfo::setData(const char* n, const char* desktopName, int kioUds,int count,bool enabled,KToggleAction* someAction)
{
   displayInColumn=count;
   name=n;
   desktopFileName=desktopName;
   udsId=kioUds;
   displayThisOne=enabled;
   toggleThisOne=someAction;
};


KonqBaseListViewWidget::KonqBaseListViewWidget( KonqListView *parent, QWidget *parentWidget)
:KListView(parentWidget )
,sortedByColumn(0)
,ascending(TRUE)
,m_dirLister(0L)
,m_dragOverItem(0L)
,m_pressed(FALSE)
,m_pressedItem(0L)
,m_filesSelected(FALSE)
,m_showIcons(TRUE)
,m_filenameColumn(0)
,m_pBrowserView(parent)
,m_selectedFilesStatusText()
{
   kdDebug(1202) << "+KonqBaseListViewWidget" << endl;

   m_bTopLevelComplete  = true;

   //Adjust KListView behaviour
   setMultiSelection(TRUE);
   setSelectionModeExt( Konqueror );

   initConfig();

   connect(this,SIGNAL(rightButtonPressed(QListViewItem*,const QPoint&,int)),this,SLOT(slotRightButtonPressed(QListViewItem*,const QPoint&,int)));
   connect(this,SIGNAL(returnPressed(QListViewItem*)),this,SLOT(slotReturnPressed(QListViewItem*)));
   connect(this, SIGNAL( mouseButtonPressed(int, QListViewItem*, const QPoint&, int)),
           this, SLOT( slotMouseButtonPressed(int, QListViewItem*, const QPoint&, int)) );
   connect(this,SIGNAL(executed(QListViewItem* )),this,SLOT(slotExecuted(QListViewItem*)));
   connect(this,SIGNAL(currentChanged(QListViewItem*)),this,SLOT(slotCurrentChanged(QListViewItem*)));
   connect(this,SIGNAL(onItem(QListViewItem*)),this,SLOT(slotOnItem(QListViewItem*)));
   connect(this,SIGNAL(onViewport()),this,SLOT(slotOnViewport()));
   connect(this,SIGNAL(menuShortCutPressed (KListView* , QListViewItem* )),this,SLOT(slotPopupMenu(KListView*,QListViewItem*)));

   viewport()->setAcceptDrops( true );
   viewport()->setMouseTracking( true );
   viewport()->setFocusPolicy( QWidget::WheelFocus );
   setFocusPolicy( QWidget::WheelFocus );
   setAcceptDrops( true );

   //looks better with the statusbar
   setFrameStyle( QFrame::WinPanel | QFrame::Sunken );
   setLineWidth(1);
   //setFrameStyle( QFrame::NoFrame | QFrame::Plain );
   setShowSortIndicator(TRUE);

   //confColumns.setAutoDelete(TRUE);
}

KonqBaseListViewWidget::~KonqBaseListViewWidget()
{
  kdDebug(1202) << "-KonqBaseListViewWidget" << endl;

  if ( m_dirLister ) delete m_dirLister;
}

void KonqBaseListViewWidget::readProtocolConfig( const QString & protocol )
{
   kdDebug(1202)<<"readProtocolConfig: -"<<protocol<<"-"<<endl;

   KConfig * config = KGlobal::config();
   if ( config->hasGroup( "ListView_" + protocol ) )
      config->setGroup( "ListView_" + protocol );
   else
      config->setGroup( "ListView_default" );

   sortedByColumn=config->readEntry("SortBy","FileName");
   ascending=config->readBoolEntry("SortOrder",TRUE);

   QStringList lstColumns = config->readListEntry( "Columns" );
   if (lstColumns.isEmpty())
   {
      // Default column selection
      lstColumns.append( "Size" );
      lstColumns.append( "Modified" );
      lstColumns.append( "Permissions" );
      lstColumns.append( "Owner" );
      lstColumns.append( "Group" );
      lstColumns.append( "Link" );
   }

   //disable everything
   for (unsigned int i=0; i<NumberOfAtoms; i++)
   {
      confColumns[i].displayThisOne=FALSE;
      confColumns[i].displayInColumn=-1;
      confColumns[i].toggleThisOne->setChecked(FALSE);
      confColumns[i].toggleThisOne->setEnabled(TRUE);
   };
   int currentColumn(m_filenameColumn+1);
   //check all columns in lstColumns
   for (unsigned int i=0; i<lstColumns.count(); i++)
   {
      //search the column in confColumns
      for (unsigned int j=0; j<NumberOfAtoms; j++)
      {
         if (confColumns[j].name==*lstColumns.at(i))
         {
            confColumns[j].displayThisOne=TRUE;
            confColumns[j].displayInColumn=currentColumn;
            confColumns[j].toggleThisOne->setChecked(TRUE);
            currentColumn++;
            break;
         }
      }
   }
   //check what the protocol provides
   QStringList listingList=KProtocolInfo::listing(protocol);
   kdDebug(1202)<<"protocol: -"<<protocol<<"-"<<endl;
   for (unsigned int j=0; j<listingList.count(); j++)
      kdDebug(1202)<<"listing: -"<<*listingList.at(j)<<"-"<<endl;

   // Even if this is not given by the protocol, we can determine it.
   // Please don't remove this ;-). It makes it possible to show the file type
   // using the mimetype comment, which for most users is a nicer alternative
   // than the raw mimetype name.
   listingList.append( "MimeType" );

   for (unsigned int i=0; i<NumberOfAtoms; i++)
   {
      if ((confColumns[i].udsId==KIO::UDS_URL) || (confColumns[i].udsId==KIO::UDS_MIME_TYPE))
         continue;
      unsigned int k(0);
      for (k=0; k<listingList.count(); k++)
         if (*listingList.at(k)==confColumns[i].desktopFileName) break;
      if (*listingList.at(k)!=confColumns[i].desktopFileName)
      {
         for (unsigned int l=0; l<NumberOfAtoms; l++)
            if (confColumns[i].displayInColumn>confColumns[i].displayInColumn)
               confColumns[i].displayInColumn--;
         confColumns[i].displayThisOne=FALSE;
         confColumns[i].toggleThisOne->setEnabled(FALSE);
         confColumns[i].toggleThisOne->setChecked(FALSE);
      }
   }
}

void KonqBaseListViewWidget::stop()
{
  m_dirLister->stop();
}

const KURL & KonqBaseListViewWidget::url()
{
  return m_url;
}

void KonqBaseListViewWidget::updateSelectedFilesInfo()
{
   long fileSizeSum = 0;
   long fileCount = 0;
   long dirCount = 0;
   m_filesSelected=FALSE;
   m_selectedFilesStatusText="";
   for (iterator it = begin(); it != end(); it++ )
   {
      if (it->isSelected())
      {
         m_filesSelected=TRUE;
         if ( S_ISDIR( it->item()->mode() ) )
            dirCount++;
         else
         {
            fileSizeSum += it->item()->size();
            fileCount++;
         }
      }
   }
   if (m_filesSelected)
   {
      int items(fileCount+dirCount);
      if (items == 1)
         m_selectedFilesStatusText= i18n("One Item");
      else
         m_selectedFilesStatusText= i18n("%1 Items").arg(items);
      m_selectedFilesStatusText+= " - ";
      if (fileCount == 1)
         m_selectedFilesStatusText+= i18n("One File");
      else
         m_selectedFilesStatusText+= i18n("%1 Files").arg(fileCount);
      m_selectedFilesStatusText+= " ";
      m_selectedFilesStatusText+= i18n("(%1 Total)").arg(KIO::convertSize(fileSizeSum));
      m_selectedFilesStatusText+= " - ";
      if (dirCount == 1)
         m_selectedFilesStatusText+= i18n("One Directory");
      else
         m_selectedFilesStatusText+= i18n("%1 Directories").arg(dirCount);
   }
   emit m_pBrowserView->setStatusBarText(m_selectedFilesStatusText);
   //cerr<<"KonqTextViewWidget::updateSelectedFilesInfo"<<endl;
}

/*QStringList KonqBaseListViewWidget::readProtocolConfig( const QString & protocol )
{
   KConfig * config = KGlobal::config();
   if ( config->hasGroup( "ListView_" + protocol ) )
      config->setGroup( "ListView_" + protocol );
   else
      config->setGroup( "ListView_default" );

   QStringList lstColumns = config->readListEntry( "Columns" );
   if (lstColumns.isEmpty())
   {
      // Default order and column selection
      lstColumns.append( "Name" );
//      lstColumns.append( "Type" );
      lstColumns.append( "Size" );
      //lstColumns.append( "Date" );
      lstColumns.append( "Date" );
//      lstColumns.append( "Created" );
//      lstColumns.append( "Accessed" );
      lstColumns.append( "Permissions" );
      lstColumns.append( "Owner" );
      lstColumns.append( "Group" );
      lstColumns.append( "Link" );
   }
   // (Temporary) complete list of columns and associated m_uds constant
   // It is just there to avoid tons of if(...) in the loop below
   // Order has no importance of course - it matches global.h just for easier maintainance
   QDict<int> completeDict;
   completeDict.setAutoDelete( true );
   completeDict.insert( I18N_NOOP("Size"), new int(KIO::UDS_SIZE) );
   completeDict.insert( I18N_NOOP("Owner"), new int(KIO::UDS_USER) );
   completeDict.insert( I18N_NOOP("Group"), new int(KIO::UDS_GROUP) );
   completeDict.insert( I18N_NOOP("Name"), new int(KIO::UDS_NAME) );
   completeDict.insert( I18N_NOOP("Permissions"), new int(KIO::UDS_ACCESS) );
   completeDict.insert( I18N_NOOP("Date"), new int(KIO::UDS_MODIFICATION_TIME) );
   // we can even have two possible titles for the same column
   completeDict.insert( I18N_NOOP("Modification time"), new int(KIO::UDS_MODIFICATION_TIME) );
   completeDict.insert( I18N_NOOP("Access time"), new int(KIO::UDS_ACCESS_TIME) );
   completeDict.insert( I18N_NOOP("Creation time"), new int(KIO::UDS_CREATION_TIME) );
   completeDict.insert( I18N_NOOP("Type"), new int(KIO::UDS_FILE_TYPE) );
   completeDict.insert( I18N_NOOP("Link"), new int(KIO::UDS_LINK_DEST) );
   completeDict.insert( I18N_NOOP("URL"), new int(KIO::UDS_URL) );
   completeDict.insert( I18N_NOOP("MimeType"), new int(KIO::UDS_MIME_TYPE) );

   m_dctColumnForAtom.clear();
   m_dctColumnForAtom.setAutoDelete( true );
   //QStringList::Iterator it = lstColumns.begin();
   int currentColumn = 0;
   for(QStringList::Iterator it = lstColumns.begin() ; it != lstColumns.end(); it++ )
   {
      // Lookup the KIO::UDS_* for this column, by name
      int * uds = completeDict[ *it ];
      if (!uds)
         kdError(1202) << "The column " << *it << ", specified in konqueror's config file, is unknown to konq_listviewwidget !" << endl;
      else
      {
         // Store result, in m_dctColumnForAtom
         m_dctColumnForAtom.insert( *uds, new int(currentColumn) );
         currentColumn++;
      }
   }
   return lstColumns;
}*/

void KonqBaseListViewWidget::initConfig()
{
   m_pSettings = KonqFMSettings::settings();

   QFont stdFont( m_pSettings->standardFont() );
   setFont( stdFont );
   //TODO: create config GUI
   QFont itemFont( m_pSettings->standardFont() );
   itemFont.setUnderline( m_pSettings->underlineLink() );
   setItemFont( itemFont );
   setItemColor( m_pSettings->normalTextColor() );
}

void KonqBaseListViewWidget::viewportDragMoveEvent( QDragMoveEvent *_ev )
{
  static int c = 0;
  c++;
  debug("DRAG EVENT %d",c);
   KonqBaseListViewItem *item = (KonqBaseListViewItem*)itemAt( _ev->pos() );
   if ( !item )
   {
      if ( m_dragOverItem )
         setSelected( m_dragOverItem, false );
      _ev->accept();
      return;
   }

   if ( m_dragOverItem == item )
      return;
   if ( m_dragOverItem != 0L )
      setSelected( m_dragOverItem, false );

   if ( item->item()->acceptsDrops( ) )
   {
      _ev->accept();
      setSelected( item, true );
      m_dragOverItem = item;
   }
   else
   {
      _ev->ignore();
      m_dragOverItem = 0L;
   }

   return;
}

void KonqBaseListViewWidget::viewportDragEnterEvent( QDragEnterEvent *_ev )
{
   m_dragOverItem = 0L;

   // Save the available formats
   m_lstDropFormats.clear();

   for( int i = 0; _ev->format( i ); i++ )
   {
      if ( *( _ev->format( i ) ) )
         m_lstDropFormats.append( _ev->format( i ) );
   }

   // By default we accept any format
   _ev->accept();
}

void KonqBaseListViewWidget::viewportDragLeaveEvent( QDragLeaveEvent * )
{
   if ( m_dragOverItem != 0L )
      setSelected( m_dragOverItem, false );
   m_dragOverItem = 0L;

   /** DEBUG CODE */
   // Give the user some feedback...
   /** End DEBUG CODE */
}

void KonqBaseListViewWidget::viewportDropEvent( QDropEvent *ev  )
{
   if ( m_dragOverItem != 0L )
      setSelected( m_dragOverItem, false );
   m_dragOverItem = 0L;

   KonqBaseListViewItem *item = (KonqBaseListViewItem*)itemAt( ev->pos() );

   KonqFileItem * destItem = (item) ? item->item() : static_cast<KonqFileItem *>(m_dirLister->rootItem());
   if ( !destItem )
   {
      // Maybe we want to do a stat to get full info about the root item
      // (when we use permissions). For now create a dummy one.
      destItem = new KonqFileItem( S_IFDIR, (mode_t)-1, url() );
   }
   KonqOperations::doDrop( destItem, ev, this );
   if ( !item && !m_dirLister->rootItem() )
     delete destItem; // we just created it
}

void KonqBaseListViewWidget::viewportMousePressEvent( QMouseEvent *_ev )
{
  KListView::viewportMousePressEvent( _ev );

  //what was it intended for ? (alex)
  //QPoint globalPos = mapToGlobal( _ev->pos() );
  m_pressed = false;

  KonqBaseListViewItem *item = (KonqBaseListViewItem*)itemAt( _ev->pos() );
  if ( item ) {
    if ( _ev->button() == LeftButton || _ev->button() == MidButton ) {

      m_pressed = true;
      m_pressedPos = _ev->pos();
      m_pressedItem = item;
      return;

    }
  }
}

void KonqBaseListViewWidget::viewportMouseDoubleClickEvent( QMouseEvent *_ev)
{
   kdDebug(1202)<<"viewportMouseDoubleClickEvent"<<endl;
   //this one adjusts m_pressedItem
   viewportMousePressEvent(_ev);
   KListView::viewportMouseDoubleClickEvent(_ev);
}

void KonqBaseListViewWidget::viewportMouseReleaseEvent( QMouseEvent *_mouse )
{
   KListView::viewportMouseReleaseEvent( _mouse );

   if ( !m_pressed )
      return;

   m_pressed = false;
   m_pressedItem = 0L;
}

void KonqBaseListViewWidget::viewportMouseMoveEvent( QMouseEvent *_mouse )
{
   KListView::viewportMouseMoveEvent( _mouse );

   if ( m_pressed && m_pressedItem )
   {
      int x = _mouse->pos().x();
      int y = _mouse->pos().y();

      //Is it time to start a drag?
      if ( abs( x - m_pressedPos.x() ) > KGlobalSettings::dndEventDelay() || abs( y - m_pressedPos.y() ) > KGlobalSettings::dndEventDelay() )
      {
         // Collect all selected items
         QStrList urls;
         iterator it = begin();
         for( ; it != end(); it++ )
            if ( it->isSelected() )
               urls.append( it->item()->url().url().ascii() );

         // Multiple URLs ?
         QPixmap pixmap2;
         bool pixmap0Invalid(m_pressedItem->pixmap(0)==0);
         if (!pixmap0Invalid) if (m_pressedItem->pixmap(0)->isNull()) pixmap0Invalid=TRUE;

         if (( urls.count() > 1 ) || (pixmap0Invalid))
         {
            pixmap2 = DesktopIcon( "kmultiple", KIcon::SizeMedium );
            if ( pixmap2.isNull() )
               warning("KDesktop: Could not find kmultiple pixmap\n");
         }

         // Calculate hotspot
         QPoint hotspot;

         // Do not handle and more mouse move or mouse release events
         m_pressed = false;

         QUriDrag *d = new QUriDrag( urls, viewport() );
         if ( !pixmap2.isNull())
         {
            hotspot.setX( pixmap2.width() / 2 );
            hotspot.setY( pixmap2.height() / 2 );
            d->setPixmap( pixmap2, hotspot );
         }
         else if (!pixmap0Invalid)
         {
            hotspot.setX( m_pressedItem->pixmap( 0 )->width() / 2 );
            hotspot.setY( m_pressedItem->pixmap( 0 )->height() / 2 );
            d->setPixmap( *(m_pressedItem->pixmap( 0 )), hotspot );
         }
         d->drag();
      }
   }
}

bool KonqBaseListViewWidget::isSingleClickArea( const QPoint& _point )
{
   if ( itemAt( _point ) )
   {
      int x = _point.x();
      int pos = header()->mapToActual( 0 );
      int offset = 0;
      int width = columnWidth( pos );

      for ( int index = 0; index < pos; index++ )
         offset += columnWidth( index );

      return ( x > offset && x < ( offset + width ) );
   }
   return false;
}

void KonqBaseListViewWidget::slotOnItem( QListViewItem* _item)
{
   QString s;
   KonqBaseListViewItem* item = (KonqBaseListViewItem*)_item;

   //TODO: Highlight on mouseover
   /*if ( item )
    s = item->item()->getStatusBarInfo();
    emit m_pBrowserView->setStatusBarText( s );*/
   if (( item ) && (!m_filesSelected))
      s = item->item()->getStatusBarInfo();
   else
      if (m_filesSelected) s=m_selectedFilesStatusText;
   emit m_pBrowserView->setStatusBarText( s );
}

void KonqBaseListViewWidget::slotOnViewport()
{
   //TODO: Display summary in DetailedList in statusbar, like iconview does
}

void KonqBaseListViewWidget::slotMouseButtonPressed(int _button, QListViewItem* _item, const QPoint&, int col)
{
  if(_item && _button == MidButton && col < 2)
    m_pBrowserView->mmbClicked( static_cast<KonqBaseListViewItem*>(_item)->item() );
}

void KonqBaseListViewWidget::slotExecuted( QListViewItem* )
{
  //isSingleClickArea() checks wether the mouse pointer is
  // over an area where an action should be triggered
  // no matter wether single or double click
  if ( isSingleClickArea( mapFromGlobal(QCursor::pos())))
  {
    if ( m_pressedItem->isExpandable() )
      m_pressedItem->setOpen( !m_pressedItem->isOpen() );
    slotReturnPressed( m_pressedItem );
  }
}

void KonqBaseListViewWidget::selectedItems( QValueList<KonqBaseListViewItem*>& _list )
{
   iterator it = begin();
   for( ; it != end(); it++ )
      if ( it->isSelected() )
         _list.append( &*it );
}

KURL::List KonqBaseListViewWidget::selectedUrls()
{
   KURL::List list;
   iterator it = begin();
   for( ; it != end(); it++ )
      if ( it->isSelected() )
         list.append( it->item()->url() );
   return list;
}

KonqPropsView * KonqBaseListViewWidget::props() const
{
  return m_pBrowserView->m_pProps;
}

void KonqBaseListViewWidget::emitOpenURLRequest(const KURL& url, const KParts::URLArgs& args)
{
   KParts::URLArgs a = args;
   a.trustedSource = true;
   emit m_pBrowserView->extension()->openURLRequest(url,a);
}

void KonqBaseListViewWidget::slotReturnPressed( QListViewItem *_item )
{
   if ( !_item )
      return;
   KonqFileItem *fileItem = static_cast<KonqBaseListViewItem*>(_item)->item();
   if ( !fileItem )
      return;
   QString serviceType = QString::null;

   KURL u( fileItem->url() );

    if (KonqFMSettings::settings()->alwaysNewWin() && fileItem->mode() & S_IFDIR) {
        KParts::URLArgs args;
        args.serviceType = fileItem->mimetype(); // inode/directory
        emit m_pBrowserView->extension()->createNewWindow( u, args );
    } else {
        // We want to emit openURLRequest, but not right now, because
        // the listview is going to emit other signals (mouse release).
        // Let's not destroy it while it isn't finished emitting.
        openURLRequestFileItem = fileItem;
        QTimer::singleShot( 0, this, SLOT(slotOpenURLRequest()) );
    }
}

void KonqBaseListViewWidget::slotOpenURLRequest()
{
  if ( !openURLRequestFileItem )
    // This shouldn't happen. Well, it can, if one double-clicks on an icon
    // or for any other reason, two singleshots get fired before we get here.
    kdWarning(1202) << "Null openURLRequestFileItem in KonqBaseListViewWidget !" << endl;
  else
  {
    KParts::URLArgs args;
    args.serviceType = openURLRequestFileItem->mimetype();
    emit m_pBrowserView->extension()->openURLRequest( openURLRequestFileItem->url(), args );
    openURLRequestFileItem = 0L;
  }
}

/*void KonqBaseListViewWidget::slotReturnPressed( QListViewItem *_item )
{
  if ( !_item )
    return;

  KonqFileItem *item = ((KonqBaseListViewItem*)_item)->item();
  mode_t mode = item->mode();

  //execute only if item is a file (or a symlink to a file)
  if ( S_ISREG( mode ) )
  {
    KParts::URLArgs args;
    args.serviceType = item->mimetype();
    emit m_pBrowserView->extension()->openURLRequest( item->url(), args );
  }
}*/

void KonqBaseListViewWidget::slotRightButtonPressed( QListViewItem *, const QPoint &_global, int )
{
  kdDebug(1202) << "KonqBaseListViewWidget::slotRightButtonPressed" << endl;
  popupMenu( _global );
}

void KonqBaseListViewWidget::slotPopupMenu(KListView* , QListViewItem* )
{
   QPoint p (width() / 2, height() / 2 );
   p = mapToGlobal( p );
   popupMenu( p );
};

void KonqBaseListViewWidget::popupMenu( const QPoint& _global )
{
   KFileItemList lstItems;

   QValueList<KonqBaseListViewItem*> items;
   selectedItems( items );
   QValueList<KonqBaseListViewItem*>::Iterator it = items.begin();
   for( ; it != items.end(); ++it )
      lstItems.append( (*it)->item() );

   KFileItem * rootItem = 0L;
   if ( lstItems.count() == 0 ) // emit popup for background
   {

     rootItem = m_dirLister->rootItem();
     if ( !rootItem )
     {
       // Maybe we want to do a stat to get full info about the root item
       // (when we use permissions). For now create a dummy one.
       rootItem = new KFileItem( S_IFDIR, (mode_t)-1, url() );
     }

     lstItems.append( m_dirLister->rootItem() );
   }
   emit m_pBrowserView->extension()->popupMenu( _global, lstItems );

   if ( lstItems.count() == 0 )
    if ( ! m_dirLister->rootItem() )
      delete rootItem; // we just created it
}

void KonqBaseListViewWidget::createColumns()
{
   //this column is always required, so add it
   if (columns()<1) addColumn(i18n("Name"));
   setSorting(0,TRUE);

   //remove all but the first column
   for (int i=columns()-1; i>0; i--)
      removeColumn(i);
   //now add the checked columns
   int currentColumn(1);
   for (int i=0; i<NumberOfAtoms; i++)
   {
      if ((confColumns[i].displayThisOne) && (confColumns[i].displayInColumn==currentColumn))
      {
         addColumn(i18n(confColumns[i].name.utf8() ));
         if (sortedByColumn==confColumns[i].desktopFileName)
            setSorting(currentColumn,ascending);
         if (confColumns[i].udsId==KIO::UDS_SIZE) setColumnAlignment(currentColumn,AlignRight);
         i=-1;
         currentColumn++;
      }
   }
}

void KonqBaseListViewWidget::updateListContents()
{
   for (KonqBaseListViewWidget::iterator it = begin(); it != end(); it++ )
      it->updateContents();
}

bool KonqBaseListViewWidget::openURL( const KURL &url )
{

   if ( !m_dirLister )
   {
      // Create the directory lister
      m_dirLister = new KonqDirLister(m_showIcons==FALSE);

      QObject::connect( m_dirLister, SIGNAL( started( const QString & ) ),
                        this, SLOT( slotStarted( const QString & ) ) );
      QObject::connect( m_dirLister, SIGNAL( completed() ), this, SLOT( slotCompleted() ) );
      QObject::connect( m_dirLister, SIGNAL( canceled() ), this, SLOT( slotCanceled() ) );
      QObject::connect( m_dirLister, SIGNAL( clear() ), this, SLOT( slotClear() ) );
      QObject::connect( m_dirLister, SIGNAL( newItems( const KFileItemList & ) ),
                        this, SLOT( slotNewItems( const KFileItemList & ) ) );
      QObject::connect( m_dirLister, SIGNAL( deleteItem( KFileItem * ) ),
                        this, SLOT( slotDeleteItem( KFileItem * ) ) );
      QObject::connect( m_dirLister, SIGNAL( refreshItems( const KFileItemList & ) ),
                        this, SLOT( slotRefreshItems( const KFileItemList & ) ) );
      QObject::connect( m_dirLister, SIGNAL( redirection( const KURL & ) ),
                        this, SLOT( slotRedirection( const KURL & ) ) );
      QObject::connect( m_dirLister, SIGNAL( closeView() ),
                        this, SLOT( slotCloseView() ) );
  }

   // The first time or new protocol ? So create the columns first
   kdDebug(1202) << "protocol in ::openURL: -" << url.protocol()<<"- url: -"<<url.path()<<"-"<<endl;

   if (( columns() <1) || ( url.protocol() != m_url.protocol() ))
   {
      readProtocolConfig( url.protocol() );
      createColumns();
   }
   m_bTopLevelComplete = false;

   m_url=url;

   // Check for new properties in the new dir
   // newProps returns true the first time, and any time something might
   // have changed.
   bool newProps = m_pBrowserView->m_pProps->enterDir( url );

   m_dirLister->setNameFilter( m_pBrowserView->nameFilter() );
   // Start the directory lister !
   m_dirLister->openURL( url, m_pBrowserView->m_pProps->isShowingDotFiles(), false /* new url */ );

   // Apply properties and reflect them on the actions
   // do it after starting the dir lister to avoid changing the properties
   // of the old view
   if ( newProps )
   {
      switch (m_pBrowserView->m_pProps->iconSize())
      {
      case KIcon::SizeSmall:
         m_pBrowserView->m_paSmallIcons->setChecked(TRUE);
         break;
      case KIcon::SizeMedium:
         m_pBrowserView->m_paMediumIcons->setChecked(TRUE);
         break;
      case KIcon::SizeLarge:
         m_pBrowserView->m_paLargeIcons->setChecked(TRUE);
         break;
      default:
         break;
      }
      m_pBrowserView->m_paShowDot->setChecked( m_pBrowserView->m_pProps->isShowingDotFiles() );

      m_pBrowserView->m_pProps->applyColors( this );
   }

   return true;
}

void KonqBaseListViewWidget::setComplete()
{
   m_bTopLevelComplete = true;
   setContentsPos( m_pBrowserView->extension()->urlArgs().xOffset, m_pBrowserView->extension()->urlArgs().yOffset );
}

void KonqBaseListViewWidget::slotStarted( const QString & /*url*/ )
{
   if (!m_bTopLevelComplete)
      emit m_pBrowserView->started(m_dirLister->job());
}

void KonqBaseListViewWidget::slotCompleted()
{
   bool complete = m_bTopLevelComplete;
   setComplete();
   if ( !complete )
     emit m_pBrowserView->completed();
}

void KonqBaseListViewWidget::slotCanceled()
{
   setComplete();
   emit m_pBrowserView->canceled( QString::null );
}

void KonqBaseListViewWidget::slotClear()
{
   kdDebug(1202) << "KonqBaseListViewWidget::slotClear()" << endl;
   clear();
}

void KonqBaseListViewWidget::slotNewItems( const KFileItemList & entries )
{
   kdDebug(1202) << "KonqBaseListViewWidget::slotNewItems " << entries.count() << endl;
   QListIterator<KFileItem> kit ( entries );
   for( ; kit.current(); ++kit )
      new KonqListViewItem( this, static_cast<KonqFileItem *>(*kit) );
}

void KonqBaseListViewWidget::slotDeleteItem( KFileItem * _fileitem )
{
  kdDebug(1202) << "removing " << _fileitem->url().url() << " from tree!" << endl;
  iterator it = begin();
  for( ; it != end(); ++it )
    if ( (*it).item() == _fileitem )
    {
      delete &(*it);
      return;
    }
}

void KonqBaseListViewWidget::slotRefreshItems( const KFileItemList & entries )
{
   QListIterator<KFileItem> kit ( entries );
   for( ; kit.current(); ++kit )
   {
       iterator it = begin();
       for( ; it != end(); ++it )
           if ( (*it).item() == kit.current() )
           {
               it->updateContents();
               break;
           }
   }
}

void KonqBaseListViewWidget::slotRedirection( const KURL & url )
{
    emit m_pBrowserView->extension()->setLocationBarURL( url.prettyURL() );
}

void KonqBaseListViewWidget::slotCloseView()
{
  delete m_pBrowserView;
}

KonqBaseListViewWidget::iterator& KonqBaseListViewWidget::iterator::operator++()
{
  if ( !m_p ) return *this;
  KonqBaseListViewItem *i = (KonqBaseListViewItem*)m_p->firstChild();
  if ( i )
  {
    m_p = i;
    return *this;
  }
  i = (KonqBaseListViewItem*)m_p->nextSibling();
  if ( i )
  {
    m_p = i;
    return *this;
  }
  m_p = (KonqBaseListViewItem*)m_p->parent();
  if ( m_p )
    m_p = (KonqBaseListViewItem*)m_p->nextSibling();

  return *this;
}

KonqBaseListViewWidget::iterator KonqBaseListViewWidget::iterator::operator++(int)
{
   KonqBaseListViewWidget::iterator it = *this;
   if ( !m_p ) return it;
   KonqBaseListViewItem *i = (KonqBaseListViewItem*)m_p->firstChild();
   if (i)
   {
      m_p = i;
      return it;
   }
   i = (KonqBaseListViewItem*)m_p->nextSibling();
   if (i)
   {
      m_p = i;
      return it;
   }
   m_p = (KonqBaseListViewItem*)m_p->parent();
   if (m_p)
      m_p = (KonqBaseListViewItem*)m_p->nextSibling();
   return it;
}

void KonqBaseListViewWidget::drawContentsOffset( QPainter* _painter, int _offsetx, int _offsety,
				    int _clipx, int _clipy, int _clipw, int _cliph )
{
  if ( !_painter )
    return;

  if ( !m_bgPixmap.isNull() )
  {
    int pw = m_bgPixmap.width();
    int ph = m_bgPixmap.height();

    int xOrigin = (_clipx/pw)*pw - _offsetx;
    int yOrigin = (_clipy/ph)*ph - _offsety;

    int rx = _clipx%pw;
    int ry = _clipy%ph;

    for ( int yp = yOrigin; yp - yOrigin < _cliph + ry; yp += ph )
    {
      for ( int xp = xOrigin; xp - xOrigin < _clipw + rx; xp += pw )
	_painter->drawPixmap( xp, yp, m_bgPixmap );
    }
  }

  QListView::drawContentsOffset( _painter, _offsetx, _offsety,
				 _clipx, _clipy, _clipw, _cliph );
}

void KonqBaseListViewWidget::focusInEvent( QFocusEvent* _event )
{
//  emit gotFocus();

  KListView::focusInEvent( _event );
}

#include "konq_listviewwidget.moc"
