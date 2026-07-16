#include "AcinerellaBuffer.h"
#include "MediaPlayerMorphOS.h"

#if ENABLE(VIDEO)

#include "CurlRequest.h"
#include "NetworkingContext.h"
#include "NetworkStorageSession.h"
#include "SameSiteInfo.h"
#include "SynchronousLoaderClient.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "CurlRequestClient.h"
#include "PlatformMediaResourceLoader.h"
#include <queue>
#include "AcinerellaDecoder.h"
#include "AcinerellaHLS.h"
#include "CookieJar.h"

#define D(x)
// Set to 1 for network buffer debug output on the serial console.
#define YTDBG_ENABLED 0
#if YTDBG_ENABLED
#if OS(AMIGAOS)
#include <proto/exec.h>
#include <stdarg.h>
#include <stdio.h>
// The kernel DebugPrintF mishandles 64-bit varargs (%lld shifts every following
// argument by one slot), so pre-format with the C library and emit a single %s.
static void ytdbgPrint(const char* fmt, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	DebugPrintF("%s", buffer);
}
#define YTDBG(x) ytdbgPrint x
#else
#define YTDBG(x) dprintf x
#endif
#else
#define YTDBG(x)
#endif

namespace WebCore {
namespace Acinerella {

class AcinerellaNetworkBufferInternal : public AcinerellaNetworkBuffer, public CurlRequestClient
{
public:
	AcinerellaNetworkBufferInternal(AcinerellaNetworkBufferResourceLoaderProvider *resourceProvider, const String &url, size_t readAhead)
		: AcinerellaNetworkBuffer(resourceProvider, url, readAhead)
	{
	
	}
	
	~AcinerellaNetworkBufferInternal()
	{
		if (m_curlRequest)
		{
// dprintf("-- AcinerellaNetworkBufferInternal ORPHANED!!\n");
			m_curlRequest->cancel();
		}
	}

	void ref() override { ThreadSafeRefCounted<AcinerellaNetworkBuffer>::ref(); }
	void deref() override { ThreadSafeRefCounted<AcinerellaNetworkBuffer>::deref(); }

	void start(uint64_t from = 0) override
	{
		D(dprintf("%s(%p) - from %llu\n", __PRETTY_FUNCTION__, this, from));

		if (m_dead)
			return;

		if (m_curlRequest)
			m_curlRequest->cancel();
		m_curlRequest = nullptr;

		{
			auto lock = holdLock(m_bufferLock);
			while (!m_buffer.empty())
				m_buffer.pop();

			m_finishedLoading = false;
			m_didFailLoading = false;

			m_bufferRead = 0;
			m_bufferPositionAbs = from;
			m_bufferSize = 0;
			m_redirectCount = 0;
			m_isPaused = false;
		}

		m_request = ResourceRequest(m_url);
		if (m_provider)
		{
			URL requestURL = URL(URL(), m_url);
			m_request.setAllowCookies(true);
			m_request.setFirstPartyForCookies(requestURL);
			m_request.setHTTPReferrer(m_provider->referrer());
		}
		m_request.clearHTTPAcceptEncoding();
		m_curlRequest = createCurlRequest(m_request);
		if (m_curlRequest)
		{
			m_curlRequest->setResumeOffset(static_cast<long long>(m_bufferPositionAbs));
			m_curlRequest->start();
			YTDBG(("[YTDBG][NB] start request created/resumed from=%llu main=%d urlLen=%u\n", m_bufferPositionAbs, isMainThread(), unsigned(m_url.length())));
		}
		else
		{
			YTDBG(("[YTDBG][NB] start request creation failed\n"));
			m_didFailLoading = true;
			m_finishedLoading = true;
			m_eventSemaphore.signal();
		}
	}

	void stop() override
	{
		D(dprintf("%s(%p)\n", __PRETTY_FUNCTION__, this));

		if (m_curlRequest)
			m_curlRequest->cancel();
		m_curlRequest = nullptr;
		m_finishedLoading = true;
		m_isPaused = false;
		
		m_eventSemaphore.signal();
		D(dprintf("%s(%p) ..\n", __PRETTY_FUNCTION__, this));
	}
	
	int64_t position() override
	{
		return int64_t(m_bufferPositionAbs);
	}

	bool canSeek() override
	{
		return m_length > 0;
	}

