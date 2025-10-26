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

#define AllocVecTaskPooled(x) calloc(1, x)
#define FreeVecTaskPooled(x) free(x)

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
APTR ARGB2BGRA(APTR src, ULONG stride, ULONG height);
VOID ARGB2BGRAFREE(APTR dst);

#ifdef __cplusplus
}
#endif

#undef NEWLIST
#define NEWLIST(_l)                                     \
do                                                      \
{                                                       \
    struct List *__aros_list_tmp = (struct List *)(_l), \
                *l = __aros_list_tmp;                   \
                                                        \
    l->lh_TailPred = (struct ::Node *)l;                \
    l->lh_Tail     = 0;                                 \
    l->lh_Head     = (struct ::Node *)&l->lh_Tail;      \
} while (0)

#undef REMOVE
#define REMOVE(n) Remove((struct ::Node*)n)

#undef REMHEAD
#define REMHEAD(l) ((APTR)RemHead((struct List*)l))

#undef ADDTAIL
#define ADDTAIL(l,n) AddTail((struct List*)l,(struct ::Node*)n)

#undef ADDHEAD
#define ADDHEAD(l,n) AddHead((struct List*)l,(struct ::Node*)n)

#undef REMTAIL
#define REMTAIL(l) ((APTR)RemTail((struct List*)l))


#endif
