/***************************************************************************
 * copyright            : (C) 2007 Leo Franchi <lfranchi@kde.org>          *
 **************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "LyricsManager.h"

#include "Debug.h"
#include "EngineController.h"

#include <QDomDocument>


////////////////////////////////////////////////////////////////
//// CLASS LyricsObserver
///////////////////////////////////////////////////////////////

LyricsObserver::LyricsObserver()
: m_subject( 0 )
{}

LyricsObserver::LyricsObserver( LyricsSubject *s )
: m_subject( s )
{
    m_subject->attach( this );
}

LyricsObserver::~LyricsObserver()
{
    if( m_subject )
        m_subject->detach( this );
}

////////////////////////////////////////////////////////////////
//// CLASS LyricsSubject
///////////////////////////////////////////////////////////////

void LyricsSubject::sendNewLyrics( QStringList lyrics )
{
    foreach( LyricsObserver* obs, m_observers )
    {
        obs->newLyrics( lyrics );
    }
}

void LyricsSubject::sendLyricsMessage( QString msg )
{
    foreach( LyricsObserver* obs, m_observers )
    {
        obs->lyricsMessage( msg );
    }
}

void LyricsSubject::attach( LyricsObserver *obs )
{
    if( !obs || m_observers.indexOf( obs ) != -1 )
        return;
    m_observers.append( obs );
}

void LyricsSubject::detach( LyricsObserver *obs )
{
    int index = m_observers.indexOf( obs );
    if( index != -1 ) m_observers.removeAt( index );
}

////////////////////////////////////////////////////////////////
//// CLASS LyricsManager
///////////////////////////////////////////////////////////////

LyricsManager* LyricsManager::s_self = 0;

void LyricsManager::lyricsResult( const QString& lyricsXML, bool cached ) //SLOT
{
    DEBUG_BLOCK
    Q_UNUSED( cached );

    debug() << "got xml: " << lyricsXML;
    
    QDomDocument doc;
    if( !doc.setContent( lyricsXML ) )
    {
        debug() << "couldn't read the xml of lyrics, misformed";
        sendLyricsMessage( QString( "error" ) );
        return;
    }

    QString lyrics;

    QDomElement el = doc.documentElement();

    if ( el.tagName() == "suggestions" )
    {
        const QDomNodeList l = doc.elementsByTagName( "suggestion" );

        debug() << "got suggestion list of length" << l.length();
        if( l.length() ==0 )
        {
            sendLyricsMessage( QString( "notfound" ) );
        }
        else
        {
            QVariantList suggested;
            for( uint i = 0; i < l.length(); ++i ) {
                const QString url    = l.item( i ).toElement().attribute( "url" );
                const QString artist = l.item( i ).toElement().attribute( "artist" );
                const QString title  = l.item( i ).toElement().attribute( "title" );

                suggested << QString( "%1 - %2 %3" ).arg( title, artist, url );
            }
           // setData( "lyrics", "suggested", suggested );
            // TODO for now suggested is disabled
            //QStringList suggestions;
            //suggestions <<
            sendLyricsMessage( QString( "notfound" ) ); // FIXME: Until we support it, show something...
        }
    }
    else
    {
        if( !The::engineController()->currentTrack() )
        {
            return;
        }

        lyrics = el.text();
        The::engineController()->currentTrack()->setCachedLyrics( lyricsXML ); // TODO: setLyricsByPath?

        const QString title      = el.attribute( "title" );

        QStringList lyricsData;
        lyricsData << title
            << The::engineController()->currentTrack()->artist()->name()
            << QString() // TODO lyrics site
            << lyrics;

//         setData( "lyrics", "lyrics", lyricsData );
        sendNewLyrics( lyricsData );

    }
}