	int read(uint8_t *outBuffer, int size, int64_t readPosition) override
	{
		D(dprintf("%s(%p): requested %ld from %lld (current %lld max %lld)\n", "nbRead", this, size, readPosition, m_bufferPositionAbs, m_length));
		int sizeWritten = 0;
		int sizeLeft = size;
		bool resume = false;
		unsigned waitRounds = 0;
		unsigned stallRestartCount = 0;
		constexpr unsigned maxIdleWaitRounds = 18; // 18 * 10s = 180s
		constexpr unsigned stallRestartEveryRounds = 6;
		constexpr unsigned maxStallRestarts = 2;

		if (-1 != readPosition && int64_t(m_bufferPositionAbs) != readPosition)
		{
			m_seekProcessed = false;

			D(dprintf("%s: seek to %llu\n", "nbRead", readPosition));

			WTF::callOnMainThread([this, seekTo = readPosition, protect = makeRef(*this)]() {
				start(seekTo);
				m_seekProcessed = true;
			});

			while (!m_dead && !m_seekProcessed)
			{
				waitRounds++;
				if (!(waitRounds % 3))
					YTDBG(("[YTDBG][NB] waiting seek process rounds=%u dead=%d\n", waitRounds, m_dead));
				m_eventSemaphore.waitFor(10_s);
			}

			D(dprintf("%s: seek to %llu processed...\n", "nbRead", m_bufferPositionAbs));
		}

		while (sizeWritten < size)
		{
			bool canReadMore = false;

			{
				auto lock = holdLock(m_bufferLock);
				if (!m_buffer.empty())
				{
					auto buffer = m_buffer.front();
					int write = std::min(int(buffer->size() - m_bufferRead), sizeLeft);

					memcpy(outBuffer + sizeWritten, buffer->data() + m_bufferRead, write);
					m_bufferRead += write;
					m_bufferPositionAbs += write;
					sizeLeft -= write;
					sizeWritten += write;

					D(dprintf("%s: read %d from current block\n", "nbRead", write));

					if (m_bufferRead == int(buffer->size()))
					{
						m_buffer.pop();
						m_bufferSize -= m_bufferRead;
						m_bufferRead = 0;
						
						// Check if we don't need to resume reading...
						if (m_isPaused && m_bufferRead < (m_readAhead / 2))
						{
							resume = true;
						}
					}
					
					canReadMore = !m_buffer.empty();
				}
			}
			
			if (sizeWritten < size && !m_finishedLoading && !canReadMore)
			{
				waitRounds++;
				if (!(waitRounds % 3))
					YTDBG(("[YTDBG][NB] waiting data rounds=%u wrote=%d/%d paused=%d finished=%d fail=%d bufSize=%d hasReq=%d\n", waitRounds, sizeWritten, size, m_isPaused, m_finishedLoading, m_didFailLoading, m_bufferSize, !!m_curlRequest));

				if (!m_isPaused && m_curlRequest && !(waitRounds % stallRestartEveryRounds) && stallRestartCount < maxStallRestarts)
				{
					stallRestartCount++;
					YTDBG(("[YTDBG][NB] stalled request restart #%u at abs=%llu\n", stallRestartCount, m_bufferPositionAbs));
					WTF::callOnMainThread([this, from = m_bufferPositionAbs, protect = makeRef(*this)]() {
						start(from);
					});
					m_eventSemaphore.waitFor(10_s);
					continue;
				}

				if (m_didFailLoading)
				{
					YTDBG(("[YTDBG][NB] abort read due to load failure\n"));
					return AcinerellaNetworkBuffer::eRead_EOF;
				}

				if (waitRounds >= maxIdleWaitRounds)
				{
					YTDBG(("[YTDBG][NB] abort read due to timeout rounds=%u\n", waitRounds));
					m_didFailLoading = true;
					m_finishedLoading = true;
					WTF::callOnMainThread([this, protect = makeRef(*this)]() {
						if (m_curlRequest)
						{
							m_curlRequest->cancel();
							m_curlRequest = nullptr;
						}
					});
					return AcinerellaNetworkBuffer::eRead_EOF;
				}

				WTF::callOnMainThread([this, protect = makeRef(*this)]() {
					continueBuffering();
				});
				m_eventSemaphore.waitFor(10_s);
			}
			else if (m_finishedLoading && !canReadMore)
			{
				break;
			}
		}

		if (resume)
		{
			WTF::callOnMainThread([this, protect = makeRef(*this)]() {
				continueBuffering();
			});
		}

		D(dprintf("%s(%p): written %ld\n", "nbRead", this, sizeWritten));
		if (sizeWritten <= 0)
			YTDBG(("[YTDBG][NB] read returned %d finished=%d fail=%d dead=%d\n", sizeWritten, m_finishedLoading, m_didFailLoading, m_dead));
		return sizeWritten;
	}
	
