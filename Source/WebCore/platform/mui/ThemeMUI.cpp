/*
 * Copyright (C) 2025 Odyssey Web Browser project.
 * Licensed under the LGPL-2.1 or later, as the rest of WebCore.
 */

#include "config.h"
#include "Theme.h"

#include <wtf/NeverDestroyed.h>

namespace WebCore {

namespace {
// shortcut: no MUI-specific theme overrides (base Theme defaults only), add overrides when MUI needs native control metrics
class ThemeMUI final : public Theme { };
}

Theme& Theme::singleton()
{
    static NeverDestroyed<ThemeMUI> theme;
    return theme.get();
}

} // namespace WebCore
