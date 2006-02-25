/* NMM - Network-Integrated Multimedia Middleware
 *
 * Copyright (C) 2002-2006
 *                    NMM work group,
 *                    Computer Graphics Lab,
 *                    Saarland University, Germany
 *                    http://www.networkmultimedia.org
 *
 * Maintainer:        Robert Gogolok <gogo@graphics.cs.uni-sb.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Steet, Fifth Floor, Boston, MA  02111-1307
 * USA
 */

#include "nmm_engine.h"

#include "nmm_kdeconfig.h"
#include "nmm_configdialog.h"
#include "debug.h"
#include "plugin/plugin.h"

#include <nmm/base/graph/GraphBuilder2.hpp>
#include <nmm/base/registry/NodeDescription.hpp>
#include <nmm/base/ProxyApplication.hpp>
#include <nmm/interfaces/base/sync/ISynchronizedSink.hpp>
#include <nmm/interfaces/file/ISeekable.hpp>
#include <nmm/interfaces/file/ITrack.hpp>
#include <nmm/interfaces/file/IBufferSize.hpp>
#include <nmm/interfaces/general/progress/IProgressListener.hpp>
#include <nmm/interfaces/general/progress/IProgress.hpp>
#include <nmm/interfaces/general/ITrackDuration.hpp>
#include <nmm/interfaces/device/audio/IAudioDevice.hpp>
#include <nmm/base/ProxyObject.hpp>
#include <nmm/utils/NMMConfig.hpp>

#include <qapplication.h>
#include <qtimer.h>

#include <kfileitem.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <kmimetype.h>
#include <iostream>
#include <kurl.h>

NmmEngine* NmmEngine::s_instance;

AMAROK_EXPORT_PLUGIN( NmmEngine )

NmmEngine::NmmEngine()
  : Engine::Base(),
    __position(0),
    __track_length(0),
    __state(Engine::Empty),
    __app(NULL),
    __endTrack_listener(this, &NmmEngine::endTrack),
    __syncReset_listener(this, &NmmEngine::syncReset),
    __setProgress_listener(this, &NmmEngine::setProgress),
    __trackDuration_listener(this, &NmmEngine::trackDuration),
    __composite(NULL),
    __playback(NULL),
    __display(NULL),
    __av_sync(NULL),
    __synchronizer(NULL),
    __with_video(false),
    __seeking(false)
{
  addPluginProperty( "HasConfigure", "true" );
}

bool NmmEngine::init()
{
  DEBUG_BLOCK
  
  s_instance = this;
    
  // disable debug and warning streams
  NamedObject::getGlobalInstance().setDebugStream(NULL, NamedObject::ALL_LEVELS);
  NamedObject::getGlobalInstance().setWarningStream(NULL, NamedObject::ALL_LEVELS);

  // create new NMM application object
  __app = ProxyApplication::getApplication(0, 0);

  return true;
}

NmmEngine::~NmmEngine()
{
  // stop all nodes
  stop();

  // delete application object
  if (__app)
    delete __app;
}

void NmmEngine::checkSecurity()
{
  const char* home(getenv("HOME"));
  NMMConfig nmmconfig( string(home) + string("/.nmmrc") );

  bool readpaths_set = false;
  bool writepaths_set = false;
  string optionvalue("");
  
  nmmconfig.getValue("allowedreadpaths", optionvalue);
  if( optionvalue != "" )
    readpaths_set = true;

  optionvalue = "";
  nmmconfig.getValue("allowedwritepaths", optionvalue);
  if( optionvalue != "" )
    writepaths_set = true;

  QString str;
  str += "<html><body>Your current NMM setup is insecure.<br/><br/>";
  str += "The file <b>.nmmrc</b> in your home directory restricts read and write access for NMM to certain paths.<br/><br/>";

  if( !readpaths_set )
    str += "<b>allowedreadpaths option is not set</b>. NMM plugins are therefore allowed to read every file the process running NMM is allowed.<br/>";

  if( !writepaths_set )
    str += "<b>allowedwritepaths option is not set</b>. NMM plugins are therefore allowed to write every file or directory the process running NMM is allowed.<br/>";

  str += "<br/>See <a href=\"http://www.networkmultimedia.org/Download/\">http://www.networkmultimedia.org/Download/</a> for general security instructions in the correspoding <i>configure and test NMM</i> section depending on your chosen installation method.";
  str += "</body></html>";
    
  if( !writepaths_set || !readpaths_set )
    KMessageBox::information(0, str, i18n( "Insecure NMM setup" ), "insecureNmmSetup", KMessageBox::AllowLink );
}

