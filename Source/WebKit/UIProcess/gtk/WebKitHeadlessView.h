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

#pragma once

#include <wtf/RefPtr.h>
#include <wtf/text/WTFString.h>

namespace WebKit {

class AcceleratedBackingStore;
class WebPageProxy;
class WebProcessPool;
class WebsiteDataStore;

// Headless GTK mode: drive WebKit with no display server (no X11/Wayland). Pages are
// widget-less WebPageProxy objects rather than WebKitWebView GtkWidgets, so the whole
// browser process can run without ever connecting to a display.
bool webkitHeadlessIsEnabled();
void webkitHeadlessSetEnabled(bool);

// Create a widget-less, automation-controlled page in the given context. The returned
// page is owned by an internal registry that keeps its PageClient alive until the page
// closes; the caller (the Playwright inspector agent) just tracks the WebPageProxy.
// initialURL is loaded to launch the web process and register the inspector target.
RefPtr<WebPageProxy> webkitHeadlessCreatePage(WebProcessPool&, WebsiteDataStore&, const String& initialURL = "about:blank"_s);

// The backing store of a headless page (it has no GtkWidget to fetch it from), or null if
// the page is not a headless page. Used by the screencast frame-capture path.
AcceleratedBackingStore* webkitHeadlessBackingStore(WebPageProxy*);

} // namespace WebKit
