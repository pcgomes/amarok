/****************************************************************************************
 * Copyright (c) 2007 Trolltech ASA <copyright@trolltech.com>                           *
 * Copyright (c) 2008 Urs Wolfer <uwolfer@kde.org>                                      *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) any later           *
 * version.                                                                             *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#define DEBUG_PREFIX "NetworkAccessManagerProxy"

#include "NetworkAccessManagerProxy.h"
#ifdef DEBUG_BUILD_TYPE
#include "NetworkAccessViewer.h"
#endif // DEBUG_BUILD_TYPE

#include "core/support/Debug.h"

#include <KProtocolManager>

#include <QMetaMethod>
#include <QNetworkReply>

NetworkAccessManagerProxy *NetworkAccessManagerProxy::s_instance = 0;

NetworkAccessManagerProxy *NetworkAccessManagerProxy::instance()
{
    if( s_instance == 0 )
        s_instance = new NetworkAccessManagerProxy();
    return s_instance;
}

void NetworkAccessManagerProxy::destroy()
{
    if( s_instance )
    {
        delete s_instance;
        s_instance = 0;
    }
}

class NetworkAccessManagerProxy::NetworkAccessManagerProxyPrivate
{
public:
    NetworkAccessManagerProxyPrivate( NetworkAccessManagerProxy *parent )
        : userAgent( KProtocolManager::defaultUserAgent() )
#ifdef DEBUG_BUILD_TYPE
        , viewer( 0 )
#endif // DEBUG_BUILD_TYPE
        , q_ptr( parent )
    {}

    ~NetworkAccessManagerProxyPrivate() {}

    void _replyFinished()
    {
        Q_Q( NetworkAccessManagerProxy );
        QNetworkReply *reply = qobject_cast<QNetworkReply*>( q->sender() );
        CallBackData callback = replyMap.take( reply );
        QByteArray sig = QMetaObject::normalizedSignature( callback.method );
        sig.remove( 0, 1 ); // remove first char, which is the member code (see qobjectdefs.h)
                            // and let Qt's meta object system handle the rest.
        bool success( false );
        const QMetaObject *mo = callback.receiver->metaObject();
        int methodIndex = mo->indexOfSlot( sig );
        if( methodIndex != -1 )
        {
            Error err = { reply->error(), reply->errorString() };
            QMetaMethod method = mo->method( methodIndex );
            success = method.invoke( callback.receiver,
                                     callback.type,
                                     Q_ARG( KUrl, reply->request().url() ),
                                     Q_ARG( QByteArray, reply->readAll() ),
                                     Q_ARG( Error, err ) );
        }
        reply->deleteLater();

        if( !success )
            debug() << QString( "Failed to invoke method %1 of %2" ).arg( QString(sig) ).arg( mo->className() );
    }

    struct CallBackData
    {
        QObject *receiver;
        const char *method;
        Qt::ConnectionType type;
    };

    QHash<QNetworkReply*, CallBackData> replyMap;
    QString userAgent;
#ifdef DEBUG_BUILD_TYPE
    NetworkAccessViewer *viewer;
#endif // DEBUG_BUILD_TYPE

private:
    NetworkAccessManagerProxy *const q_ptr;
    Q_DECLARE_PUBLIC( NetworkAccessManagerProxy )
};

NetworkAccessManagerProxy::NetworkAccessManagerProxy( QObject *parent )
    : KIO::Integration::AccessManager( parent )
    , d( new NetworkAccessManagerProxyPrivate( this ) )
{
    setCache(0);   // disable QtWebKit cache to just use KIO one..
}

NetworkAccessManagerProxy::~NetworkAccessManagerProxy()
{
    delete d;
    s_instance = 0;
}

#ifdef DEBUG_BUILD_TYPE
NetworkAccessViewer *
NetworkAccessManagerProxy::networkAccessViewer()
{
    return d->viewer;
}

void
NetworkAccessManagerProxy::setNetworkAccessViewer( NetworkAccessViewer *viewer )
{
    if( viewer )
    {
        if( d->viewer )
            delete d->viewer;
        d->viewer = viewer;
    }
}
#endif // DEBUG_BUILD_TYPE

QNetworkReply *
NetworkAccessManagerProxy::getData( const KUrl &url, QObject *receiver, const char *method,
                                    Qt::ConnectionType type )
{
    if( !url.isValid() )
    {
        const QMetaObject *mo = receiver->metaObject();
        debug() << QString( "Error: URL %1 is invalid (from %2)" ).arg( url.url() ).arg( mo->className() );
        return 0;
    }
    QNetworkReply *reply = get( QNetworkRequest(url) );
    d->replyMap[ reply ].receiver = receiver;
    d->replyMap[ reply ].method = method;
    d->replyMap[ reply ].type = type;
    connect( reply, SIGNAL(finished()), this, SLOT(_replyFinished()), type );
    return reply;
}

QNetworkReply *
NetworkAccessManagerProxy::createRequest( Operation op, const QNetworkRequest &req, QIODevice *outgoingData )
{
    QNetworkRequest request = req;
    request.setAttribute( QNetworkRequest::HttpPipeliningAllowedAttribute, true );
    request.setRawHeader( "User-Agent", d->userAgent.toLocal8Bit() );

    KIO::CacheControl cc = KProtocolManager::cacheControl();
    switch (cc)
    {
    case KIO::CC_CacheOnly:      // Fail request if not in cache.
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysCache);
        break;

    case KIO::CC_Refresh:        // Always validate cached entry with remote site.
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferNetwork);
        break;

    case KIO::CC_Reload:         // Always fetch from remote site
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        break;

    case KIO::CC_Cache:          // Use cached entry if available.
    case KIO::CC_Verify:         // Validate cached entry with remote site if expired.
    default:
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        break;
    }

    QNetworkReply *reply = KIO::Integration::AccessManager::createRequest( op, request, outgoingData );

#ifdef DEBUG_BUILD_TYPE
    if( d->viewer )
        d->viewer->addRequest( op, request, outgoingData, reply );
#endif // DEBUG_BUILD_TYPE
    return reply;
}

namespace The
{
    NetworkAccessManagerProxy *networkAccessManager()
    {
        return NetworkAccessManagerProxy::instance();
    }
}

#include "NetworkAccessManagerProxy.moc"