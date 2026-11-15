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

#include <wtf/text/WTFString.h>
#include <wtf/text/CString.h>
#include "FileIOLinux.h"

#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <clib/macros.h>

#include "gui.h"
#include "utils.h"

#define D(x)

using namespace WebCore;

STATIC CONST CONST_STRPTR labels[] =
{
    "DuckDuckGo Lite",
    "Google",
    "OS4Depot",
    "Aminet",
    "MorphOS",
    "YouTube",
    NULL
};

STATIC CONST CONST_STRPTR shortcuts[] =
{
    "d",
    "g",
    "p",
    "a",
    "m",
    "y",
    NULL
};

STATIC CONST CONST_STRPTR requests[] =
{
    "https://lite.duckduckgo.com/lite/?q=%s",
    "https://www.google.com/search?q=%s&ie=UTF-8&oe=UTF-8",
    "https://os4depot.net/index.php?function=search&tool=simple&f_fields=%s",
    "https://www.aminet.net/search.php?query=%s",
    "https://www.morphos-storage.net/?find=%s",
    "https://www.youtube.com/results?search_query=%s",
    NULL
};

struct Data
{
    ULONG changed;
    // The saved search engines, as last loaded or saved. Read by the search bar, so it must not follow unsaved edits in the list.
    Vector<String> labels;
    Vector<String> requests;
    Vector<String> shortcuts;

    Object *bt_remove;
    Object *bt_up;
    Object *bt_down;
    ULONG up_is_toolbutton;
    ULONG down_is_toolbutton;
    Object *lv_entries;
    Object *st_label;
    Object *st_request;
    Object *st_shortcut;
};

static void load_searchengines(Object *obj, struct Data *data)
{
    OWBFile *searchFile = new OWBFile("PROGDIR:Conf/searchengines.prefs");

    if (!searchFile)
        return;

    if (searchFile->open('r') == -1)
    {
        delete searchFile;
        return;
    }

    char *buffer = searchFile->read(searchFile->getSize());
    String fileBuffer = buffer;
    delete [] buffer;
    searchFile->close();
    delete searchFile;

    Vector<String> lines = fileBuffer.split('\n');

    set(data->lv_entries, MUIA_List_Quiet, TRUE);

    for(size_t i = 0; i < lines.size(); i++)
    {
        Vector<String> searchAttributes = lines[i].split('\1');

        if(searchAttributes.size() >= 2) // 1.10 introduced a 3rd field
        {
            struct search_entry *se = (struct search_entry *) malloc(sizeof(*se));

            if (se)
            {
                se->label   = strdup(searchAttributes[0].latin1().data());
                se->request = strdup(searchAttributes[1].latin1().data());

                if(searchAttributes.size() == 3)
                {
                    se->shortcut = strdup(searchAttributes[2].latin1().data());
                }
                else
                {
                    se->shortcut = strdup("");
                }

                DoMethod(data->lv_entries, MUIM_List_InsertSingle, se, MUIV_List_Insert_Bottom);
            }
        }
    }

    set(data->lv_entries, MUIA_List_Quiet, FALSE);
}

void save_searchengines(Object *obj, struct Data *data)
{
    struct search_entry *se;
    ULONG i = 0;
    OWBFile *searchFile = new OWBFile("PROGDIR:Conf/searchengines.prefs");
    if(!searchFile)
        return;

    if (searchFile->open('w') == -1)
    {
        delete searchFile;
        return;
    }

    do
    {
        DoMethod(data->lv_entries, MUIM_List_GetEntry, i, (struct search_entry *) &se);

        if (se)
        {
            searchFile->write(createWithFormatAndArguments("%s\1%s\1%s\n", se->label, se->request, se->shortcut));
        }

        i++;
    }
    while (se);

    searchFile->close();
    delete searchFile;
}

