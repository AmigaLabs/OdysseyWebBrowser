#include <stdio.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

Object * STDARGS VARARGS68K DoSuperNew(struct IClass *cl, Object * obj, ...)
{
        Object *rc;
        va_list args;
        struct opSet msg;

        va_start(args, obj);
        msg.MethodID = OM_NEW;
        msg.ops_AttrList = va_getlinearva(args, struct TagItem *);
        msg.ops_GInfo = NULL;
        rc = (Object *)DoSuperMethodA(cl, obj, (Msg)&msg);
        va_end(args);

        return rc;
}

static LONG do_alpha(LONG a, LONG v)
{
  LONG tmp  = (a*v);
  return ((tmp<<8) + tmp + 32768)>>16;
}

uint32_t _WritePixelArrayAlpha(APTR src, uint16_t srcx, uint16_t srcy, uint16_t srcmod, struct RastPort *rp, uint16_t destx, uint16_t desty, uint16_t width, uint16_t height, uint32_t globalalpha)
{
  uint32_t pixels = 0;

  if(width > 0 && height > 0)
  {
    uint32_t *buf;

    if((buf = (uint32_t *)malloc(width * 4)) != NULL)
    {
      uint32_t x, y;

      // Incorrect but cant bother with alpha channel math for now
      globalalpha = 255 - (globalalpha >> 24);

      for(y = 0; y < height; y++)
      {
        uint32_t *spix;
        uint32_t *dpix;

        ReadPixelArray(rp, destx, desty + y, (uint8 *)buf, 0, 0, width * 4, PIXF_A8R8G8B8, width, 1);

        spix = (uint32_t *)((uint32_t)src + (srcy + y) * srcmod + srcx * sizeof(uint32_t));
        dpix = buf;

        // Prefetch the first cache lines of source and destination
        if (width > 8) {
          __builtin_prefetch(spix + 8, 0, 3);  // Read prefetch, high locality
          __builtin_prefetch(dpix + 8, 1, 3);  // Write prefetch, high locality
        }

        for(x = 0; x < width; x++)
        {
          uint32_t srcpix, dstpix, a, r, g, b;

          srcpix = *spix++;
          dstpix = *dpix;

          // Prefetch ahead in the loop (8 pixels ahead)
          if (x + 8 < width) {
            __builtin_prefetch(spix + 8, 0, 3);
            __builtin_prefetch(dpix + 8, 1, 3);
          }

          // Convert from big-endian to host byte order if needed
          // Assuming PIXF_A8R8G8B8 might be in different endianness
          //srcpix = __builtin_bswap32(srcpix);
          //dstpix = __builtin_bswap32(dstpix);

          // Extract components - now in consistent byte order
          a = (srcpix >> 24) & 0xff;
          r = (srcpix >> 16) & 0xff;
          g = (srcpix >> 8) & 0xff;
          b = srcpix & 0xff;

          a = a - globalalpha;

          if(a > 0)
          {
            uint32_t dest_r, dest_g, dest_b;

            // Extract destination components
            dest_r = (dstpix >> 16) & 0xff;
            dest_g = (dstpix >> 8) & 0xff;
            dest_b = dstpix & 0xff;

            // Alpha blending calculations
            dest_r += do_alpha(a, r - dest_r);
            dest_g += do_alpha(a, g - dest_g);
            dest_b += do_alpha(a, b - dest_b);

            // Recompose pixel in the same ARGB layout used by ReadPixelArray/WritePixelArray.
            dstpix = (0xffUL << 24) | (dest_r << 16) | (dest_g << 8) | dest_b;
          }
          else
          {
            // Keep the original pixel unchanged.
          }

          *dpix++ = dstpix;
          pixels++;
        }

        WritePixelArray((uint8 *)buf, 0, 0, width * 4, PIXF_A8R8G8B8, rp, destx, desty + y, width, 1);
      }

      free(buf);
    }
  }

  return pixels;
}

APTR ARGB2BGRA(APTR src, uint32_t stride, uint32_t height)
{
    APTR _return = malloc(stride * height);
    uint32_t * dstptr = (uint32_t *)_return;
    uint32_t * srcptr = (uint32_t *)src;
    uint32_t x, y, pixelsperline = stride / 4, srcval, dstval;

    for (y = 0; y < height; y++)
        for (x = 0; x < pixelsperline; x++)
        {
            srcval = (*srcptr);
            dstval = 0;
            dstval |= ((srcval & 0x000000FF) >> 0)  << 24;
            dstval |= ((srcval & 0x0000FF00) >> 8)  << 16;
            dstval |= ((srcval & 0x00FF0000) >> 16) << 8;
            dstval |= ((srcval & 0xFF000000) >> 24) << 0;

            (*dstptr) = dstval;
            srcptr++;
            dstptr++;
        }


    return _return;
}

void ARGB2BGRAFREE(APTR dst)
{
  if (dst)
    free(dst);
}

#ifdef __cplusplus
}
#endif

