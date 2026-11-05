/*
 * Copyright (C) 2025 Odyssey Web Browser project.
 * Licensed under the LGPL-2.1 or later, as the rest of WebCore.
 */

#include "config.h"
#include "AXObjectCache.h"
#include "AccessibilityObject.h"

namespace WebCore {

// shortcut: accessibility not exposed on MUI, all platform hooks are no-ops, add an AT bridge when MUI gets screen reader support
void AXObjectCache::attachWrapper(AccessibilityObject&)
{
}

void AXObjectCache::postPlatformNotification(AccessibilityObject&, AXNotification)
{
}

void AXObjectCache::nodeTextChangePlatformNotification(AccessibilityObject*, AXTextChange, unsigned, const String&)
{
}

void AXObjectCache::frameLoadingEventPlatformNotification(AccessibilityObject*, AXLoadingEvent)
{
}

void AXObjectCache::handleScrolledToAnchor(const Node&)
{
}

void AXObjectCache::platformHandleFocusedUIElementChanged(Element*, Element*)
{
}

void AXObjectCache::platformPerformDeferredCacheUpdate()
{
}

void AccessibilityObject::detachPlatformWrapper(AccessibilityDetachmentType)
{
}

bool AccessibilityObject::accessibilityIgnoreAttachment() const
{
    return false;
}

AccessibilityObjectInclusion AccessibilityObject::accessibilityPlatformIncludesObject() const
{
    return AccessibilityObjectInclusion::DefaultBehavior;
}

} // namespace WebCore