	void continueBuffering()
	{
		D(dprintf("%s(%p): pau %d underbuf %d fini %d\n", __PRETTY_FUNCTION__, this, m_isPaused, m_bufferRead < (m_readAhead / 2), m_finishedLoading));
		if (m_isPaused && m_bufferRead < (m_readAhead / 2) && !m_finishedLoading && !m_dead)
		{
			if (m_curlRequest)
			{
				YTDBG(("[YTDBG][NB] continueBuffering resume existing request\n"));
				m_curlRequest->resume();
			}
			else
			{
				uint64_t abs;

				{
					auto lock = holdLock(m_bufferLock);
					abs = m_bufferPositionAbs + m_bufferSize;
				}

				m_request = ResourceRequest(m_url);
				if (m_provider)
				{
					URL requestURL = URL(URL(), m_url);
					m_request.setAllowCookies(true);
					m_request.setFirstPartyForCookies(requestURL);
					m_request.setHTTPReferrer(m_provider->referrer());
				}
				m_request.clearHTTPAcceptEncoding();
				m_curlRequest = createCurlRequest(m_request);
				if (m_curlRequest)
				{
					m_curlRequest->setResumeOffset(static_cast<long long>(abs));
					YTDBG(("[YTDBG][NB] continueBuffering start new request abs=%llu\n", abs));
					m_curlRequest->start();
				}
				else
				{
					YTDBG(("[YTDBG][NB] continueBuffering failed to create request\n"));
					m_didFailLoading = true;
					m_finishedLoading = true;
					m_eventSemaphore.signal();
					return;
				}
			}
			D(dprintf("%s(%p): resuming...\n", __PRETTY_FUNCTION__, this));
			m_isPaused = false;
		}
	}
	
	Ref<CurlRequest> createCurlRequest(ResourceRequest&request)
	{
		auto context = MediaPlayerMorphOSSettings::settings().m_networkingContextForRequests;
		if (context)
		{
			auto& storageSession = *context->storageSession();
			auto includeSecureCookies = request.url().protocolIs("https") ? IncludeSecureCookies::Yes : IncludeSecureCookies::No;
			String cookieHeaderField = storageSession.cookieRequestHeaderFieldValue(request.firstPartyForCookies(), SameSiteInfo::create(request), request.url(), std::nullopt, std::nullopt, includeSecureCookies, ShouldAskITP::Yes, ShouldRelaxThirdPartyCookieBlocking::No).first;
			if (!cookieHeaderField.isEmpty())
				request.addHTTPHeaderField(HTTPHeaderName::Cookie, cookieHeaderField);
		}

		auto curlRequest = CurlRequest::create(WTFMove(request), *this);
		return curlRequest;
	}

	void curlDidSendData(CurlRequest&, unsigned long long, unsigned long long) override
	{
		D(dprintf("%s(%p)\n", __PRETTY_FUNCTION__, this));
	}

	inline bool shouldRedirectAsGET(const ResourceRequest& request, bool crossOrigin)
	{
		if ((request.httpMethod() == "GET") || (request.httpMethod() == "HEAD"))
			return false;

		if (!request.url().protocolIsInHTTPFamily())
			return true;

		if (m_response.isSeeOther())
			return true;

		if ((m_response.isMovedPermanently() || m_response.isFound()) && (request.httpMethod() == "POST"))
			return true;

		if (crossOrigin && (request.httpMethod() == "DELETE"))
			return true;

		return false;
	}
	
