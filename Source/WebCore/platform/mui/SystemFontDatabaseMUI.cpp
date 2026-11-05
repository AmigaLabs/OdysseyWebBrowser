/*
 * Copyright (C) 2025 Odyssey Web Browser project.
 * Licensed under the LGPL-2.1 or later, as the rest of WebCore.
 */

#include "config.h"
#include "SystemFontDatabase.h"

#include "WebKitFontFamilyNames.h"
#include <wtf/NeverDestroyed.h>

namespace WebCore {

SystemFontDatabase& SystemFontDatabase::singleton()
{
    static NeverDestroyed<SystemFontDatabase> database = SystemFontDatabase();
    return database.get();
}

// shortcut: no system font shorthands on MUI, every shorthand gets the standard family at 16px, add a real lookup when MUI exposes system fonts
auto SystemFontDatabase::platformSystemFontShorthandInfo(FontShorthand) -> SystemFontShorthandInfo
{
    return { WebKitFontFamilyNames::standardFamily, 16, normalWeightValue() };
}

void SystemFontDatabase::platformInvalidate()
{
}

} // namespace WebCore
