#pragma once
#include "MediaPlayerEnums.h"
#include "MediaPlayerMorphOS.h"
#include <wtf/RefPtr.h>

#define EP_PROFILING 0
#if EP_PROFILING
#include <libeventprofiler.h>
#else 
#define EP_SCOPE(x)
#define EP_EVENT(x)
#define EP_EVENT_WAIT(x)
#define EP_EVENT_WAIT_FOR(x, y)
#define EP_EVENT_SIGNAL(x)
#define EP_EVENT_SIGNAL_ALL(x)
#define EP_EVENT_SIGNAL_ALL_IF(x, y)
#define EP_EVENT_SIGNAL_IF(x, y)
#define EP_EVENT_SIGNAL_IF_ALL(x, y)
#define EP_EVENT_SIGNAL_IF_ANY(x, y)
#define EP_EVENT_SIGNAL_ALL_IF_ANY(x, y)
#endif

namespace WebCore {

class PlatformMediaResourceLoader;
struct MediaPlayerMorphOSStreamSettings;

namespace Acinerella {

#include <wtf/Seconds.h>

class AcinerellaClient
{
public:
	virtual const MediaPlayerMorphOSStreamSettings& streamSettings() = 0;
	virtual void accInitialized(MediaPlayerMorphOSInfo info) = 0;
	virtual void accUpdated(MediaPlayerMorphOSInfo info) = 0;
	virtual void accSetNetworkState(WebCore::MediaPlayerEnums::NetworkState state, const WTF::String &error) = 0;
	virtual void accSetReadyState(WebCore::MediaPlayerEnums::ReadyState state) = 0;
	virtual void accSetBufferLength(double buffer) = 0;
	virtual void accSetPosition(double position) = 0;
    virtual void accSeeked(double position) = 0;
	virtual void accSetDuration(double duration) = 0;
	virtual void accSetVideoSize(int width, int height) = 0;
	virtual void accSetFrameCounts(unsigned decoded, unsigned dropped) = 0;
	virtual void accEnded() = 0;
	virtual void accFailed() = 0; // on initialization!
	virtual void accNextFrameReady() = 0;
	virtual void accNoFramesReady() = 0;
	virtual void accFrameUpdateNeeded() = 0;
	virtual bool accCodecSupported(const String &codec) = 0;
	virtual bool accIsURLValid(const String& url) = 0;
	
	virtual RefPtr<PlatformMediaResourceLoader> accCreateResourceLoader() = 0;
	virtual String accReferrer() = 0;
};

}
}
