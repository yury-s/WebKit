/*
 * Copyright (C) 2026 Microsoft Corporation.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.
 */

#include "config.h"
#include "WebKitHeadlessView.h"

#include "APINavigation.h"
#include "APINavigationAction.h"
#include "APINavigationClient.h"
#include "APINavigationResponse.h"
#include "APIPageConfiguration.h"
#include "APIUIClient.h"
#include "APIWebsitePolicies.h"
#include "AcceleratedBackingStore.h"
#include "ContextMenuContextData.h"
#include "DrawingAreaProxyCoordinatedGraphics.h"
#include "FrameInfoData.h"
#include "PageClientImpl.h"
#include "UserData.h"
#include "WebContextMenuItem.h"
#include "WebContextMenuProxy.h"
#include "WebEventModifier.h"
#include "WebKitNetworkSessionPrivate.h"
#include "WebMouseEvent.h"
#include "WebKitWebContextPrivate.h"
#include "WebKitWebsiteDataManagerPrivate.h"
#include "WebPageProxy.h"
#include "WebPreferences.h"
#include "WebProcessPool.h"
#include "WebsiteDataStore.h"
#include <WebCore/ActivityState.h>
#include <WebCore/Color.h>
#include <WebCore/FloatRect.h>
#include <WebCore/IntSize.h>
#include <WebCore/PermissionState.h>
#include <WebCore/Region.h>
#include <WebCore/ResourceRequest.h>
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RunLoop.h>

namespace WebKit {
using namespace WebCore;

// Default size for a freshly created headless page; Playwright overrides this via the
// protocol (Page.setSize) once it attaches.
static constexpr int defaultViewWidth = 1280;
static constexpr int defaultViewHeight = 720;

static void scheduleHeadlessViewRemoval(WebPageProxy*);

#if ENABLE(CONTEXT_MENUS)
// The GTK context menu proxy parents a popover on the view widget; with no widget that
// crashes. Automation only needs the DOM "contextmenu" event (dispatched by the web
// process), so the native menu is a no-op, matching the WPE port's widget-less proxy.
class HeadlessContextMenuProxy final : public WebContextMenuProxy {
public:
    static Ref<HeadlessContextMenuProxy> create(WebPageProxy& page, FrameInfoData&& frameInfo, ContextMenuContextData&& context, const UserData& userData)
    {
        return adoptRef(*new HeadlessContextMenuProxy(page, WTF::move(frameInfo), WTF::move(context), userData));
    }

private:
    HeadlessContextMenuProxy(WebPageProxy& page, FrameInfoData&& frameInfo, ContextMenuContextData&& context, const UserData& userData)
        : WebContextMenuProxy(page, WTF::move(frameInfo), WTF::move(context), userData)
    {
    }

    void showContextMenuWithItems(Vector<Ref<WebContextMenuItem>>&&) override { }
};
#endif // ENABLE(CONTEXT_MENUS)

// A PageClient for a page that has no GtkWidget backing it. It mirrors PageClientImpl but
// reports the view as always visible/focused/in-window so the web process keeps laying
// out and processing input, and it stubs every widget-touching notification.
class HeadlessViewClient final : public PageClientImpl {
public:
    explicit HeadlessViewClient(const IntSize& size)
        : PageClientImpl(nullptr)
        , m_size(size)
    {
    }

    void setPage(WebPageProxy* page) { m_page = page; }
    void setBackingStore(AcceleratedBackingStore* backingStore) { m_backingStore = backingStore; }

private:
    Ref<DrawingAreaProxy> createDrawingAreaProxy(WebProcessProxy& process) override
    {
        return DrawingAreaProxyCoordinatedGraphics::create(*m_page, process);
    }
    void setViewNeedsDisplay(const Region&) override { }
    void requestScroll(const FloatPoint&, const IntPoint&, ScrollIsAnimated, InterruptScrollAnimation) override { }
    void requestScrollToRect(const FloatRect&, const FloatPoint&) override { }
    FloatPoint viewScrollPosition() override { return { }; }
    IntSize viewSize() override { return m_size; }
    bool isViewWindowActive() override { return true; }
    bool isViewFocused() override { return true; }
    bool isActiveViewVisible() override { return true; }
    bool isViewInWindow() override { return true; }