Engine::State NmmEngine::state() const
{
  return __state;
}

amaroK::PluginConfig* NmmEngine::configure() const
{
    NmmConfigDialog* dialog = new NmmConfigDialog();

    // connect...
    
    return dialog;
}

bool NmmEngine::load(const KURL& url, bool stream)
{
  DEBUG_BLOCK

  // check security options
  static bool already_checked = false;
  if( !already_checked)
    QTimer::singleShot(100, this, SLOT( checkSecurity() ) );
  already_checked = true;

  // play only local files
  if (!url.isLocalFile()) return false;

  Engine::Base::load(url, stream);

  cleanup();

  // make the GraphBuilder construct an appropriate graph for the given URL
  try {
    QStringList hosts;

    // node for audio playback
    NodeDescription playback_nd("PlaybackNode");
    // ALSA or OSS
    if( NmmKDEConfig::audioOutputPlugin() == "ALSAPlaybackNode" )
        playback_nd = NodeDescription("ALSAPlaybackNode");

    // TODO: currently we only support one host for audio playback
    if( !(hosts = getSinkHosts()).empty() )
        playback_nd.setLocation( hosts.first().ascii() );

    // node for video playback
    NodeDescription display_nd("XDisplayNode");

    // TODO: currently we only support one host for video playback
    if( !(hosts = getSinkHosts( false )).empty() )
        display_nd.setLocation( hosts.first().ascii() );

    GraphBuilder2 gb;

    // convert the URL to a valid NMM url
    if(!gb.setURL("file://" + string(url.path().ascii())))
      throw Exception("Invalid URL given");

    ClientRegistry& registry = __app->getRegistry();
    {
      RegistryLock lock(registry);

      // get a playback node interface from the registry
      list<Response> playback_response = registry.initRequest(playback_nd);
      if (playback_response.empty())
        throw Exception("PlaybackNode is not available on host " + playback_nd.getLocation() );
      __playback = registry.requestNode(playback_response.front());

      // get a display node interface from the registry
      list<Response> display_response = registry.initRequest(display_nd);
      if (display_response.empty())
        throw Exception("Display node is not available on host" + display_nd.getLocation() );
      __display = registry.requestNode(display_response.front());

    }

    __av_sync = new MultiAudioVideoSynchronizer();
    __synchronizer = __av_sync->getCheckedInterface<IMultiAudioVideoSynchronizer>();

    // initialize the GraphBuilder
    gb.setMultiAudioVideoSynchronizer(__synchronizer);
    gb.setAudioSink(__playback);
    gb.setVideoSink(__display);
    gb.setDemuxAudioJackTag("audio");
    gb.setDemuxVideoJackTag("video");

    // create the graph represented by a composite node
    __composite = gb.createGraph(*__app);

    // if the display node is connected we know we will play a video TODO: what about video without audio?
    __with_video = __display->isInputConnected();
    debug() << "NMM video playback? " << __with_video << endl;

    // set volume for playback node
    setVolume( m_volume );

    // register the needed event listeners at the display node if video enabled
    if(__with_video) {
        __display->getParentObject()->registerEventListener(ISyncReset::syncReset_event, &__syncReset_listener);
        __display->getParentObject()->registerEventListener(IProgressListener::setProgress_event, &__setProgress_listener);
        __display->getParentObject()->registerEventListener(ITrack::endTrack_event, &__endTrack_listener);

    }
    else { // in other case at the playback node
        __playback->getParentObject()->registerEventListener(ISyncReset::syncReset_event, &__syncReset_listener);
        __playback->getParentObject()->registerEventListener(IProgressListener::setProgress_event, &__setProgress_listener);
        __playback->getParentObject()->registerEventListener(ITrack::endTrack_event, &__endTrack_listener);


        (__playback->getParentObject()->getCheckedInterface<ISynchronizedSink>())->setSynchronized(false);
    }

    __playback->getParentObject()->registerEventListener(ITrackDuration::trackDuration_event, &__trackDuration_listener);
    __display->getParentObject()->registerEventListener(ITrackDuration::trackDuration_event, &__trackDuration_listener);

    // Tell the node that implements the IProgress interface to send progress events frequently.
    IProgress_var progress(__composite->getInterface<IProgress>());
    if (progress.get()) {
      progress->sendProgressInformation(true);
      progress->setProgressInterval(1);
    }

    // minimize the buffer size to increase the frequency of progress events
    IBufferSize_var buffer_size(__composite->getInterface<IBufferSize>());
    if (buffer_size.get()) {
      buffer_size->setBufferSize(1000);
    }

    // we don't know the track length yet - we have to wait for the trackDuration event
    __track_length = 0;

    __seeking = false;

    // finally start the graph
    if(__playback->isActivated())
      __playback->reachStarted();
    if(__display->isActivated())
      __display->reachStarted();

    __composite->reachStarted();
 
    return true;
  }
  catch (const Exception& e) {
    cerr << e << endl;
    QString status = e.getComment().c_str() ;
    emit statusText( QString("NMM engine: ") + status );
  }
  catch(...) {
    emit statusText( "NMM engine: Something went wrong..." );
  }

  // loading failed, clean up 
  cleanup();

  return false;
}