	void curlDidReceiveResponse(CurlRequest& request, CurlResponse&& response) override
	{
		D(dprintf("%s(%p): %s\n", __PRETTY_FUNCTION__, this, m_url.utf8().data()));
		YTDBG(("[YTDBG][NB] curlDidReceiveResponse reqMatch=%d\n", m_curlRequest.get() == &request));
		if (m_curlRequest.get() == &request)
		{
			D(dprintf("%s(%p)..\n", __PRETTY_FUNCTION__, this));
			m_response = ResourceResponse(response);
			
			// only set on 1st request (or when we're reading from pos=0)
			if (0 == m_bufferPositionAbs)
				m_length = static_cast<int64_t>(m_response.expectedContentLength());
			{
				// Keep the URL short: the kernel debug output cannot take multi-KB lines
				auto shortURL = m_response.url().string().left(160).utf8();
				YTDBG(("[YTDBG][NB] response code=%d expectedLen=%lld url=%s%s\n", m_response.httpStatusCode(), (long long)m_length, shortURL.data(), m_response.url().string().length() > 160 ? "..." : ""));
			}

			if (m_response.shouldRedirect())
			{
				static const int maxRedirects = 20;

				if (m_redirectCount++ > maxRedirects)
				{
					m_didFailLoading = true;
					m_eventSemaphore.signal();
					return;
				}

				String location = m_response.httpHeaderField(HTTPHeaderName::Location);
				URL newURL = URL(m_response.url(), location);
				bool crossOrigin = !protocolHostAndPortAreEqual(m_request.url(), newURL);

				ResourceRequest newRequest = m_request;
				newRequest.setURL(newURL);

				if (shouldRedirectAsGET(newRequest, crossOrigin)) {
					newRequest.setHTTPMethod("GET");
					newRequest.setHTTPBody(nullptr);
					newRequest.clearHTTPContentType();
				}

				if (crossOrigin) {
					// If the network layer carries over authentication headers from the original request
					// in a cross-origin redirect, we want to clear those headers here.
					newRequest.clearHTTPAuthorization();
					newRequest.clearHTTPOrigin();
				}

				m_curlRequest->cancel();
				m_curlRequest = createCurlRequest(newRequest);
				if (m_curlRequest)
				{
					m_curlRequest->setResumeOffset(static_cast<long long>(m_bufferPositionAbs));
					m_curlRequest->start();
				}

				D(dprintf("%s(%p): redirected to %s\n", __PRETTY_FUNCTION__, this, location.utf8().data()));
				return;
			}
			
			request.completeDidReceiveResponse();
		}
	}
	
	void curlDidReceiveBuffer(CurlRequest& request, Ref<SharedBuffer>&&buffer) override
	{
		D(dprintf("%s(%p): %d bytes, currently buffered size: %d\n", __PRETTY_FUNCTION__, this, buffer->size(), m_bufferSize));
		if (m_curlRequest.get() == &request)
		{
			if (buffer->size())
			{
				{
					auto lock = holdLock(m_bufferLock);
					m_bufferSize += buffer->size();
					m_buffer.push(RefPtr<SharedBuffer>(WTFMove(buffer)));

					if (m_bufferSize > m_readAhead && !m_isPaused)
					{
						if (m_curlRequest)
						{
							D(dprintf("%s: suspending...\n", __PRETTY_FUNCTION__));
							m_curlRequest->suspend();
							m_isPaused = true;
						}
					}
				}

				m_eventSemaphore.signal();
			}
		}
	}
	
	void curlDidComplete(CurlRequest& request, NetworkLoadMetrics&&) override
	{
		D(dprintf("%s(%p): %s %lld\n", __PRETTY_FUNCTION__, this, m_url.utf8().data(), m_length));
		YTDBG(("[YTDBG][NB] curl complete len=%lld\n", m_length));
		if (m_curlRequest.get() == &request)
		{
			m_finishedLoading = true;
			m_eventSemaphore.signal();
			m_curlRequest->cancel();
			m_curlRequest = nullptr;
		}
	}
	