    void enterAcceleratedCompositingMode(const LayerTreeContext& context) override
    {
        if (m_backingStore)
            m_backingStore->update(context);
    }
    void updateAcceleratedCompositingMode(const LayerTreeContext& context) override
    {
        if (m_backingStore)
            m_backingStore->update(context);
    }
    void exitAcceleratedCompositingMode() override { }

    // Process lifecycle and widget-touching notifications: PageClientImpl forwards these
    // to the (null) widget, so override to no-ops.
    void processWillSwap() override { }
    void processDidExit() override { }
    void didRelaunchProcess() override { }
    void pageClosed() override
    {
        // The registry owns this client; drop the entry once we're out of this callback.
        if (m_page) {
            scheduleHeadlessViewRemoval(m_page);
            m_page = nullptr;
        }
    }
    void preferencesDidChange() override { }
    void refView() override { }
    void derefView() override { }
    void toolTipChanged(const String&, const String&) override { }
    void setCursor(const Cursor&) override { }
    void setCursorHiddenUntilMouseMoves(bool) override { }
    void didChangeContentSize(const IntSize&) override { }
    void selectionDidChange() override { }
    void didChangeBackgroundColor() override { }
    void themeColorDidChange() override { }
    void didRestoreScrollPosition() override { }
    void didChangeWebPageID() const override { }
    void makeViewBlank(bool) override { }
    void doneWithKeyEvent(const NativeWebKeyboardEvent&, bool) override { }
    void wheelEventWasNotHandledByWebCore(const NativeWebWheelEvent&) override { }
#if ENABLE(TOUCH_EVENTS)
    void doneWithTouchEvent(const WebTouchEvent&, bool) override { }
#endif
#if ENABLE(DRAG_SUPPORT)
    void startDrag(SelectionData&&, OptionSet<DragOperation>, RefPtr<ShareableBitmap>&&, IntPoint&&) override { }
    void didPerformDragControllerAction() override { }
#endif

    FloatRect convertToDeviceSpace(const FloatRect& rect) override { return rect; }
    FloatRect convertToUserSpace(const FloatRect& rect) override { return rect; }
    IntPoint screenToRootView(const IntPoint& point) override { return point; }
    IntRect rootViewToScreen(const IntRect& rect) override { return rect; }
    IntPoint rootViewToScreen(const IntPoint& point) override { return point; }
    IntPoint accessibilityScreenToRootView(const IntPoint& point) override { return point; }
    IntRect rootViewToAccessibilityScreen(const IntRect& rect) override { return rect; }

#if ENABLE(CONTEXT_MENUS)
    Ref<WebContextMenuProxy> createContextMenuProxy(WebPageProxy& page, FrameInfoData&& frameInfo, ContextMenuContextData&& context, const UserData& userData) override
    {
        return HeadlessContextMenuProxy::create(page, WTF::move(frameInfo), WTF::move(context), userData);
    }
#endif
    RefPtr<WebPopupMenuProxy> createPopupMenuProxy(WebPageProxy&) override { return nullptr; }
    RefPtr<WebColorPicker> createColorPicker(WebPageProxy&, const Color&, const IntRect&, ColorControlSupportsAlpha, Vector<Color>&&, std::optional<FrameIdentifier>) override { return nullptr; }
    RefPtr<WebDateTimePicker> createDateTimePicker(WebPageProxy&) override { return nullptr; }
    RefPtr<WebDataListSuggestionsDropdown> createDataListSuggestionsDropdown(WebPageProxy&) override { return nullptr; }

    UserInterfaceLayoutDirection userInterfaceLayoutDirection() override { return UserInterfaceLayoutDirection::LTR; }
    bool effectiveAppearanceIsDark() const override { return false; }
    Color accentColor() override { return SRGBA<uint8_t> { 53, 132, 228 }; }
    WebKitWebResourceLoadManager* webResourceLoadManager() override { return nullptr; }