bool NmmEngine::play(uint)
{
  DEBUG_BLOCK

  if (!__composite)
    return false;

  // TODO: seek to the last position if 'resume playback on startup' is enabled

  __synchronizer->wakeup();
  __state = Engine::Playing;
  emit stateChanged(Engine::Playing);

  return true;
}

void NmmEngine::cleanup()
{
  DEBUG_BLOCK

  // remove all event listeners
  if(__display && __with_video ) {
    __display->getParentObject()->removeEventListener(&__setProgress_listener);
    __display->getParentObject()->removeEventListener(&__endTrack_listener);
    __display->getParentObject()->removeEventListener(&__syncReset_listener);

    __playback->getParentObject()->removeEventListener(&__trackDuration_listener);
    __display->getParentObject()->removeEventListener(&__trackDuration_listener);

    debug() << "removed event listener for __display" << endl;
  } 
  else if (__playback ) {
    __playback->getParentObject()->removeEventListener(&__setProgress_listener);
    __playback->getParentObject()->removeEventListener(&__endTrack_listener);
    __playback->getParentObject()->removeEventListener(&__syncReset_listener);

    __playback->getParentObject()->removeEventListener(&__trackDuration_listener);
    __display->getParentObject()->removeEventListener(&__trackDuration_listener);

    debug() << "removed event listener for __playback" << endl;
  }

  if( __composite && __composite->isStarted() ) {
    __composite->reachActivated();
    debug() << "__composite STARTED -> ACTIVATED" << endl;
  }

  if( __playback && __playback->isStarted() ) {
    __playback->reachActivated();
    debug() << "__playback STARTED -> ACTIVATED " << endl;
  }

  if( __display && __display->isStarted() ) {
    __display->reachActivated();
    debug() << "__display STARTED -> ACTIVATED " << endl;
  }

  if( __composite && __composite->isActivated() ) {
    __composite->flush();
    __composite->reachConstructed();
    debug() << "__composite ACTIVATED -> CONSTRUCTED " << endl;
  }

  if( __playback && __playback->isActivated() ) {
    __playback->flush();
    __playback->reachConstructed();
    debug() << "__playback ACTIVATED -> CONSTRUCTED " << endl;
  }

  if( __display && __display->isActivated() ) {
    __display->flush();
    __display->reachConstructed();
    debug() << "__display ACTIVATED -> CONSTRUCTED " << endl;
  }

  // release the playback and video node
  ClientRegistry& registry = __app->getRegistry();
  {
    RegistryLock lock(registry);
    if(__playback) {
      registry.releaseNode(*__playback);
      debug() << "RELEASED __playback node" << endl;
    }
    if(__display) {
      registry.releaseNode(*__display);
      debug() << "RELEASED __display node" << endl;
    }
  }

  delete __composite;
  __composite = NULL;
  delete __playback;
  __playback = NULL;
  delete __display;
  __display = NULL;

  __with_video = false;

  delete __synchronizer; 
  __synchronizer = NULL;
  delete __av_sync; 
  __av_sync = NULL;

  __position = 0;
  __state = Engine::Idle;
}

