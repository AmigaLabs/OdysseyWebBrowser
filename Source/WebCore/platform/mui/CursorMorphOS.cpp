/*
 * Copyright (C) 2009-2013 Fabien Coeurjoly.
 * Copyright (C) 2008 Pleyo.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Pleyo nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PLEYO AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PLEYO OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "config.h"
#include "Cursor.h"
#include <wtf/Assertions.h>
#include "Logging.h"

#include <cstdio>
#include <string.h>
#include <proto/intuition.h>
#include <intuition/pointerclass.h>

namespace WebCore {

void Cursor::ensurePlatformCursor() const
{
#if OS(AMIGAOS)
    // The browser view sets this as MUIA_PointerType; MUIV_PointerType_* have the same values as POINTERTYPE_*.
    ULONG pointertype;

    switch(m_type)
    {
        default:
        case Type::Pointer:
            pointertype = POINTERTYPE_NORMAL;
            break;
        case Type::Hand:
            pointertype = POINTERTYPE_LINK;
            break;
        case Type::Cross:
            pointertype = POINTERTYPE_CROSS;
            break;
        case Type::Wait:
            pointertype = POINTERTYPE_BUSY;
            break;
        case Type::Help:
            pointertype = POINTERTYPE_HELP;
            break;
        case Type::IBeam:
            pointertype = POINTERTYPE_TEXT;
            break;
        case Type::VerticalText:
            pointertype = POINTERTYPE_VERTICALTEXT;
            break;
        case Type::Move:
        case Type::MiddlePanning:
            pointertype = POINTERTYPE_SCROLLALL;
            break;
        case Type::Grab:
        case Type::Grabbing:
            pointertype = POINTERTYPE_HAND;
            break;
        case Type::EastResize:
        case Type::EastPanning:
            pointertype = POINTERTYPE_EASTRESIZE;
            break;
        case Type::WestResize:
        case Type::WestPanning:
            pointertype = POINTERTYPE_WESTRESIZE;
            break;
        case Type::NorthResize:
        case Type::NorthPanning:
            pointertype = POINTERTYPE_NORTHRESIZE;
            break;
        case Type::SouthResize:
        case Type::SouthPanning:
            pointertype = POINTERTYPE_SOUTHRESIZE;
            break;
        case Type::NorthEastResize:
        case Type::NorthEastPanning:
            pointertype = POINTERTYPE_NORTHEASTRESIZE;
            break;
        case Type::NorthWestResize:
        case Type::NorthWestPanning:
            pointertype = POINTERTYPE_NORTHWESTRESIZE;
            break;
        case Type::SouthEastResize:
        case Type::SouthEastPanning:
            pointertype = POINTERTYPE_SOUTHEASTRESIZE;
            break;
        case Type::SouthWestResize:
        case Type::SouthWestPanning:
            pointertype = POINTERTYPE_SOUTHWESTRESIZE;
            break;
        case Type::EastWestResize:
            pointertype = POINTERTYPE_EASTWESTRESIZE;
            break;
        case Type::NorthSouthResize:
            pointertype = POINTERTYPE_NORTHSOUTHRESIZE;
            break;
        case Type::NorthEastSouthWestResize:
            pointertype = POINTERTYPE_NORTHEASTSOUTHWESTRESIZE;
            break;
        case Type::NorthWestSouthEastResize:
            pointertype = POINTERTYPE_NORTHWESTSOUTHEASTRESIZE;
            break;
        case Type::ColumnResize:
            pointertype = POINTERTYPE_COLUMNRESIZE;
            break;
        case Type::RowResize:
            pointertype = POINTERTYPE_ROWRESIZE;
            break;
        case Type::Cell:
            pointertype = POINTERTYPE_CELL;
            break;
        case Type::ContextMenu:
            pointertype = POINTERTYPE_CONTEXTMENU;
            break;
        case Type::Alias:
            pointertype = POINTERTYPE_ALIAS;
            break;
        case Type::Progress:
            pointertype = POINTERTYPE_PROGRESS;
            break;
        case Type::NoDrop:
            pointertype = POINTERTYPE_NODROP;
            break;
        case Type::NotAllowed:
            pointertype = POINTERTYPE_NOTALLOWED;
            break;
        case Type::Copy:
            pointertype = POINTERTYPE_COPY;
            break;
        case Type::None:
            pointertype = POINTERTYPE_NONE;
            break;
        case Type::ZoomIn:
            pointertype = POINTERTYPE_ZOOMIN;
            break;
        case Type::ZoomOut:
            pointertype = POINTERTYPE_ZOOMOUT;
            break;
    }

    m_platformCursor = (void *) (uintptr_t) pointertype;
#elif !OS(AROS)
    ULONG pointertype;

    switch(m_type)
    {
        default:
        case Pointer:
            pointertype = POINTERTYPE_NORMAL;
            break;
        case Hand:
            pointertype = POINTERTYPE_SELECTLINK;
            break;
        case Cross:
            pointertype = POINTERTYPE_AIMING;
            break;
        case Wait:
            pointertype = POINTERTYPE_BUSY;
            break;
        case Help:
            pointertype = POINTERTYPE_HELP;
            break;
        case IBeam:
        case VerticalText:
            pointertype = POINTERTYPE_SELECTTEXT;
            break;
        case Move:
        case Grab:
        case Grabbing:
            pointertype = POINTERTYPE_MOVE;
            break;
        case EastResize:
        case WestResize:
        case EastWestResize:
        case ColumnResize:
            pointertype = POINTERTYPE_HORIZONTALRESIZE;
            break;
        case NorthResize:
        case SouthResize:
        case NorthSouthResize:
        case RowResize:
            pointertype = POINTERTYPE_VERTICALRESIZE;
            break;
        case NorthEastResize:
        case SouthWestResize:
        case NorthEastSouthWestResize:
            pointertype = POINTERTYPE_DIAGONALRESIZE1;
            break;
        case SouthEastResize:
        case NorthWestResize:
        case NorthWestSouthEastResize:
            pointertype = POINTERTYPE_DIAGONALRESIZE2;
            break;
        case Progress:
            pointertype = POINTERTYPE_WORKING;
            break;
        case NoDrop:
        case NotAllowed:
            pointertype = POINTERTYPE_NOTAVAILABLE;
            break;
    }

    m_platformCursor = (void *) pointertype;
#endif
}


}