    void didStartProvisionalLoadForMainFrame() override { }
    void didFirstVisuallyNonEmptyLayoutForMainFrame() override { }
    void didCommitLoadForMainFrame(const String&, bool) override { }
    void didFinishNavigation(API::Navigation*) override { }
    void didFailNavigation(API::Navigation*) override { }
    void didSameDocumentNavigationForMainFrame(SameDocumentNavigationType) override { }
    void didFinishLoadingDataForCustomContentProvider(const String&, std::span<const uint8_t>) override { }

    void navigationGestureDidBegin() override { }
    void navigationGestureWillEnd(bool, WebBackForwardListItem&) override { }
    void navigationGestureDidEnd(bool, WebBackForwardListItem&) override { }
    void navigationGestureDidEnd() override { }
    void willRecordNavigationSnapshot(WebBackForwardListItem&) override { }
    void didRemoveNavigationGestureSnapshot() override { }

    IntSize m_size;
    WebPageProxy* m_page { nullptr };
    AcceleratedBackingStore* m_backingStore { nullptr };
};

// Builds a widget-less page from an existing configuration (used for the agent-created page,
// window.open popups, and modifier/middle-click new windows). A non-null initialURL is loaded
// to launch the web process and register the inspector target; a null URL loads nothing (the
// page's web process drives its own initial load, e.g. popups).
static RefPtr<WebPageProxy> createHeadlessPage(Ref<API::PageConfiguration>&&, const String& initialURL);

// A modifier/middle-click on a link opens it in a new window. Windowed GTK does this in the
// MiniBrowser's decide-policy handler (BrowserWindow.c); a widget-less page has no such
// handler, so replicate the decision here: Ctrl/Shift + primary click, or a middle click,
// on a clicked link.
static bool shouldOpenInNewWindow(const API::NavigationAction& action)
{
    if (action.navigationType() != WebCore::NavigationType::LinkClicked)
        return false;
    if (action.mouseButton() == WebMouseEventButton::Middle)
        return true;
    return action.mouseButton() == WebMouseEventButton::Left
        && action.modifiers().containsAny({ WebEventModifier::ControlKey, WebEventModifier::ShiftKey });
}

// The default navigation client always uses (displays) responses, so downloads never
// start. Decide download for non-showable responses and download-attribute actions; the
// download itself is reported to automation by the data store's download instrumentation.
class HeadlessNavigationClient final : public API::NavigationClient {
public:
    void decidePolicyForNavigationAction(WebPageProxy& page, Ref<API::NavigationAction>&& action, Ref<WebFramePolicyListenerProxy>&& listener) override
    {
        if (action->shouldPerformDownload()) {
            listener->download();
            return;
        }
        if (shouldOpenInNewWindow(action)) {
            // Open the link in a fresh page in the same context (no opener), then cancel the
            // in-place navigation -- matching windowed GTK's modifier/middle-click behavior.
            if (RefPtr newPage = createHeadlessPage(page.configuration().copy(), String()))
                newPage->loadRequest(WebCore::ResourceRequest { action->request() });
            listener->ignore();
            return;
        }
        listener->use();
    }

    void decidePolicyForNavigationResponse(WebPageProxy&, Ref<API::NavigationResponse>&& response, Ref<WebFramePolicyListenerProxy>&& listener) override
    {
        if (response->canShowMIMEType())
            listener->use();
        else
            listener->download();
    }
};

// Without a window, the page has no UI delegate to create or close pages. The Playwright
// agent closes pages via WebPageProxy::closePage() -> UIClient::close(), and window.open
// goes through UIClient::createNewPage(); both must work for a widget-less page.
class HeadlessUIClient final : public API::UIClient {
public:
    void createNewPage(WebPageProxy&, Ref<API::PageConfiguration>&& configuration, Ref<API::NavigationAction>&&, CompletionHandler<void(RefPtr<WebPageProxy>&&)>&& completionHandler) final
    {
        // The popup's web process drives its own initial load, so don't force about:blank.
        completionHandler(createHeadlessPage(WTF::move(configuration), String()));
    }

    void close(WebPageProxy* page) final
    {
        if (page)
            page->close();
    }