void NmmEngine::stop()
{
  DEBUG_BLOCK

  cleanup();

  __state = Engine::Empty;
  emit stateChanged(Engine::Empty);

}

void NmmEngine::pause()
{
  if (!__composite)
    return;

  debug() << "pause()" << endl;
  if( __state == Engine::Playing ) {
    __synchronizer->pause();
    __state = Engine::Paused;
    emit stateChanged(Engine::Paused);
  }
  else if ( __state == Engine::Paused ) {
    __synchronizer->wakeup();
    __state = Engine::Playing;
    emit stateChanged(Engine::Playing);
  }
}

void NmmEngine::seek(uint ms)
{
  if (!__track_length)
    return;

  __seeking = true;
  __position = ms;

  ISeekable_var seek(__composite->getCheckedInterface<ISeekable>());
  if (seek.get())
    seek->seekPercentTo(Rational(ms, __track_length));
}

void NmmEngine::endOfStreamReached()
{
    DEBUG_BLOCK
    emit trackEnded();
}

uint NmmEngine::position() const
{
  return __position;
}

uint NmmEngine::length() const
{
  return __track_length;
}

bool NmmEngine::canDecode(const KURL& url) const
{
    static QStringList types;

    if (url.protocol() == "http" ) return false; 

    // the following MIME types can be decoded
    types += QString("audio/x-mp3");
    types += QString("audio/x-wav");
    types += QString("audio/ac3");
    types += QString("audio/vorbis");
    types += QString("video/mpeg");
    types += QString("video/x-msvideo");
    types += QString("video/x-ogm");

    KFileItem fileItem( KFileItem::Unknown, KFileItem::Unknown, url, false ); //false = determineMimeType straight away
    KMimeType::Ptr mimetype = fileItem.determineMimeType();

    return types.contains(mimetype->name());
}


void NmmEngine::setVolumeSW(uint percent)
{
    if( __playback )
    {
        IAudioDevice_var audio(__playback->getParentObject()->getCheckedInterface<IAudioDevice>());
        audio->setVolume( percent );
    }
}

QStringList NmmEngine::getSinkHosts( bool audio )
{
  // read locations from environment variable
  if( NmmKDEConfig::location() == NmmKDEConfig::EnumLocation::EnvironmentVariable )
  {
    QString hosts;

    if( audio )
        hosts = getenv("AUDIO_HOSTS");
    else
        hosts = getenv("VIDEO_HOSTS");

    if( hosts.isEmpty() ) { // environment variable not set
      debug() << "environment variable empty." << endl;
      return QStringList();
  }

    debug() << "locations from environment variable are => " << hosts << endl;

    return QStringList::split(":", hosts );

 } // read locations from host list
 else if( NmmKDEConfig::location() == NmmKDEConfig::EnumLocation::HostList )
 {
   // TODO: only toggled locations
    return NmmKDEConfig::hostList();
 }

  // localhost only
  return QStringList();
}

Result NmmEngine::setProgress(u_int64_t& numerator, u_int64_t& denominator)
{
  // compute the track position in milliseconds
  u_int64_t position = numerator * __track_length / denominator;

  if (__seeking)
    return SUCCESS;
  
  __position = position;

  return SUCCESS;
}

Result NmmEngine::endTrack()
{
  __state = Engine::Idle;
  __position = 0;

  // cleanup after this method returned
  QTimer::singleShot( 0, instance(), SLOT( endOfStreamReached() ) );

  return SUCCESS;
}

Result NmmEngine::syncReset()
{
  __seeking = false;
  return SUCCESS;
}

Result NmmEngine::trackDuration(Interval& duration)
{
  // we got the duration of the track, so let's convert it to milliseconds
  __track_length = duration.sec * 1000 + duration.nsec / 1000;
  kdDebug() << "NmmEngine::trackDuration " << __track_length << endl;
  return SUCCESS;
}

#include "nmm_engine.moc"
