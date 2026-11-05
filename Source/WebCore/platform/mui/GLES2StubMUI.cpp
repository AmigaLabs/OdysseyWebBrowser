/*
 * GLES2 interface pointer for MUI. libGLESv2.a is NOT linked: its startup code asserts
 * when GLES2.library is missing. Accelerated compositing is off, so nothing dereferences this.
 */

#include "config.h"
#include <interfaces/ogles2.h>

// shortcut: GL is not available on MUI (accelerated compositing off), revert to libGLESv2.a when GLES2.library is installed
struct OGLES2IFace* IOGLES2 = nullptr;