    // Script dialogs: the base UIClient immediately dismisses them. Store the completion
    // handler and run it when the automation client accepts/dismisses via
    // InspectorDialogAgent::handleJavaScriptDialog -> UIClient::handleJavaScriptDialog.
    void runJavaScriptAlert(WebPageProxy&, const String&, WebFrameProxy*, FrameInfoData&&, Function<void()>&& completionHandler) final
    {
        m_alertHandler = WTF::move(completionHandler);
    }

    void runJavaScriptConfirm(WebPageProxy&, const String&, WebFrameProxy*, FrameInfoData&&, Function<void(bool)>&& completionHandler) final
    {
        m_confirmHandler = WTF::move(completionHandler);
    }

    void runJavaScriptPrompt(WebPageProxy&, const String&, const String&, WebFrameProxy*, FrameInfoData&&, Function<void(const String&)>&& completionHandler) final
    {
        m_promptHandler = WTF::move(completionHandler);
    }

    bool canRunBeforeUnloadConfirmPanel() const final { return true; }

    void runBeforeUnloadConfirmPanel(WebPageProxy&, String&&, WebFrameProxy*, FrameInfoData&&, Function<void(bool)>&& completionHandler) final
    {
        m_confirmHandler = WTF::move(completionHandler);
    }

    // Permissions not explicitly granted/denied via automation default to "prompt" (the
    // base UIClient returns null, which surfaces as a NotSupportedError to navigator.permissions).
    void queryPermission(const String&, API::SecurityOrigin&, CompletionHandler<void(std::optional<WebCore::PermissionState>)>&& completionHandler) final
    {
        completionHandler(WebCore::PermissionState::Prompt);
    }

    // No real window; report the viewport as the window frame so window.outerWidth/Height
    // match innerWidth/Height instead of being zero.
    void windowFrame(WebPageProxy& page, Function<void(WebCore::FloatRect)>&& completionHandler) final
    {
        auto size = page.viewSize();
        completionHandler(WebCore::FloatRect(0, 0, size.width(), size.height()));
    }

