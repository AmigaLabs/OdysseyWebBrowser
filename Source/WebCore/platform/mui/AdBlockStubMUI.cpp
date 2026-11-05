/*
 * MUI ad-block stubs. The 2.34 AdBlock.cpp (rule cache + filter) is not ported.
 */

#include "config.h"
#include <wtf/text/WTFString.h>

namespace WebCore {

// shortcut: ad blocking disabled on MUI (no rule cache), restore AdBlock.cpp from the 2.34 branch to enable
bool ad_block_enabled = false;

void flushCache()
{
}

void loadCache()
{
}

bool writeCache()
{
    return false;
}

void* addCacheEntry(String, int)
{
    return nullptr;
}

void updateCacheEntry(String, int, void*)
{
}

void removeCacheEntry(void*, int)
{
}

} // namespace WebCore