// The list holds the entries being edited. The saved state is kept in data->labels/requests/shortcuts (read by the search bar)
// and in the prefs file; both change only on "Save".
static void snapshot_from_list(struct Data *data)
{
    struct search_entry *se;
    ULONG i = 0;

    data->labels.clear();
    data->requests.clear();
    data->shortcuts.clear();

    for(;;)
    {
        DoMethod(data->lv_entries, MUIM_List_GetEntry, i, (struct search_entry *) &se);

        if(!se)
            break;

        data->labels.append(String(se->label));
        data->requests.append(String(se->request));
        data->shortcuts.append(String(se->shortcut));
        i++;
    }
}

static void append_entry(struct Data *data, const char *label, const char *request, const char *shortcut)
{
    struct search_entry *se = (struct search_entry *) malloc(sizeof(*se));

    if(se)
    {
        se->label    = strdup(label);
        se->request  = strdup(request);
        se->shortcut = strdup(shortcut);

        DoMethod(data->lv_entries, MUIM_List_InsertSingle, se, MUIV_List_Insert_Bottom);
    }
}

static void insert_defaults(struct Data *data)
{
    for(ULONG i = 0; labels[i]; i++)
    {
        append_entry(data, labels[i], requests[i], shortcuts[i]);
    }
}

static void fill_list_from_snapshot(struct Data *data)
{
    set(data->lv_entries, MUIA_List_Quiet, TRUE);
    DoMethod(data->lv_entries, MUIM_List_Clear);

    for(size_t i = 0; i < data->labels.size(); i++)
    {
        append_entry(data, data->labels[i].latin1().data(), data->requests[i].latin1().data(), data->shortcuts[i].latin1().data());
    }

    set(data->lv_entries, MUIA_List_Quiet, FALSE);
}

// The icon file may be missing from the installation, so check without a requester.
static bool image_exists(CONST_STRPTR path)
{
    APTR oldwin = SetProcWindow((APTR) -1);
    BPTR lock = Lock(path, SHARED_LOCK);
    SetProcWindow(oldwin);

    if(lock)
    {
        UnLock(lock);
        return true;
    }

    return false;
}

// Icon button when the image exists, otherwise a text button. The help text doubles as the tool button's text.
static Object *make_move_button(CONST_STRPTR text, CONST_STRPTR help, CONST_STRPTR image, ULONG *is_toolbutton)
{
    Object *button;

    *is_toolbutton = image_exists(image);

    if(*is_toolbutton)
    {
        button = create_toolbutton(help, image, MV_ToolButton_Type_Icon);
    }
    else
    {
        button = (Object *) MakeButton(text);
        set(button, MUIA_ShortHelp, help);
    }

    return button;
}

static void set_button_enabled(Object *button, ULONG is_toolbutton, ULONG enabled)
{
    if(is_toolbutton)
        set(button, MA_ToolButton_Disabled, !enabled);
    else
        set(button, MUIA_Disabled, !enabled);
}

static void move_active_entry(Object *obj, struct Data *data, LONG delta)
{
    LONG active = (LONG) getv(data->lv_entries, MUIA_List_Active);
    LONG count  = (LONG) getv(data->lv_entries, MUIA_List_Entries);
    LONG target = active + delta;

    if(active < 0 || target < 0 || target >= count)
        return;

    DoMethod(data->lv_entries, MUIM_List_Exchange, active, target);
    set(data->lv_entries, MUIA_List_Active, target);

    // The active entry may already have followed the exchange, in which case no notification fires.
    DoMethod(obj, MM_SearchManagerGroup_Update);
}

static void    doset(APTR obj, struct Data *data, struct TagItem *tags)
{
    struct TagItem *tag, *tstate;

    tstate = tags;

    while ((tag = NextTagItem(&tstate)) != NULL)
    {
        switch (tag->ti_Tag)
        {
            case MA_SearchManagerGroup_Changed:
            {
                data->changed = (ULONG) tag->ti_Data;
                break;
            }
        }
    }
}

