/*
 * Copyright (C) 2004, 2006 Apple Inc.  All rights reserved.
 * Copyright (C) 2005, 2006 Michael Emmel mike.emmel@gmail.com
 * Copyright (C) 2017 Sony Interactive Entertainment Inc.
 * All rights reserved.
 * Copyright (C) 2017 NAVER Corp.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CurlResourceHandleDelegate.h"

#include "CurlCacheManager.h"
#include "CurlRequest.h"
#include "NetworkStorageSession.h"
#include "ResourceHandle.h"
#include "ResourceHandleClient.h"
#include "ResourceHandleInternal.h"
#include "SecurityOrigin.h"

#include <wtf/CompletionHandler.h>

#if PLATFORM(MUI)
#include "gui.h"
#undef String
#undef set
#undef get
#endif

#if USE(CURL)

namespace WebCore {

#if PLATFORM(MUI)
// Activity of each running request, keyed by its ResourceHandle. Only the main thread touches it.
static UncheckedKeyHashMap<const ResourceHandle*, NetworkActivity>& networkActivities()
{
    static NeverDestroyed<UncheckedKeyHashMap<const ResourceHandle*, NetworkActivity>> activities;
    return activities;
}

static NetworkActivity currentNetworkActivity(ResourceHandle& handle)
{
    auto it = networkActivities().find(&handle);
    return it == networkActivities().end() ? NetworkActivity { } : it->value;
}

static void pushNetworkActivityMethod(ULONG method, ResourceHandle& handle)
{
    if (app)
        methodstack_push_sync(app, 2, method, &handle);
}

// Stores the new activity and tells the UI (LEDs and network window redraw).
static void updateNetworkActivity(ResourceHandle& handle, const NetworkActivity& activity)
{
    auto it = networkActivities().find(&handle);
    if (it == networkActivities().end())
        return;
    it->value = activity;
    pushNetworkActivityMethod(MM_Network_UpdateJob, handle);
}

// The UI list keeps raw pointers to the requests, so a listed request is kept alive until it ends.
void networkActivityStarted(ResourceHandle& handle)
{
    NetworkActivity activity { };
    activity.state = NETWORK_ACTIVITY_CONNECTING;
    networkActivities().set(&handle, activity);
    handle.ref();
    pushNetworkActivityMethod(MM_Network_AddJob, handle);
}

void networkActivityEnded(ResourceHandle& handle)
{
    if (!networkActivities().remove(&handle))
        return;
    pushNetworkActivityMethod(MM_Network_RemoveJob, handle);
    handle.deref();
}
#endif

CurlResourceHandleDelegate::CurlResourceHandleDelegate(ResourceHandle& handle)
    : m_handle(handle)
{

}

void CurlResourceHandleDelegate::ref() const
{
    m_handle.ref();
}

void CurlResourceHandleDelegate::deref() const
{
    m_handle.deref();
}

bool CurlResourceHandleDelegate::cancelledOrClientless()
{
    return m_handle.cancelledOrClientless();
}

ResourceHandleClient* CurlResourceHandleDelegate::client() const
{
    return m_handle.client();
}

ResourceHandleInternal* CurlResourceHandleDelegate::d()
{
    return m_handle.getInternal();
}

void CurlResourceHandleDelegate::curlDidSendData(CurlRequest&, unsigned long long bytesSent, unsigned long long totalBytesToBeSent)
{
    ASSERT(isMainThread());

    if (cancelledOrClientless())
        return;

#if PLATFORM(MUI)
    auto activity = currentNetworkActivity(m_handle);
    activity.state = NETWORK_ACTIVITY_SENDING_DATA;
    activity.sent = bytesSent;
    activity.bodySize = totalBytesToBeSent;
    updateNetworkActivity(m_handle, activity);
#endif
    client()->didSendData(&m_handle, bytesSent, totalBytesToBeSent);
}

static void handleCookieHeaders(ResourceHandleInternal* d, const ResourceRequest& request, const CurlResponse& response)
{
    static constexpr auto setCookieHeader = "set-cookie: "_s;

    for (const auto& header : response.headers) {
        if (header.startsWithIgnoringASCIICase(setCookieHeader)) {
            const auto contents = header.right(header.length() - setCookieHeader.length());
            d->m_context->storageSession()->setCookiesFromHTTPResponse(request.firstPartyForCookies(), response.url, contents);
        }
    }
}

void CurlResourceHandleDelegate::curlDidReceiveResponse(CurlRequest& request, CurlResponse&& receivedResponse)
{
    ASSERT(isMainThread());
    ASSERT(!d()->m_defersLoading);

    if (cancelledOrClientless())
        return;

    m_response = ResourceResponse(receivedResponse);
#if PLATFORM(MUI)
    auto activity = currentNetworkActivity(m_handle);
    activity.state = NETWORK_ACTIVITY_WAITING_DATA;
    activity.received = 0;
    auto expectedLength = m_response.expectedContentLength();
    activity.total = expectedLength > 0 ? expectedLength : 0;
    updateNetworkActivity(m_handle, activity);
#endif

    updateNetworkLoadMetrics(receivedResponse.networkLoadMetrics);
    m_response.setDeprecatedNetworkLoadMetrics(Box<NetworkLoadMetrics>::create(WTFMove(receivedResponse.networkLoadMetrics)));

    handleCookieHeaders(d(), request.resourceRequest(), receivedResponse);

    if (m_response.shouldRedirect()) {
        m_handle.willSendRequest();
        return;
    }

    if (m_response.isUnauthorized() && receivedResponse.availableHttpAuth) {
        AuthenticationChallenge challenge(receivedResponse, d()->m_authFailureCount, m_response, &m_handle);
        m_handle.didReceiveAuthenticationChallenge(challenge);
        d()->m_authFailureCount++;
        return;
    }

    if (m_response.isNotModified()) {
        URL cacheUrl = m_response.url();
        cacheUrl.removeFragmentIdentifier();

        if (CurlCacheManager::singleton().getCachedResponse(cacheUrl.string(), m_response)) {
            if (d()->m_addedCacheValidationHeaders) {
                m_response.setHTTPStatusCode(200);
                m_response.setHTTPStatusText("OK"_s);
            }
        }
    }

    m_handle.didReceiveResponse(ResourceResponse(m_response), [this, protectedHandle = Ref { m_handle }] {
        // moved to avoid reverting the events order (must be header, then data)
        if (!cancelledOrClientless())
        {
            CurlCacheManager::singleton().didReceiveResponse(m_handle, m_response);
            m_handle.continueAfterDidReceiveResponse();
        }
    });
}

void CurlResourceHandleDelegate::curlDidReceiveData(CurlRequest&, Ref<SharedBuffer>&& buffer)
{
    ASSERT(isMainThread());

    if (cancelledOrClientless())
        return;

#if PLATFORM(MUI)
    auto activity = currentNetworkActivity(m_handle);
    activity.state = NETWORK_ACTIVITY_RECEIVING_DATA;
    activity.received += buffer->size();
    updateNetworkActivity(m_handle, activity);
#endif
    CurlCacheManager::singleton().didReceiveData(m_handle, buffer->span());
    client()->didReceiveBuffer(&m_handle, buffer, buffer->size());
}

void CurlResourceHandleDelegate::curlDidComplete(CurlRequest&, NetworkLoadMetrics&& metrics)
{
    ASSERT(isMainThread());
#if PLATFORM(MUI)
    networkActivityEnded(m_handle);
#endif

    if (cancelledOrClientless())
        return;

    updateNetworkLoadMetrics(metrics);

    CurlCacheManager::singleton().didFinishLoading(m_handle);
    client()->didFinishLoading(&m_handle, WTFMove(metrics));
}

void CurlResourceHandleDelegate::curlDidFailWithError(CurlRequest&, ResourceError&& resourceError, CertificateInfo&&)
{
    ASSERT(isMainThread());
#if PLATFORM(MUI)
    networkActivityEnded(m_handle);
#endif

    if (cancelledOrClientless())
        return;

    CurlCacheManager::singleton().didFail(m_handle);
    client()->didFail(&m_handle, resourceError);
}

void CurlResourceHandleDelegate::updateNetworkLoadMetrics(NetworkLoadMetrics& networkLoadMetrics)
{
    if (!d()->m_startTime)
        d()->m_startTime = networkLoadMetrics.fetchStart;

    m_handle.checkTAO(m_response);

    networkLoadMetrics.redirectStart = m_handle.startTimeBeforeRedirects();
    networkLoadMetrics.redirectCount = m_handle.redirectCount();
    networkLoadMetrics.failsTAOCheck = m_handle.failsTAOCheck();
    networkLoadMetrics.hasCrossOriginRedirect = m_handle.hasCrossOriginRedirect();
}

} // namespace WebCore

#if PLATFORM(MUI)
// User abort from the network window. The page loader only stops waiting when it gets a result, and a
// cancelled request never reports one, so report a cancellation first (WebCore does not show it as an error).
void networkActivityAbort(void *job)
{
    auto* handle = static_cast<WebCore::ResourceHandle*>(job);
    Ref protectedHandle { *handle }; // the loader's didFail may drop its reference
    if (auto* client = handle->client())
        client->didFail(handle, WebCore::ResourceError(WebCore::ResourceError::Type::Cancellation));
    handle->cancel();
}

// Aborts only the requests active when called. Aborting makes page loaders start new requests, which must survive.
void networkActivityAbortAll()
{
    Vector<Ref<WebCore::ResourceHandle>> jobs;
    for (auto* handle : WebCore::networkActivities().keys())
        jobs.append(Ref { *const_cast<WebCore::ResourceHandle*>(handle) });

    for (auto& job : jobs) {
        if (WebCore::networkActivities().contains(job.ptr()))
            networkActivityAbort(job.ptr());
    }
}

int networkActivityFor(void *job, struct NetworkActivity *out)
{
    auto it = WebCore::networkActivities().find(static_cast<const WebCore::ResourceHandle*>(job));
    if (it == WebCore::networkActivities().end())
        return 0;
    *out = it->value;
    return 1;
}
#endif

#endif
