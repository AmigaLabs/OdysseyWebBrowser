#ifndef CLIB_DEBUG_PROTOS_H
#define CLIB_DEBUG_PROTOS_H

#ifdef __AROS__
#include <aros/debug.h>
#endif

#ifdef __amigaos4__
#include <proto/exec.h>
#define kprintf DebugPrintF
#endif

#undef D

#endif
