/*
 * GL is not available on the MUI port; accelerated compositing is forced off in WebView.
 */

#include "config.h"
#include "GLContext.h"

#include <wtf/NeverDestroyed.h>

namespace WebCore {

// shortcut: no GL context on MUI (accelerated compositing disabled), upgrade with Mesa/GLES2 support
GLContext* GLContext::current()
{
    return nullptr;
}

unsigned GLContext::version()
{
    return 0;
}

const GLContext::GLExtensions& GLContext::glExtensions() const
{
    static NeverDestroyed<GLExtensions> none;
    return none;
}

} // namespace WebCore
