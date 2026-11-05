/*
 * MUI memory-pressure hook.
 */

#include "config.h"

namespace WebKit {

// shortcut: no resource-cache flush on MUI yet (MorphOS WebProcess::clearResourceCaches not ported), upgrade when WebProcess is ported
void reactOnMemoryPressureInWebKit()
{
}

} // namespace WebKit