DEFNEW
{
    Object *lv_entries, *st_label, *st_request, *st_shortcut, *bt_add, *bt_remove, *bt_up, *bt_down;
    ULONG up_is_toolbutton, down_is_toolbutton;

    bt_up   = make_move_button(GSI(MSG_SEARCHMANAGERGROUP_UP),   GSI(MSG_SEARCHMANAGERGROUP_UP_HELP),   "PROGDIR:resource/page_previous.png", &up_is_toolbutton);
    bt_down = make_move_button(GSI(MSG_SEARCHMANAGERGROUP_DOWN), GSI(MSG_SEARCHMANAGERGROUP_DOWN_HELP), "PROGDIR:resource/page_next.png",     &down_is_toolbutton);

    obj = (Object *) DoSuperNew(cl, obj,
        Child, HGroup,
            Child, lv_entries = (Object *) NewObject(getsearchmanagerlistclass(), NULL, TAG_DONE),
            Child, VGroup,
                MUIA_Weight, 0,
                Child, bt_up,
                Child, bt_down,
                Child, HVSpace,
            End,
        End,

        Child, HGroup,
            Child, bt_add = (Object *) MakeButton(GSI(MSG_SEARCHMANAGERGROUP_ADD)),
            Child, bt_remove = (Object *) MakeButton(GSI(MSG_SEARCHMANAGERGROUP_REMOVE)),
        End,

        Child, ColGroup(2), 
            Child, MakeLabel(GSI(MSG_SEARCHMANAGERGROUP_TITLE)),
            Child, st_label = (Object *) MakeString("", FALSE),
            Child, MakeLabel(GSI(MSG_SEARCHMANAGERGROUP_LINK)),
            Child, st_request = (Object *) MakeString("", FALSE),
            Child, MakeLabel(GSI(MSG_SEARCHMANAGERGROUP_SHORTCUT)),
            Child, st_shortcut = (Object *) MakeString("", FALSE),
        End,

        TAG_MORE, INITTAGS
        );

    if (obj)
    {
        GETDATA;

        data->lv_entries  = lv_entries;
        data->st_request  = st_request;
        data->st_label    = st_label;
        data->st_shortcut = st_shortcut;
        data->bt_remove   = bt_remove;
        data->bt_up       = bt_up;
        data->bt_down     = bt_down;
        data->up_is_toolbutton   = up_is_toolbutton;
        data->down_is_toolbutton = down_is_toolbutton;

        set(st_label,    MUIA_Disabled, TRUE);
        set(st_request,  MUIA_Disabled, TRUE);
        set(st_shortcut, MUIA_Disabled, TRUE);
        set(bt_remove,   MUIA_Disabled, TRUE);
        set_button_enabled(bt_up,   up_is_toolbutton,   FALSE);
        set_button_enabled(bt_down, down_is_toolbutton, FALSE);

        set(bt_add,    MUIA_ShortHelp, GSI(MSG_SEARCHMANAGERGROUP_ADD_HELP));
        set(bt_remove, MUIA_ShortHelp, GSI(MSG_SEARCHMANAGERGROUP_REMOVE_HELP));

        DoMethod(bt_remove, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MM_SearchManagerGroup_Remove);
        DoMethod(bt_add,    MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MM_SearchManagerGroup_Add);
        DoMethod(bt_up,     MUIM_Notify, up_is_toolbutton   ? MA_ToolButton_Pressed : MUIA_Pressed, FALSE, obj, 1, MM_SearchManagerGroup_Up);
        DoMethod(bt_down,   MUIM_Notify, down_is_toolbutton ? MA_ToolButton_Pressed : MUIA_Pressed, FALSE, obj, 1, MM_SearchManagerGroup_Down);
        DoMethod(lv_entries,  MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, obj, 1, MM_SearchManagerGroup_Update);

        DoMethod(st_request,  MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, obj, 1, MM_SearchManagerGroup_Change);
        DoMethod(st_label,    MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, obj, 1, MM_SearchManagerGroup_Change);
        DoMethod(st_shortcut, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, obj, 1, MM_SearchManagerGroup_Change);
    }

    return ((IPTR)obj);
}

