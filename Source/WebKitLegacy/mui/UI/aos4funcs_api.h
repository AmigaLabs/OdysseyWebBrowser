#ifndef AOS4FUNCS_API_H
#define AOS4FUNCS_API_H

#include <proto/exec.h>
#include <sys/types.h>

#include "classes.h"

typedef long long QUAD;
typedef unsigned long long UQUAD;
typedef unsigned long ULONG;
typedef unsigned long IPTR;
typedef IPTR STACKIPTR;

#define AllocVecTaskPooled(x) AllocVec(x,MEMF_PRIVATE)
#define FreeVecTaskPooled(x) FreeVec(x)

// typedef of _Msg_ on morphos are the same as typedef of _Msg on os4 (intuition/classusr.h on both oses)
#define _Msg_ _Msg
#undef String

#ifdef __cplusplus
extern "C" {
#endif

Object * VARARGS68K DoSuperNew(struct IClass *cl, Object *obj, ...);
// size_t stccpy(char *p, const char *q, size_t n);
ULONG _WritePixelArrayAlpha(APTR src, UWORD srcx, UWORD srcy, UWORD srcmod, struct RastPort *rp, UWORD destx, UWORD desty, UWORD width, UWORD height, ULONG globalalpha);
#define WritePixelArrayAlpha _WritePixelArrayAlpha

#ifdef __cplusplus
}
#endif

#endif
