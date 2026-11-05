/*
 * MUI/AmigaOS CurlSSLHandle platform hook.
 */

#include "config.h"
#include "CurlSSLHandle.h"

namespace WebCore {

// shortcut: no extra SSL setup on AmigaOS (system libcurl/OpenSSL defaults), revisit if certificate loading fails
void CurlSSLHandle::platformInitialize()
{
}

} // namespace WebCore
