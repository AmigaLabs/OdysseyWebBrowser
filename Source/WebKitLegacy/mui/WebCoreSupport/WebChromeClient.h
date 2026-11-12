/*
 * Copyright (C) 2008 Pleyo.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Pleyo nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PLEYO AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PLEYO OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <WebCore/ChromeClient.h>
#include <WebCore/DataListSuggestionPicker.h>
#include <WebCore/FocusDirection.h>
#include <WebCore/GraphicsContext.h>
#include <WebCore/ScrollTypes.h>
#include <wtf/Forward.h>
#include <wtf/RunLoop.h>
#include <memory>

#include <clib/debug_protos.h>

class WebDesktopNotificationsDelegate;
class WebView;

namespace WebCore {
    class Geolocation;
    class NotificationClient;
    class GraphicsLayer;
    class LocalFrame;
}

class WebChromeClient final : public WebCore::ChromeClient {
public:
    WebChromeClient(WebView*);

    void chromeDestroyed() final;

    void setWindowRect(const WebCore::FloatRect&) final;
    WebCore::FloatRect windowRect() const final;
    
    WebCore::FloatRect pageRect() const final;

    void focus() final;
    void unfocus() final;

    bool canTakeFocus(WebCore::FocusDirection) const final;
    void takeFocus(WebCore::FocusDirection) final;

    void focusedElementChanged(WebCore::Element*) final;
    void focusedFrameChanged(WebCore::Frame*) final;

    RefPtr<WebCore::Page> createWindow(WebCore::LocalFrame&, const WTF::String& openedMainFrameName, const WebCore::WindowFeatures&, const WebCore::NavigationAction&) final;
    void show() final;

    bool canRunModal() const final;
    void runModal() final;

    void setToolbarsVisible(bool) final;
    bool toolbarsVisible() const final;
    
    void setStatusbarVisible(bool) final;
    bool statusbarVisible() const final;
    
    void setScrollbarsVisible(bool) final;
    bool scrollbarsVisible() const final;
    
    void setMenubarVisible(bool) final;
    bool menubarVisible() const final;

    void setResizable(bool) final;

    void addMessageToConsole(JSC::MessageSource, JSC::MessageLevel, const WTF::String& message, unsigned lineNumber, unsigned columnNumber, const WTF::String& url) final;

    bool canRunBeforeUnloadConfirmPanel() final;
    bool runBeforeUnloadConfirmPanel(const WTF::String& message, WebCore::LocalFrame&) final;

    void closeWindow() final;

    void rootFrameAdded(const WebCore::LocalFrame&) final { }
    void rootFrameRemoved(const WebCore::LocalFrame&) final { }

    void runJavaScriptAlert(WebCore::LocalFrame&, const WTF::String&) final;
    bool runJavaScriptConfirm(WebCore::LocalFrame&, const WTF::String&) final;
    bool runJavaScriptPrompt(WebCore::LocalFrame&, const WTF::String& message, const WTF::String& defaultValue, WTF::String& result) final;

    WebCore::KeyboardUIMode keyboardUIMode() final;

    bool hoverSupportedByPrimaryPointingDevice() const final { return true; }
    bool hoverSupportedByAnyAvailablePointingDevice() const final { return true; }
    std::optional<WebCore::PointerCharacteristics> pointerCharacteristicsOfPrimaryPointingDevice() const final { return WebCore::PointerCharacteristics::Fine; }
    OptionSet<WebCore::PointerCharacteristics> pointerCharacteristicsOfAllAvailablePointingDevices() const final { return WebCore::PointerCharacteristics::Fine; }

    void invalidateRootView(const WebCore::IntRect&) final;
    void invalidateContentsAndRootView(const WebCore::IntRect&) final;
    void invalidateContentsForSlowScroll(const WebCore::IntRect&) final;
    void scroll(const WebCore::IntSize& scrollDelta, const WebCore::IntRect& rectToScroll, const WebCore::IntRect& clipRect) final;

    WebCore::IntPoint screenToRootView(const WebCore::IntPoint&) const final;
    WebCore::IntPoint rootViewToScreen(const WebCore::IntPoint& point) const final { return point; }
    WebCore::IntRect rootViewToScreen(const WebCore::IntRect&) const final;
    WebCore::IntPoint accessibilityScreenToRootView(const WebCore::IntPoint&) const final;
    WebCore::IntRect rootViewToAccessibilityScreen(const WebCore::IntRect&) const final;
    PlatformPageClient platformPageClient() const final;
    void contentsSizeChanged(WebCore::LocalFrame&, const WebCore::IntSize&) const final;
    void intrinsicContentsSizeChanged(const WebCore::IntSize&) const final;

    void mouseDidMoveOverElement(const WebCore::HitTestResult&, OptionSet<WebCore::PlatformEventModifier>, const WTF::String&, WebCore::TextDirection) final;
    bool shouldUnavailablePluginMessageBeButton(WebCore::PluginUnavailabilityReason) const final;
    void unavailablePluginButtonClicked(WebCore::Element&, WebCore::PluginUnavailabilityReason) const final;

    void print(WebCore::LocalFrame&, const WebCore::StringWithDirection&) final;

    void exceededDatabaseQuota(WebCore::LocalFrame&, const WTF::String&, WebCore::DatabaseDetails) final;

    void reachedMaxAppCacheSize(int64_t spaceNeeded) final;
    void reachedApplicationCacheOriginQuota(WebCore::SecurityOrigin&, int64_t totalSpaceNeeded) final;

    RefPtr<WebCore::ColorChooser> createColorChooser(WebCore::ColorChooserClient&, const WebCore::Color&) final;
    RefPtr<WebCore::DataListSuggestionPicker> createDataListSuggestionPicker(WebCore::DataListSuggestionsClient&) final { return nullptr; }
    bool canShowDataListSuggestionLabels() const final { return false; }

    void runOpenPanel(WebCore::LocalFrame&, WebCore::FileChooser&) final;
    void loadIconForFiles(const Vector<WTF::String>&, WebCore::FileIconLoader&) final;

    void setCursor(const WebCore::Cursor&) final;
    void setCursorHiddenUntilMouseMoves(bool) final;

    // Pass 0 as the GraphicsLayer to detatch the root layer.
    void attachRootGraphicsLayer(WebCore::LocalFrame&, WebCore::GraphicsLayer*) final;
    void attachViewOverlayGraphicsLayer(WebCore::GraphicsLayer*) final;
    // Sets a flag to specify that the next time content is drawn to the window,
    // the changes appear on the screen in synchrony with updates to GraphicsLayers.
    void setNeedsOneShotDrawingSynchronization() final { }
    // Sets a flag to specify that the view needs to be updated, so we need
    // to do an eager layout before the drawing.
    void triggerRenderingUpdate() final;

    void localStorageCreatedForDocument(const WebCore::LocalFrame&, WebCore::Storage*) const final { }

#if ENABLE(VIDEO)
    bool supportsVideoFullscreen(WebCore::HTMLMediaElementEnums::VideoFullscreenMode) final;
    void enterVideoFullscreenForVideoElement(WebCore::HTMLVideoElement&, WebCore::HTMLMediaElementEnums::VideoFullscreenMode, bool) final;
    void exitVideoFullscreenForVideoElement(WebCore::HTMLVideoElement&, WTF::CompletionHandler<void(bool)>&& = [](bool) { }) final;
#endif

    bool selectItemWritingDirectionIsNatural() final;
    bool selectItemAlignmentFollowsMenuWritingDirection() final;
    RefPtr<WebCore::PopupMenu> createPopupMenu(WebCore::PopupMenuClient&) const final;
    RefPtr<WebCore::SearchPopupMenu> createSearchPopupMenu(WebCore::PopupMenuClient&) const final;

#if ENABLE(FULLSCREEN_API)
    bool supportsFullScreenForElement(const WebCore::Element&, bool withKeyboard) final;
    void enterFullScreenForElement(WebCore::Element&, WebCore::HTMLMediaElementEnums::VideoFullscreenMode, CompletionHandler<void(WebCore::ExceptionOr<void>)>&& willEnterFullscreen, CompletionHandler<bool(bool)>&& didEnterFullscreen) final;
    void exitFullScreenForElement(WebCore::Element*, CompletionHandler<void()>&& completionHandler) final;
#endif

    void wheelEventHandlersChanged(bool) final { }

    void setTextIndicator(const WebCore::TextIndicatorData&) const final { }

    WebView* webView() { return m_webView; }

    bool shouldUseTiledBackingForFrameView(const WebCore::LocalFrameView&) const final;

    RefPtr<WebCore::Icon> createIconForFiles(const Vector<WTF::String>&) final;

    void didFinishLoadingImageForElement(WebCore::HTMLImageElement&) final;

    RefPtr<WebCore::DateTimeChooser> createDateTimeChooser(WebCore::DateTimeChooserClient&) final;

    void requestCookieConsent(CompletionHandler<void(WebCore::CookieConsentDecisionResult)>&&) final;

private:
    WebView* m_webView;
    std::unique_ptr<WTF::RunLoop::Timer> m_hiddenUpdateTimer; // 1 s rendering updates of hidden tabs
    RefPtr<WebCore::Element> m_fullScreenElement;
#if ENABLE(NOTIFICATIONS) || ENABLE(LEGACY_NOTIFICATIONS)
    std::unique_ptr<WebDesktopNotificationsDelegate> m_notificationsDelegate;
#endif
};