    void handleJavaScriptDialog(WebPageProxy&, bool accept, const String& value) final
    {
        if (auto handler = std::exchange(m_alertHandler, nullptr))
            handler();
        else if (auto handler = std::exchange(m_confirmHandler, nullptr))
            handler(accept);
        else if (auto handler = std::exchange(m_promptHandler, nullptr))
            handler(accept ? value : String());
    }

private:
    Function<void()> m_alertHandler;
    Function<void(bool)> m_confirmHandler;
    Function<void(const String&)> m_promptHandler;
};

// Owns everything that keeps a headless page running: its PageClient, the page, and the
// backing store that drains/acknowledges compositor frames. The client is a const
// std::unique_ptr because makeUniqueWithoutRefCountedCheck() returns one; initialize it
// with lazyInitialize().
struct HeadlessView {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(HeadlessView);
    const std::unique_ptr<HeadlessViewClient> client;
    RefPtr<WebPageProxy> page;
    RefPtr<AcceleratedBackingStore> backingStore;
};

static HashMap<WebPageProxy*, std::unique_ptr<HeadlessView>>& headlessViews()
{
    static NeverDestroyed<HashMap<WebPageProxy*, std::unique_ptr<HeadlessView>>> views;
    return views;
}

static void scheduleHeadlessViewRemoval(WebPageProxy* page)
{
    RunLoop::mainSingleton().dispatch([page] {
        headlessViews().remove(page);
    });
}

static bool s_headlessEnabled { false };

bool webkitHeadlessIsEnabled()
{
    return s_headlessEnabled;
}

void webkitHeadlessSetEnabled(bool enabled)
{
    s_headlessEnabled = enabled;
}

AcceleratedBackingStore* webkitHeadlessBackingStore(WebPageProxy* page)
{
    if (auto* view = headlessViews().get(page))
        return view->backingStore.get();
    return nullptr;
}

static RefPtr<WebPageProxy> createHeadlessPage(Ref<API::PageConfiguration>&& configuration, const String& initialURL)
{
    IntSize size { defaultViewWidth, defaultViewHeight };
    configuration->setControlledByAutomation(true);

    // Without a display, GTK's threaded scrolling tree never gets its commit from the main
    // thread, so wheel scrolling is routed to the scrolling thread and silently dropped
    // (no DOM wheel event, no scroll position update). Route wheel/scroll through the main
    // thread instead, matching how the WPE headless port behaves.
    configuration->preferences().setThreadedScrollingEnabled(false);

    // A windowed WebKitWebView gets a default WebKitWebsitePolicies whose autoplay policy is
    // AllowWithoutSound; a widget-less page would otherwise default to Default, which lets
    // AudioContext start without a user gesture. Match the windowed behavior.
    configuration->defaultWebsitePolicies().setAutoplayPolicy(WebsiteAutoplayPolicy::AllowWithoutSound);

    Ref processPool = configuration->processPool();

    auto view = makeUnique<HeadlessView>();
    lazyInitialize(view->client, makeUniqueWithoutRefCountedCheck<HeadlessViewClient>(size));

    Ref page = processPool->createWebPage(*view->client, WTF::move(configuration));
    view->client->setPage(page.ptr());
    page->setUIClient(makeUnique<HeadlessUIClient>());
    page->setNavigationClient(makeUniqueRef<HeadlessNavigationClient>());

    view->backingStore = AcceleratedBackingStore::create(page);
    view->client->setBackingStore(view->backingStore.get());

    auto& pageConfiguration = page->configuration();
    page->initializeWebPage(pageConfiguration.openedSite(), pageConfiguration.initialSandboxFlags(), pageConfiguration.initialReferrerPolicy());

    // Headless views are always "visible" so the web process lays out and processes input.
    page->activityStateDidChange({ ActivityState::IsVisible, ActivityState::IsInWindow, ActivityState::WindowIsActive, ActivityState::IsFocused });
    if (auto* drawingArea = page->drawingArea())
        drawingArea->setSize(size);

    view->page = page.copyRef();
    headlessViews().add(page.ptr(), WTF::move(view));

    // For the agent-created top-level page, kick off an initial load to launch the web process
    // and register the page's inspector target; the windowed path does the same with
    // webkit_web_view_load_uri(view, "about:blank"). Popups load via their opener instead.
    if (!initialURL.isNull())
        page->loadRequest(URL { { }, initialURL });
    return page.copyRef();
}

RefPtr<WebPageProxy> webkitHeadlessCreatePage(WebProcessPool& processPool, WebsiteDataStore& dataStore, const String& initialURL)
{
    Ref preferences = WebPreferences::create(String(), "WebKit2."_s, "WebKit2."_s);
    preferences->setDeveloperExtrasEnabled(true);

    Ref configuration = API::PageConfiguration::create();
    configuration->setProcessPool(&processPool);
    configuration->setWebsiteDataStore(&dataStore);
    configuration->setPreferences(preferences.ptr());

    return createHeadlessPage(WTF::move(configuration), initialURL);
}

} // namespace WebKit

// C entry point so the (C) MiniBrowser can switch the library into headless mode before
// it initializes the browser inspector.
extern "C" __attribute__((visibility("default"))) void webkit_headless_set_enabled(int enabled);
extern "C" void webkit_headless_set_enabled(int enabled)
{
    WebKit::webkitHeadlessSetEnabled(enabled);
}

// C entry point to create the persistent (default) context's initial page in headless mode.
// launchPersistentContext waits for this page before resolving.
extern "C" __attribute__((visibility("default"))) void webkit_headless_create_default_page(WebKitWebContext* context, WebKitNetworkSession* session, const char* startupURL);
extern "C" void webkit_headless_create_default_page(WebKitWebContext* context, WebKitNetworkSession* session, const char* startupURL)
{
    auto* dataManager = webkit_network_session_get_website_data_manager(session);
    // launchPersistentContext may pass a startup URL on the command line (e.g. with
    // ignoreDefaultArgs); load it instead of about:blank so the default page lands there.
    String initialURL = startupURL && *startupURL ? String::fromUTF8(startupURL) : "about:blank"_s;
    WebKit::webkitHeadlessCreatePage(webkitWebContextGetProcessPool(context), webkitWebsiteDataManagerGetDataStore(dataManager), initialURL);
}
