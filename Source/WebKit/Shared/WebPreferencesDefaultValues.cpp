/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebPreferencesDefaultValues.h"

#include <WebCore/RuntimeApplicationChecks.h>
#include <wtf/text/WTFString.h>

#if PLATFORM(COCOA)
#include <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#endif

#if ENABLE(MEDIA_SESSION_COORDINATOR)
#import "WebProcess.h"
#import <wtf/cocoa/Entitlements.h>
#endif

namespace WebKit {

#if PLATFORM(IOS_FAMILY)

bool defaultPassiveTouchListenersAsDefaultOnDocument()
{
    static bool result = linkedOnOrAfter(SDKVersion::FirstThatDefaultsToPassiveTouchListenersOnDocument);
    return result;
}

bool defaultCSSOMViewScrollingAPIEnabled()
{
    static bool result = WebCore::IOSApplication::isIMDb() && !linkedOnOrAfter(SDKVersion::FirstWithoutIMDbCSSOMViewScrollingQuirk);
    return !result;
}

#if !USE(APPLE_INTERNAL_SDK)
bool defaultAlternateFormControlDesignEnabled()
{
    return false;
}
#endif

#endif

#if PLATFORM(MAC)

bool defaultPassiveWheelListenersAsDefaultOnDocument()
{
    static bool result = linkedOnOrAfter(SDKVersion::FirstThatDefaultsToPassiveWheelListenersOnDocument);
    return result;
}

bool defaultWheelEventGesturesBecomeNonBlocking()
{
    static bool result = linkedOnOrAfter(SDKVersion::FirstThatAllowsWheelEventGesturesToBecomeNonBlocking);
    return result;
}

#endif

#if PLATFORM(MAC) || PLATFORM(IOS_FAMILY)

bool defaultDisallowSyncXHRDuringPageDismissalEnabled()
{
#if PLATFORM(MAC) || PLATFORM(MACCATALYST)
    if (CFPreferencesGetAppBooleanValue(CFSTR("allowDeprecatedSynchronousXMLHttpRequestDuringUnload"), CFSTR("com.apple.WebKit"), nullptr)) {
        WTFLogAlways("Allowing synchronous XHR during page unload due to managed preference");
        return false;
    }
#elif PLATFORM(IOS_FAMILY) && !PLATFORM(MACCATALYST) && !PLATFORM(WATCHOS)
    if (allowsDeprecatedSynchronousXMLHttpRequestDuringUnload()) {
        WTFLogAlways("Allowing synchronous XHR during page unload due to managed preference");
        return false;
    }
#endif
    return true;
}

#endif

#if PLATFORM(MAC)

bool defaultAppleMailPaginationQuirkEnabled()
{
    return WebCore::MacApplication::isAppleMail();
}

#endif

bool defaultOfflineWebApplicationCacheEnabled()
{
#if PLATFORM(COCOA)
    static bool newSDK = linkedOnOrAfter(SDKVersion::FirstWithApplicationCacheDisabledByDefault);
    return !newSDK;
#else
    // FIXME: Other platforms should consider turning this off.
    // ApplicationCache is on its way to being removed from WebKit.
    return true;
#endif
}

#if ENABLE(MEDIA_STREAM)

bool defaultCaptureAudioInGPUProcessEnabled()
{
#if PLATFORM(MAC)
    // FIXME: Enable GPU process audio capture when <rdar://problem/29448368> is fixed.
    if (!WebCore::MacApplication::isSafari())
        return false;
#endif

#if ENABLE(GPU_PROCESS_BY_DEFAULT)
    return true;
#else
    return false;
#endif
}

bool defaultCaptureAudioInUIProcessEnabled()
{
#if PLATFORM(MAC)
    return !defaultCaptureAudioInGPUProcessEnabled();
#endif

    return false;
}

#endif // ENABLE(MEDIA_STREAM)

#if ENABLE(MEDIA_SESSION_COORDINATOR)
bool defaultMediaSessionCoordinatorEnabled()
{
    static dispatch_once_t onceToken;
    static bool enabled { false };
    dispatch_once(&onceToken, ^{
        if (WebCore::isInWebProcess())
            enabled = WebProcess::singleton().parentProcessHasEntitlement("com.apple.developer.group-session.urlactivity");
        else
            enabled = WTF::processHasEntitlement("com.apple.developer.group-session.urlactivity");
    });
    return enabled;
}
#endif

#if HAVE(SCREEN_CAPTURE_KIT)
bool defaultScreenCaptureKitEnabled()
{
#if ENABLE(SCREEN_CAPTURE_KIT)
    return true;
#else
    return false;
#endif
}
#endif // HAVE(SCREEN_CAPTURE_KIT)


} // namespace WebKit
