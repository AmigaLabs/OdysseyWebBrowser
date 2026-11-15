/*
 * Copyright 2010 Fabien Coeurjoly <fabien.coeurjoly@wanadoo.fr>
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

#include <WebCore/GraphicsContext.h>
#include "WebView.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <clib/macros.h>

#include "gui.h"

using namespace WebCore;

struct Data
{
    Object *group;
};

DEFNEW
{
    Object *group, *bt_save, *bt_cancel;

    obj = (Object *) DoSuperNew(cl, obj,
            MUIA_Window_ID, MAKE_ID('W','S','E','A'),
            MUIA_Window_Title, GSI(MSG_SEARCHMANAGERWINDOW_TITLE),
            MUIA_Window_NoMenus, TRUE,
            WindowContents, VGroup,
                Child, group = (Object *) NewObject(getsearchmanagergroupclass(), NULL, TAG_DONE),
                Child, HGroup,
                    Child, bt_save = (Object *) MakeButton(GSI(MSG_SEARCHMANAGERWINDOW_SAVE)),
                    Child, bt_cancel = (Object *) MakeButton(GSI(MSG_SEARCHMANAGERWINDOW_CANCEL)),
                End,
            End,
            TAG_MORE, msg->ops_AttrList);

    if (obj)
    {
        GETDATA;

        data->group = group;

        set(bt_save,   MUIA_ShortHelp, GSI(MSG_SEARCHMANAGERWINDOW_SAVE_HELP));
        set(bt_cancel, MUIA_ShortHelp, GSI(MSG_SEARCHMANAGERWINDOW_CANCEL_HELP));

        // Edits take effect only with "Save"; the close gadget works like "Cancel".
        DoMethod(obj,       MUIM_Notify, MUIA_Window_CloseRequest, TRUE,  obj, 1, MM_SearchManagerWindow_Cancel);
        DoMethod(bt_save,   MUIM_Notify, MUIA_Pressed,             FALSE, obj, 1, MM_SearchManagerWindow_Save);
        DoMethod(bt_cancel, MUIM_Notify, MUIA_Pressed,             FALSE, obj, 1, MM_SearchManagerWindow_Cancel);
    }

    return (IPTR)obj;
}

DEFGET
{
    GETDATA;

    switch (msg->opg_AttrID)
    {
        case MA_OWB_WindowType:
        {
            *msg->opg_Storage = (IPTR) MV_OWB_Window_SearchManager;
        }
        return TRUE;

        case MA_SearchManagerWindow_Group:
        {
            *msg->opg_Storage = (IPTR) data->group;
        }
        return TRUE;
    }

    return DOSUPER;
}

DEFTMETHOD(SearchManagerGroup_Load)
{
    GETDATA;
    return DoMethod(data->group, MM_SearchManagerGroup_Load);
}

DEFTMETHOD(SearchManagerWindow_Save)
{
    GETDATA;

    DoMethod(data->group, MM_SearchManagerGroup_Save);
    set(obj, MUIA_Window_Open, FALSE);

    return 0;
}

DEFTMETHOD(SearchManagerWindow_Cancel)
{
    GETDATA;

    DoMethod(data->group, MM_SearchManagerGroup_Revert);
    set(obj, MUIA_Window_Open, FALSE);

    return 0;
}

BEGINMTABLE
DECNEW
DECGET
DECTMETHOD(SearchManagerGroup_Load)
DECTMETHOD(SearchManagerWindow_Save)
DECTMETHOD(SearchManagerWindow_Cancel)
ENDMTABLE

DECSUBCLASS_NC(MUIC_Window, searchmanagerwindowclass)