DEFDISP
{
    return DOSUPER;
}

DEFSET
{
    GETDATA;
    doset(obj, data, msg->ops_AttrList);
    return DOSUPER;
}

DEFGET
{
    GETDATA;

    switch (msg->opg_AttrID)
    {
        case MA_SearchManagerGroup_Changed:
            *msg->opg_Storage = data->changed;
            return TRUE;

        // The saved search engines, not the entries being edited: the search bar updates only when "Save" is pressed.
        case MA_SearchManagerGroup_Labels:
        {
            if(data->labels.isEmpty())
            {
                for(ULONG i = 0; labels[i]; i++)
                {
                    data->labels.append(String(labels[i]));
                }
            }

            *msg->opg_Storage = (IPTR)&data->labels;
            return TRUE;
        }

        case MA_SearchManagerGroup_Requests:
        {
            if(data->requests.isEmpty())
            {
                for(ULONG i = 0; requests[i]; i++)
                {
                    data->requests.append(String(requests[i]));
                }
            }

            *msg->opg_Storage = (IPTR)&data->requests;
            return TRUE;
        }

        case MA_SearchManagerGroup_Shortcuts:
        {
            if(data->shortcuts.isEmpty())
            {
                for(ULONG i = 0; shortcuts[i]; i++)
                {
                    data->shortcuts.append(String(shortcuts[i]));
                }
            }

            *msg->opg_Storage = (IPTR)&data->shortcuts;
            return TRUE;
        }
    }

    return DOSUPER;
}

DEFTMETHOD(SearchManagerGroup_Load)
{
    GETDATA;

    load_searchengines(obj, data);

    // Use defaults if there's nothing in prefs
    if(getv(data->lv_entries, MUIA_List_Entries) == 0)
    {
        insert_defaults(data);
    }

    snapshot_from_list(data);

    set(obj, MA_SearchManagerGroup_Changed, TRUE);

    return 0;
}

DEFTMETHOD(SearchManagerGroup_Add)
{
    GETDATA;
    struct search_entry *entry = (struct search_entry *) malloc(sizeof(*entry));

    if(entry)
    {
        entry->label    = strdup("");
        entry->request  = strdup("");
        entry->shortcut = strdup("");

        DoMethod(data->lv_entries, MUIM_List_InsertSingle, entry, MUIV_List_Insert_Bottom);
        set(data->lv_entries, MUIA_List_Active, MUIV_List_Active_Bottom);
        set(_win(obj), MUIA_Window_ActiveObject, data->st_label);
    }

    return 0;
}

DEFTMETHOD(SearchManagerGroup_Remove)
{
    GETDATA;
    struct search_entry *entry;

    DoMethod(data->lv_entries, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (struct search_entry *) &entry);

    if(entry)
    {
        DoMethod(data->lv_entries, MUIM_List_Remove, MUIV_List_Remove_Active);

        // Don't allow empty list
        if(getv(data->lv_entries, MUIA_List_Entries) == 0)
        {
            insert_defaults(data);
        }

        DoMethod(obj, MM_SearchManagerGroup_Update);
    }

    return 0;
}