	void curlDidFailWithError(CurlRequest& request, ResourceError&& error, CertificateInfo&&) override
	{
		D(dprintf("%s(%p)\n", __PRETTY_FUNCTION__, this));
		auto failingURL = error.failingURL().string().left(160).utf8();
		auto domain = error.domain().left(80).utf8();
		auto desc = error.localizedDescription().left(160).utf8();
		YTDBG(("[YTDBG][NB] curl fail code=%d domain=%s url=%s desc=%s\n", error.errorCode(), domain.data(), failingURL.data(), desc.data()));
		if (m_curlRequest.get() == &request)
		{
			m_curlRequest->cancel();
			m_curlRequest = nullptr;
			m_isPaused = true;
			m_didFailLoading = true;
			m_finishedLoading = true;
			m_eventSemaphore.signal();
		}
	}
protected:
	ResourceRequest                  m_request;
	ResourceResponse                 m_response;
	unsigned                         m_redirectCount = 0;
	BinarySemaphore                  m_eventSemaphore;
	RefPtr<CurlRequest>              m_curlRequest;
	Lock                             m_bufferLock;
	std::queue<RefPtr<SharedBuffer>> m_buffer;
	// pos within the front() chunk
	int                              m_bufferRead = 0;
	// abs position in the stream that we've read (not in the buffer anymore)
	uint64_t                         m_bufferPositionAbs = 0;
	// size of all chunks on the list (- m_bufferRead)
	int                              m_bufferSize = 0;
	bool                             m_finishedLoading = false;
	bool                             m_didFailLoading = false;
	bool                             m_isPaused = false;
	bool                             m_seekProcessed = true;
};

#if 0
class AcinerellaNetworkBufferPlatformMediaResourceLoader : public AcinerellaNetworkBuffer
{
	class AcinerellaPlatformMediaResourceClient : public PlatformMediaResourceClient
	{
		WTF_MAKE_FAST_ALLOCATED;
		WTF_MAKE_NONCOPYABLE(AcinerellaPlatformMediaResourceClient);
	public:
		AcinerellaPlatformMediaResourceClient()
		{
		}

		void dataReceived(PlatformMediaResource&, const char* bytes, int size) override
		{
			D(dprintf("%s(%p): %ld\n", __PRETTY_FUNCTION__, this, size));
		}
		
		void accessControlCheckFailed(PlatformMediaResource&, const ResourceError&) override
		{
			D(dprintf("%s(%p)\n", __PRETTY_FUNCTION__, this));
		}
		
		void loadFailed(PlatformMediaResource&, const ResourceError& error) override
		{
			D(dprintf("%s(%p) %s %d '%s'\n", __PRETTY_FUNCTION__, this, error.domain().utf8().data(), error.errorCode(), error.localizedDescription().utf8().data()));
		}
		
		void loadFinished(PlatformMediaResource&) override
		{
			D(dprintf("%s(%p)\n", __PRETTY_FUNCTION__, this));
		}
	};

public:
	AcinerellaNetworkBufferPlatformMediaResourceLoader(AcinerellaNetworkBufferResourceLoaderProvider *resourceProvider, const String &url, size_t readAhead)
		: AcinerellaNetworkBuffer(resourceProvider, url, readAhead)
	{
	
	}
	
	~AcinerellaNetworkBufferPlatformMediaResourceLoader()
	{
	
	}
	
	void start(uint64_t from) override
	{
		stop();
		
		D(dprintf("%s(%p): %s\n", __PRETTY_FUNCTION__, this, m_url.utf8().data()));
		
		if (m_provider)
		{
			URL url = URL(URL(), m_url);
			ResourceRequest request(url);
			request.setAllowCookies(true);
			request.setFirstPartyForCookies(url);
			request.setHTTPReferrer(m_provider->referrer());
			request.clearHTTPAcceptEncoding();

    		if (m_url.contains("movies.apple.com") || m_url.contains("trailers.apple.com"))
    		{
        		request.setHTTPUserAgent("Quicktime/7.6.6");
			}
			
			if (0 != from)
			{
				String range = "bytes=";
				range.append(String::number(from));
				range.append("-");
				request.setHTTPHeaderField(HTTPHeaderName::Range, range);
			}
			
			request.setHTTPHeaderField(HTTPHeaderName::IcyMetadata, "1");

			if (!m_loader)
				m_loader = m_provider->createResourceLoader();
			
			if (!!m_loader)
			{
				PlatformMediaResourceLoader::LoadOptions loadOptions = 0;
				if (request.url().protocolIsBlob())
				{
					loadOptions |= PlatformMediaResourceLoader::LoadOption::BufferData;
				}

				D(dprintf("%s(%p): isBlob %d\n", __PRETTY_FUNCTION__, this, request.url().protocolIsBlob()));

				m_resource = m_loader->requestResource(ResourceRequest(request), loadOptions);
				if (!!m_resource)
				{
					m_resource->setClient(makeUnique<AcinerellaPlatformMediaResourceClient>());
				}
			}
		}
	}
	
	void stop()
	{
		if (!!m_resource)
		{
			m_resource->stop();
			m_resource = nullptr;
		}
	}
	
	int read(uint8_t *outBuffer, int size, int64_t position) override
	{
		return -1;
	}
	// MediaResourceLoader::requestResource
	// static gboolean webKitWebSrcMakeRequest(GstBaseSrc* baseSrc, bool notifyAsyncCompletion)
	
private:
	RefPtr<PlatformMediaResourceLoader> m_loader;
	RefPtr<PlatformMediaResource>       m_resource;
};
#endif

AcinerellaNetworkBuffer::AcinerellaNetworkBuffer(AcinerellaNetworkBufferResourceLoaderProvider *resourceProvider, const String &url, size_t readAhead)
	: m_provider(resourceProvider)
	, m_url(url)
	, m_readAhead(readAhead)
{
	D(dprintf("%s(%p) - %s\n", __PRETTY_FUNCTION__, this, url.utf8().data()));
	if (m_provider)
		m_provider->ref();
}

AcinerellaNetworkBuffer::~AcinerellaNetworkBuffer()
{
	if (m_provider)
		m_provider->deref();
}

void AcinerellaNetworkBuffer::die()
{
	if (m_provider)
		m_provider->deref();
	m_provider = nullptr;
	m_dead = true;
	stop();
}

RefPtr<AcinerellaNetworkBuffer> AcinerellaNetworkBuffer::create(AcinerellaNetworkBufferResourceLoaderProvider *resourceProvider, const String &url, size_t readAhead)
{
#if 0
	if (startsWithLettersIgnoringASCIICase(url, "blob:"))
		return RefPtr<AcinerellaNetworkBuffer>(WTF::adoptRef(*new AcinerellaNetworkBufferPlatformMediaResourceLoader(resourceProvider, url, readAhead)));
#endif
	if (url.contains("m3u8"))
		return RefPtr<AcinerellaNetworkBuffer>(WTF::adoptRef(*new AcinerellaNetworkBufferHLS(resourceProvider, url, readAhead)));
	return RefPtr<AcinerellaNetworkBuffer>(WTF::adoptRef(*new AcinerellaNetworkBufferInternal(resourceProvider, url, readAhead)));
}

RefPtr<AcinerellaNetworkBuffer> AcinerellaNetworkBuffer::createDisregardingFileType(AcinerellaNetworkBufferResourceLoaderProvider *resourceProvider, const String &url, size_t readAhead)
{
	return RefPtr<AcinerellaNetworkBuffer>(WTF::adoptRef(*new AcinerellaNetworkBufferInternal(resourceProvider, url, readAhead)));
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class AcinerellaNetworkFileRequestInternal : public AcinerellaNetworkFileRequest, CurlRequestClient
{
public:
	AcinerellaNetworkFileRequestInternal(const String &url, Function<void(bool)>&& onFinished)
		: AcinerellaNetworkFileRequest(url, WTFMove(onFinished))
	{
		m_request = ResourceRequest(m_url);
		m_curlRequest = createCurlRequest(m_request);

		if (m_curlRequest)
		{
			m_curlRequest->start();
		}
		else
		{
			if (m_onFinished)
				m_onFinished(false);
			m_onFinished = nullptr;
		}
	}

	void cancel() override
	{
		if (m_curlRequest)
			m_curlRequest->cancel();
		m_onFinished = nullptr;
	}

	RefPtr<SharedBuffer> buffer() override { return m_buffer; }

	Ref<CurlRequest> createCurlRequest(ResourceRequest&request)
	{
		auto context = MediaPlayerMorphOSSettings::settings().m_networkingContextForRequests;
		if (context)
		{
			auto& storageSession = *context->storageSession();
			auto includeSecureCookies = request.url().protocolIs("https") ? IncludeSecureCookies::Yes : IncludeSecureCookies::No;
			String cookieHeaderField = storageSession.cookieRequestHeaderFieldValue(request.firstPartyForCookies(), SameSiteInfo::create(request), request.url(), std::nullopt, std::nullopt, includeSecureCookies, ShouldAskITP::Yes, ShouldRelaxThirdPartyCookieBlocking::No).first;
			if (!cookieHeaderField.isEmpty())
				request.addHTTPHeaderField(HTTPHeaderName::Cookie, cookieHeaderField);
		}

		auto curlRequest = CurlRequest::create(WTFMove(request), *this);
		return curlRequest;
	}

	void curlDidSendData(CurlRequest&, unsigned long long, unsigned long long) override
	{
	}

	inline bool shouldRedirectAsGET(const ResourceRequest& request, bool crossOrigin)
	{
		if ((request.httpMethod() == "GET") || (request.httpMethod() == "HEAD"))
			return false;

		if (!request.url().protocolIsInHTTPFamily())
			return true;

		if (m_response.isSeeOther())
			return true;

		if ((m_response.isMovedPermanently() || m_response.isFound()) && (request.httpMethod() == "POST"))
			return true;

		if (crossOrigin && (request.httpMethod() == "DELETE"))
			return true;

		return false;
	}
	
	void curlDidReceiveResponse(CurlRequest& request, CurlResponse&& response) override
	{
		D(dprintf("%s(%p): %s\n", __PRETTY_FUNCTION__, this, m_url.utf8().data()));
		if (m_curlRequest.get() == &request)
		{
			D(dprintf("%s(%p)..\n", __PRETTY_FUNCTION__, this));
			m_response = ResourceResponse(response);

			if (m_response.shouldRedirect())
			{
				static const int maxRedirects = 20;

				if (m_redirectCount++ > maxRedirects)
				{
					if (m_onFinished)
						m_onFinished(false);
					m_onFinished = nullptr;
					return;
				}

				String location = m_response.httpHeaderField(HTTPHeaderName::Location);
				URL newURL = URL(m_response.url(), location);
				bool crossOrigin = !protocolHostAndPortAreEqual(m_request.url(), newURL);

				ResourceRequest newRequest = m_request;
				newRequest.setURL(newURL);

				if (shouldRedirectAsGET(newRequest, crossOrigin)) {
					newRequest.setHTTPMethod("GET");
					newRequest.setHTTPBody(nullptr);
					newRequest.clearHTTPContentType();
				}

				if (crossOrigin) {
					// If the network layer carries over authentication headers from the original request
					// in a cross-origin redirect, we want to clear those headers here.
					newRequest.clearHTTPAuthorization();
					newRequest.clearHTTPOrigin();
				}

				m_curlRequest->cancel();
				m_curlRequest = createCurlRequest(newRequest);
				if (m_curlRequest)
				{
					m_curlRequest->start();
				}
				else
				{
					if (m_onFinished)
						m_onFinished(false);
					m_onFinished = nullptr;
				}

				D(dprintf("%s(%p): redirected to %s\n", __PRETTY_FUNCTION__, this, location.utf8().data()));
				return;
			}
			
			request.completeDidReceiveResponse();
		}
	}
	
	void curlDidReceiveBuffer(CurlRequest& request, Ref<SharedBuffer>&&buffer) override
	{
		if (m_curlRequest.get() == &request)
		{
			if (buffer->size())
			{
				D(dprintf("%s(%p): append %d\n", __PRETTY_FUNCTION__, this, buffer->size()));
				if (!m_buffer)
					m_buffer = RefPtr<SharedBuffer>(WTFMove(buffer));
				else
					m_buffer->append(WTFMove(buffer));
			}
		}
	}
	
	void curlDidComplete(CurlRequest& request, NetworkLoadMetrics&&) override
	{
		D(dprintf("%s(%p): %s %d OK %d onfini %d\n", __PRETTY_FUNCTION__, this, m_url.utf8().data(), m_buffer?m_buffer->size():0, m_curlRequest.get() == &request, !!m_onFinished));
		if (m_curlRequest.get() == &request)
		{
			if (m_onFinished)
				m_onFinished(true);
			m_onFinished = nullptr;
		}
	}
	
	void curlDidFailWithError(CurlRequest& request, ResourceError&&, CertificateInfo&&) override
	{
		D(dprintf("%s(%p)\n", __PRETTY_FUNCTION__, this));
		if (m_curlRequest.get() == &request)
		{
			if (m_onFinished)
				m_onFinished(false);
			m_onFinished = nullptr;
		}
	}

	void ref() override { ThreadSafeRefCounted<AcinerellaNetworkFileRequest>::ref(); }
	void deref() override { ThreadSafeRefCounted<AcinerellaNetworkFileRequest>::deref(); }

protected:
	ResourceRequest      m_request;
	ResourceResponse     m_response;
	unsigned             m_redirectCount = 0;
	BinarySemaphore      m_eventSemaphore;
	RefPtr<CurlRequest>  m_curlRequest;
	RefPtr<SharedBuffer> m_buffer;
};

RefPtr<AcinerellaNetworkFileRequest> AcinerellaNetworkFileRequest::create(const String &url, Function<void(bool)>&& onFinished)
{
	return adoptRef(*new AcinerellaNetworkFileRequestInternal(url, WTFMove(onFinished)));
}

}
}
#endif