DEFTMETHOD(SearchManagerGroup_Update)
{
    GETDATA;
    struct search_entry *entry;

    DoMethod(data->lv_entries, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (struct search_entry *) &entry);

    if(entry)
    {
        LONG active = (LONG) getv(data->lv_entries, MUIA_List_Active);
        LONG count  = (LONG) getv(data->lv_entries, MUIA_List_Entries);

        set(data->bt_remove,   MUIA_Disabled, FALSE);
        set(data->st_label,    MUIA_Disabled, FALSE);
        set(data->st_request,  MUIA_Disabled, FALSE);
        set(data->st_shortcut, MUIA_Disabled, FALSE);
        set_button_enabled(data->bt_up,   data->up_is_toolbutton,   active > 0);
        set_button_enabled(data->bt_down, data->down_is_toolbutton, active < count - 1);
        nnset(data->st_label,    MUIA_String_Contents, entry->label);
        nnset(data->st_request,  MUIA_String_Contents, entry->request);
        nnset(data->st_shortcut, MUIA_String_Contents, entry->shortcut);
    }
    else
    {
        set(data->bt_remove,   MUIA_Disabled, TRUE);
        set(data->st_label,    MUIA_Disabled, TRUE);
        set(data->st_request,  MUIA_Disabled, TRUE);
        set(data->st_shortcut, MUIA_Disabled, TRUE);
        set_button_enabled(data->bt_up,   data->up_is_toolbutton,   FALSE);
        set_button_enabled(data->bt_down, data->down_is_toolbutton, FALSE);
        nnset(data->st_label,    MUIA_String_Contents, "");
        nnset(data->st_request,  MUIA_String_Contents, "");
        nnset(data->st_shortcut, MUIA_String_Contents, "");
    }

    return 0;
}

DEFTMETHOD(SearchManagerGroup_Change)
{
    GETDATA;
    struct search_entry *entry;

    DoMethod(data->lv_entries, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (struct search_entry *) &entry);

    if(entry)
    {
        free(entry->label);
        free(entry->request);
        free(entry->shortcut);
        entry->label    = strdup((char *) getv(data->st_label,    MUIA_String_Contents));
        entry->request  = strdup((char *) getv(data->st_request,  MUIA_String_Contents));
        entry->shortcut = strdup((char *) getv(data->st_shortcut, MUIA_String_Contents));

        DoMethod(data->lv_entries,  MUIM_List_Redraw, MUIV_List_Redraw_Entry, entry);
    }

    return 0;
}

// Write the edited list to the prefs file and make it the state the search bar uses.
DEFTMETHOD(SearchManagerGroup_Save)
{
    GETDATA;

    // An empty list would leave the search bar without engines
    if(getv(data->lv_entries, MUIA_List_Entries) == 0)
    {
        insert_defaults(data);
    }

    save_searchengines(obj, data);
    snapshot_from_list(data);

    set(obj, MA_SearchManagerGroup_Changed, TRUE);

    return 0;
}

// Throw away all edits: show the saved entries again.
DEFTMETHOD(SearchManagerGroup_Revert)
{
    GETDATA;

    fill_list_from_snapshot(data);
    set(data->lv_entries, MUIA_List_Active, MUIV_List_Active_Off);
    DoMethod(obj, MM_SearchManagerGroup_Update);

    return 0;
}

DEFTMETHOD(SearchManagerGroup_Up)
{
    GETDATA;
    move_active_entry(obj, data, -1);
    return 0;
}

DEFTMETHOD(SearchManagerGroup_Down)
{
    GETDATA;
    move_active_entry(obj, data, 1);
    return 0;
}

BEGINMTABLE
DECNEW
DECDISP
DECSET
DECGET
DECTMETHOD(SearchManagerGroup_Load)
DECTMETHOD(SearchManagerGroup_Add)
DECTMETHOD(SearchManagerGroup_Remove)
DECTMETHOD(SearchManagerGroup_Update)
DECTMETHOD(SearchManagerGroup_Change)
DECTMETHOD(SearchManagerGroup_Save)
DECTMETHOD(SearchManagerGroup_Revert)
DECTMETHOD(SearchManagerGroup_Up)
DECTMETHOD(SearchManagerGroup_Down)
ENDMTABLE

DECSUBCLASS_NC(MUIC_Group, searchmanagergroupclass)
