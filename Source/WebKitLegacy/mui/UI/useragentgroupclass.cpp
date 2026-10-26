/*
 * Copyright 2009 Fabien Coeurjoly <fabien.coeurjoly@wanadoo.fr>
 * Copyright 2009 Ilkka Lehtoranta <ilkleht@isoveli.org>
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
#include <WebCore/GraphicsContext.h>
#include <wtf/text/WTFString.h>
#include <wtf/text/CString.h>
#include <wtf/Vector.h>
#include "FileIOLinux.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gui.h"
#include "utils.h"

/* Debug output to serial handled via D(bug("....."));
*  See Base/debug.h for details.
*  D(x)    - to disable debug
*  D(x) x  - to enable debug
*/
#define D(x)


#include "useragent.h"
#include "versioninfo.h"

#if OS(AROS) && CPU(X86)
#define OSHEADER "AROS i686"
#elif OS(AROS) && CPU(X86_64)
#define OSHEADER "AROS x86_64"
#elif OS(MORPHOS)
#define OSHEADER "Macintosh; PowerPC MorphOS %u.%u"
#elif OS(AMIGAOS)
#define OSHEADER "PowerPC AmigaOS"
#endif
static const char verfmt[] = "Mozilla/5.0 (" OSHEADER "; Odyssey Web Browser; rv:" VERSION ") AppleWebKit/" WEBKITVER " (KHTML, like Gecko) OWB/" VERSION " Safari/" WEBKITVER;
static char odysseyuseragent[sizeof(verfmt) + 2 * (10 - 2)];
STATIC CONST_STRPTR useragents_labels2[UA_MAX_INMENU + 1];
STATIC CONST_STRPTR useragents_strings2[UA_MAX_INMENU + 1];
STATIC CONST_STRPTR ua_builtin_labels[] = {
    "Odyssey Web Browser", "Firefox (Linux)", "Firefox (Windows)",
    "Windows 10 Edge", "Windows 10 Chrome", "Internet Explorer 6",
    "Opera (Windows)", "Safari 13.1 (Mac)", "Chrome 42 (Linux)",
    "Apple IPhone 16e", "Apple IPad Pro", "Samsung Galaxy S25 Ultra",
    "Google Pixel 9 Pro", NULL
};
STATIC CONST_STRPTR ua_builtin_strings[] = {
    odysseyuseragent,
    "Mozilla/5.0 (X11; Linux i686; rv:138.0) Gecko/20100101 Firefox/138.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:138.0) Gecko/20100101 Firefox/138.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36 Edg/134.0.0.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36",
    "Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; .NET CLR 1.1.4322)",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36 OPR/118.0.0.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.3.1 Safari/605.1.15",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36",
    "Mozilla/5.0 (iPhone17,5; CPU iPhone OS 18_3_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148 FireKeepers/1.7.0",
    "Mozilla/5.0 (iPad16,3; CPU OS 18_3_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148 Tropicana_NJ/5.7.1",
    "Mozilla/5.0 (Linux; Android 15; SM-S931B Build/AP3A.240905.015.A2; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/127.0.6533.103 Mobile Safari/537.36",
    "Mozilla/5.0 (Linux; Android 14; Pixel 9 Pro Build/AD1A.240418.003; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/124.0.6367.54 Mobile Safari/537.36",
    NULL
};
#define TOTAL_BUILTIN_UA (sizeof(ua_builtin_labels) / sizeof(ua_builtin_labels[0]) - 1)

ULONG get_user_agent_count()
{
    ULONG i = 0;
    while (i < UA_MAX_INMENU && useragents_labels2[i])
        ++i;
    return i;
}

ULONG get_user_agent_fullcount()
{
    ULONG count = 0;
    APTR node;
    ITERATELIST(node, &useragent_list)
        ++count;
    return count;
}

CONST_STRPTR *get_user_agent_labels()
{
    return useragents_labels2;
}

CONST_STRPTR *get_user_agent_strings()
{
    return useragents_strings2;
}

ULONG get_default_user_agent_idx()
{
    APTR node;
    ITERATELIST(node, &useragent_list) {
        auto* agent = (struct useragentnode*)node;
        if (!(agent->uan_flags & UA_DEFAULT))
            continue;
        for (ULONG index = 0; index < get_user_agent_count(); ++index) {
            if (!strcmp(agent->uan_string, useragents_strings2[index]))
                return index;
        }
    }
    return 0;
}

ULONG get_builtin_user_agents()
{
    return TOTAL_BUILTIN_UA;
}

void init_useragent()
{
#if OS(MORPHOS)
    sprintf(odysseyuseragent, verfmt, 4, 1);
#else
    strcpy(odysseyuseragent, verfmt);
#endif
}


using namespace WebCore;


void useragent_delete(struct useragentnode *uan)
{
	REMOVE(uan);
	//uan->uan_flags = 0;
	free(uan->uan_status);
	free(uan->uan_label);
	free(uan->uan_string);
	free(uan);
}

struct Data
{
	Object *lv_useragents;
	Object *bt_add;
	Object *bt_remove;
	Object *bt_active; // changes ua_status (flags) to UA_DEFAULT
	Object *st_useragent_label;
	Object *st_useragent;
	Object *ch_showinmenu;
	Object *txt_info;
};


struct useragentnode* useragent_create(int uaflags, const char *ualabel, const char *uastring, struct MinList *ualist)
{
    auto *un = (struct useragentnode *)calloc(1, sizeof(struct useragentnode));
    if (!un)
        return nullptr;
    un->uan_flags = uaflags;
    un->uan_status = strdup(GSI((uaflags & UA_DEFAULT) ? MSG_USERAGENTLIST_STATUS_DEFAULT : (uaflags & UA_SHOW) ? MSG_USERAGENTLIST_STATUS_ENABLED : MSG_USERAGENTLIST_STATUS_DISABLED));
    un->uan_label = strdup(ualabel ? ualabel : "");
    un->uan_string = strdup(uastring ? uastring : "");
    if (!un->uan_status || !un->uan_label || !un->uan_string) {
        free(un->uan_status);
        free(un->uan_label);
        free(un->uan_string);
        free(un);
        return nullptr;
    }
    ADDTAIL(ualist, un);
    return un;
}

void rebuild_ua_array_menu(void)
{
    CONST_STRPTR nextLabels[UA_MAX_INMENU + 1] = { nullptr };
    CONST_STRPTR nextStrings[UA_MAX_INMENU + 1] = { nullptr };
    ULONG nextCount = 0;
    while (nextCount < TOTAL_BUILTIN_UA && nextCount < UA_MAX_INMENU) {
        nextLabels[nextCount] = ua_builtin_labels[nextCount];
        nextStrings[nextCount] = ua_builtin_strings[nextCount];
        ++nextCount;
    }
    APTR node;
    for (int pass = 0; pass < 2; ++pass) {
        ITERATELIST(node, &useragent_list) {
            auto* un = (struct useragentnode*)node;
            bool isDefault = un->uan_flags & UA_DEFAULT;
            if ((un->uan_flags & UA_BUILTIN) || !(un->uan_flags & UA_SHOW) || isDefault != (pass == 0) || nextCount >= UA_MAX_INMENU)
                continue;
            char* label = strdup(un->uan_label);
            char* agent = strdup(un->uan_string);
            if (label && agent) {
                nextLabels[nextCount] = label;
                nextStrings[nextCount] = agent;
                ++nextCount;
            } else {
                free(label);
                free(agent);
            }
        }
    }

    ULONG oldCount = get_user_agent_count();
    CONST_STRPTR oldLabels[UA_MAX_INMENU + 1];
    CONST_STRPTR oldStrings[UA_MAX_INMENU + 1];
    memcpy(oldLabels, useragents_labels2, sizeof(oldLabels));
    memcpy(oldStrings, useragents_strings2, sizeof(oldStrings));
    ITERATELIST(node, &urlsetting_list) {
        auto* setting = (struct urlsettingnode*)node;
        LONG oldIndex = setting->settings.useragent;
        if (oldIndex < 0)
            continue;
        if ((ULONG)oldIndex >= oldCount) {
            setting->settings.useragent = USERAGENT_DEFAULT;
            continue;
        }
        ULONG newIndex = 0;
        while (newIndex < nextCount && strcmp(useragents_strings2[oldIndex], nextStrings[newIndex]))
            ++newIndex;
        if (newIndex == nextCount) {
            newIndex = 0;
            while (newIndex < nextCount && strcmp(useragents_labels2[oldIndex], nextLabels[newIndex]))
                ++newIndex;
        }
        setting->settings.useragent = newIndex < nextCount ? (LONG)newIndex : (LONG)USERAGENT_DEFAULT;
    }
    memcpy(useragents_labels2, nextLabels, sizeof(nextLabels));
    memcpy(useragents_strings2, nextStrings, sizeof(nextStrings));
    ASSERT(get_user_agent_count() == nextCount);
    refresh_urlprefs_user_agent_cycle();
    ITERATELIST(node, &view_list) {
        auto* view = (BalWidget*)node;
        Object* browser = view->browser;
        LONG oldIndex = browser ? (LONG)getv(browser, MA_OWBBrowser_UserAgent) : (LONG)USERAGENT_DEFAULT;
        if (oldIndex < 0)
            continue;
        if ((ULONG)oldIndex >= oldCount) {
            set(browser, MA_OWBBrowser_UserAgent, USERAGENT_DEFAULT);
            continue;
        }
        ULONG newIndex = 0;
        while (newIndex < nextCount && strcmp(oldStrings[oldIndex], useragents_strings2[newIndex]))
            ++newIndex;
        if (newIndex == nextCount) {
            newIndex = 0;
            while (newIndex < nextCount && strcmp(oldLabels[oldIndex], useragents_labels2[newIndex]))
                ++newIndex;
        }
        if (newIndex < nextCount) {
            if (oldIndex != (LONG)newIndex || strcmp(oldStrings[oldIndex], useragents_strings2[newIndex]))
                set(browser, MA_OWBBrowser_UserAgent, (IPTR)newIndex);
        } else
            set(browser, MA_OWBBrowser_UserAgent, (IPTR)USERAGENT_DEFAULT);
    }
    for (ULONG i = TOTAL_BUILTIN_UA; i < oldCount; ++i) {
        free((void*)oldLabels[i]);
        free((void*)oldStrings[i]);
    }
}

void init_useragents()
{
    if (!IsMinListEmpty(&useragent_list))
        return;
    init_useragent();
    for (ULONG i = 0; i < TOTAL_BUILTIN_UA; ++i)
        useragent_create(UA_BUILTIN | UA_SHOW, ua_builtin_labels[i], ua_builtin_strings[i], &useragent_list);

    bool hasDefault = false;
    OWBFile file("PROGDIR:Conf/useragents.prefs");
    if (file.open('r') != -1) {
        char* buffer = file.read(file.getSize());
        if (buffer) {
            Vector<String> lines = String(buffer).split("\n");
            delete[] buffer;
            for (auto& line : lines) {
                Vector<String> attributes = line.split("\1");
                if (attributes.size() != 3 || get_user_agent_fullcount() >= UA_MAX_INLIST)
                    continue;
                CString flagString = attributes[0].latin1();
                long savedFlags = strtol(flagString.data(), nullptr, 10);
                int flags = savedFlags & (UA_DEFAULT | UA_SHOW);
                hasDefault |= flags & UA_DEFAULT;
                CString label = attributes[1].latin1();
                CString userAgent = attributes[2].latin1();
                bool isBuiltin = false;
                APTR builtinNode;
                if (savedFlags & UA_BUILTIN) ITERATELIST(builtinNode, &useragent_list) {
                    auto* builtin = (struct useragentnode*)builtinNode;
                    if ((builtin->uan_flags & UA_BUILTIN) && !strcmp(builtin->uan_label, label.data())) {
                        builtin->uan_flags = UA_BUILTIN | flags;
                        free(builtin->uan_status);
                        builtin->uan_status = strdup(GSI((flags & UA_DEFAULT) ? MSG_USERAGENTLIST_STATUS_DEFAULT : (flags & UA_SHOW) ? MSG_USERAGENTLIST_STATUS_ENABLED : MSG_USERAGENTLIST_STATUS_DISABLED));
                        isBuiltin = true;
                        break;
                    }
                }
                if (!isBuiltin)
                    useragent_create(flags, label.data(), userAgent.data(), &useragent_list);
            }
        }
        file.close();
    }
    if (!IsMinListEmpty(&useragent_list)) {
        bool foundDefault = false;
        APTR node;
        ITERATELIST(node, &useragent_list) {
            auto* un = (struct useragentnode*)node;
            if (un->uan_flags & UA_DEFAULT) {
                if (foundDefault) {
                    un->uan_flags &= ~UA_DEFAULT;
                    free(un->uan_status);
                    un->uan_status = strdup(GSI((un->uan_flags & UA_SHOW) ? MSG_USERAGENTLIST_STATUS_ENABLED : MSG_USERAGENTLIST_STATUS_DISABLED));
                } else {
                    un->uan_flags |= UA_SHOW;
                    foundDefault = true;
                }
            }
        }
        if (!hasDefault || !foundDefault) {
            auto* un = (struct useragentnode*)GetHead((struct List*)&useragent_list);
            un->uan_flags |= UA_DEFAULT | UA_SHOW;
            free(un->uan_status);
            un->uan_status = strdup(GSI(MSG_USERAGENTLIST_STATUS_DEFAULT));
        }
    }
    rebuild_ua_array_menu();
}

void moveList(struct MinList *from, struct MinList *to)
{
	//if(!from || IsListEmpty((struct List *)from))
	if( !from || IsMinListEmpty(from) )
	{
		/*if(to)
			NEWLIST(to);*/
		return;
	}
	to->mlh_Head = from->mlh_Head;
	to->mlh_Tail = NULL;
	to->mlh_TailPred = from->mlh_TailPred;
	to->mlh_Head->mln_Pred = (struct MinNode *)&to->mlh_Head;
	to->mlh_TailPred->mln_Succ = (struct MinNode *)&to->mlh_Tail;
	//NEWLIST(from);
}

void clone_ua_list(struct MinList *src_list, struct MinList *dst_list)
{
	struct useragentnode *tmp;
	APTR n, m;
	D(bug("clone_ua_list()... src=0x%08x -> dts=0x%08x\n",src_list,dst_list));
	ITERATELISTSAFE(n, m, dst_list)
	{
		useragent_delete((useragentnode *) n);
	}
	//NEWLIST(dst_list);

	ITERATELIST(n, src_list)
	{
		tmp = (useragentnode *) n;

        useragent_create(tmp->uan_flags, tmp->uan_label, tmp->uan_string, dst_list);
	}
/*
	struct MinList temp;

	moveList(src_list, &temp);
	moveList(dst_list, src_list);
	moveList(&temp, dst_list);
*/
}

void use_useragents(void)
{
	D(bug("use_useragents() (ua_list -> BAK)\n"));
	clone_ua_list(&useragent_list, &ua_list_bak);
	rebuild_ua_array_menu();
}

void save_useragents(void)
{
	D(bug("save_useragents() (list -> FILE and BAK)\n"));
	//char dbg_str[64] = "";
	struct useragentnode *un;
	OWBFile *uaFile = new OWBFile("PROGDIR:Conf/useragents.prefs");
	APTR n;

	if(!uaFile)
		return;

	if (uaFile->open('w') == -1)
	{
		delete uaFile;
		return;
	}

	D(bug("FILE 'PROGDIR:Conf/useragents.prefs':\n"));
	ITERATELIST(n, &useragent_list)
	{
		un = (useragentnode *) n;

		if (!(un->uan_flags & UA_BUILTIN) || (un->uan_flags & UA_DEFAULT))
			uaFile->write(createWithFormatAndArguments("%d\1%s\1%s\n", un->uan_flags, un->uan_label, un->uan_string));
	}

	uaFile->close();
	delete uaFile;

	clone_ua_list(&useragent_list, &ua_list_bak);
	rebuild_ua_array_menu();
}

/*void load_useragents(void)
{
D(bug("load_useragents()\n"));
}*/

void cancel_useragents(void)
{
	D(bug("cancel_useragents() (BAK -> ua_list)\n"));
	clone_ua_list(&ua_list_bak, &useragent_list);
	rebuild_ua_array_menu();
}


/*
 * "Homage" to IBrowse v2.5 spoofing prefs (THXaLOT Oliver Roberts)
 */
DEFNEW
{
	Object *lv_useragents, *bt_add, *bt_remove, *bt_active, *st_useragent_label, *st_useragent, *ch_showinmenu, *txt_info;
	D(bug("UserAgent DEFNEW\n"));
	obj = (Object *) DoSuperNew(cl, obj,
			MUIA_ObjectID, MAKE_ID('S','M','0','2'),
			Child, lv_useragents = (Object *) NewObject(getuseragentlistclass(), NULL, TAG_DONE),
			Child, HGroup,
			       Child, bt_add = (Object *) MakeButton(GSI(MSG_USERAGENTGROUP_ADD)),
			       Child, bt_remove = (Object *) MakeButton(GSI(MSG_USERAGENTGROUP_REMOVE)),
			       Child, bt_active = (Object *) MakeButton(GSI(MSG_USERAGENTGROUP_MAKE_DEFAULT)),
			       End,
			Child, txt_info = TextObject,
			       MUIA_Text_Contents, GSI(MSG_USERAGENTLIST_INFO),//"NOTE: max. 64 entries; max. in menu 32",
			       MUIA_Text_SetMin, FALSE,
			       End,
			Child, HSpace(0),

			Child, ColGroup(2),
				Child, MakeLabel(GSI(MSG_USERAGENTGROUP_NAME)),
				Child, HGroup,
				       Child, st_useragent_label = (Object *) MakeString("", FALSE),
				       Child, ch_showinmenu = (Object *) MakeCheck(GSI(MSG_USERAGENTGROUP_SHOW_IN_MENU), FALSE),//, MAKE_ID('S','U','A','1')),
				       Child, LLabel(GSI(MSG_USERAGENTGROUP_SHOW_IN_MENU)),
				       End,
				Child, MakeLabel(GSI(MSG_USERAGENTGROUP_USERAGENT)),
				Child, st_useragent = (Object *) MakeString("", FALSE),

				End,
	      TAG_MORE, msg->ops_AttrList);

	if (obj)
	{
		GETDATA;
		D(bug("odysseyuseragent: '%s'\n",odysseyuseragent));
		data->bt_active = bt_active;
		data->bt_remove = bt_remove;
		data->bt_add = bt_add;
		data->lv_useragents = lv_useragents;
		data->txt_info = txt_info;

		data->st_useragent_label = st_useragent_label;
		data->st_useragent = st_useragent;
		data->ch_showinmenu = ch_showinmenu;

		DoMethod(bt_add,    MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MM_UserAgentGroup_Add);
		DoMethod(bt_remove, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MM_UserAgentGroup_Remove);
		DoMethod(bt_active, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, MM_UserAgentGroup_Active);
		DoMethod(ch_showinmenu, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, obj, 1, MM_UserAgentGroup_ShowInMenu);

		DoMethod(lv_useragents, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, obj, 1, MM_UserAgentGroup_Refresh);

		DoMethod(st_useragent_label, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, obj, 1, MM_UserAgentGroup_Change);
		DoMethod(st_useragent, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, obj, 1, MM_UserAgentGroup_Change);
	}

	return (IPTR)obj;
}

DEFDISP
{
	GETDATA;
	D(bug("UserAgent DEFDISP:\n"));
	DoMethod(data->lv_useragents, MUIM_List_Clear);

	// User-agent lists and arrays belong to the application, not this preference page.
	return DOSUPER;
}

DEFTMETHOD(UserAgentGroup_Refresh)
{
	GETDATA;
	struct useragentnode *un;

	DoMethod(data->lv_useragents, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (struct useragentnode *) &un);

	if(un)
	{
		bool builtin = (un->uan_flags&UA_BUILTIN);
		bool no_removable = (un->uan_flags&UA_DEFAULT) ? 1 : builtin; // default spoof NOT removable
		//D(bug("clicked entry un->uan_flags=0x%08x\n",un->uan_flags));
		//D(bug("is built-in (NOT removable) = %d\n",builtin));
		//D(bug("is default spoof (NOT removable) = %d\n",(un->uan_flags&UA_DEFAULT)? TRUE : FALSE));
		set(data->bt_remove, MUIA_Disabled, no_removable);
		set(data->bt_active, MUIA_Disabled, (un->uan_flags&UA_DEFAULT)? TRUE : FALSE);

		set(data->st_useragent_label, MUIA_Disabled, builtin);
		set(data->ch_showinmenu, MUIA_Disabled, builtin);
		set(data->st_useragent, MUIA_Disabled, builtin);

		nnset(data->st_useragent_label, MUIA_String_Contents, un->uan_label);
		nnset(data->ch_showinmenu, MUIA_Selected, un->uan_flags&UA_SHOW);
		nnset(data->st_useragent, MUIA_String_Contents, un->uan_string);
	}
	else
	{
		set(data->bt_active, MUIA_Disabled, TRUE);
		set(data->bt_remove, MUIA_Disabled, TRUE);

		set(data->st_useragent_label, MUIA_Disabled, TRUE);
		set(data->ch_showinmenu, MUIA_Disabled, TRUE);
		set(data->st_useragent, MUIA_Disabled, TRUE);
	}

	return 0;
}

DEFTMETHOD(UserAgentGroup_Active)
{
	GETDATA;
	struct useragentnode *un;
	APTR n;

	//Remove UA_DEFAULT (default/active) flag
	ITERATELIST(n, &useragent_list)
	{
		un = (struct useragentnode *)n;

		if( (un->uan_flags&UA_DEFAULT) )
		{
			//D(bug("1)OLD un->uan_flags=0x%08x\n",un->uan_flags));
			un->uan_flags &= ~(UA_DEFAULT);
			//D(bug("2)OLD un->uan_flags=0x%08x\n",un->uan_flags));
			free(un->uan_status);
			if( un->uan_flags&UA_SHOW ) un->uan_status = strdup(GSI(MSG_USERAGENTLIST_STATUS_ENABLED));
			else un->uan_status = strdup(GSI(MSG_USERAGENTLIST_STATUS_DISABLED));

			DoMethod(data->lv_useragents, MUIM_List_Redraw, MUIV_List_Redraw_Entry, un);
			break;
		}
	}

	//Add UA_DEFAULT (default/active) flag
	DoMethod(data->lv_useragents, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (struct useragentnode *) &un);

	if(un)
	{
		//D(bug("1)NEW un->uan_flags=0x%08x\n",un->uan_flags));
		un->uan_flags |= UA_DEFAULT | UA_SHOW;
		//D(bug("2)NEW un->uan_flags=0x%08x\n",un->uan_flags));
		free(un->uan_status);
		un->uan_status = strdup(GSI(MSG_USERAGENTLIST_STATUS_DEFAULT));

		DoMethod(data->lv_useragents, MUIM_List_Redraw, MUIV_List_Redraw_Entry, un);
        nnset(data->ch_showinmenu, MUIA_Selected, TRUE);
        rebuild_ua_array_menu();
		set(data->bt_active, MUIA_Disabled, TRUE);
	}

	return 0;
}

DEFTMETHOD(UserAgentGroup_ShowInMenu)
{
	GETDATA;
	struct useragentnode *un;
	/*ULONG res = getv(data->ch_showinmenu, MUIA_Selected); // res=1 -> "adding" to menu
	D(bug("UserAgentGroup_ShowInMenu: %d (%d)\n",get_user_agent_count(),res));
	if(res && get_user_agent_count()==UA_MAX_INMENU)
	{
		D(bug("Maximum entries reached!\n"));
		nnset(data->ch_showinmenu, MUIA_Selected, FALSE);
		return 0;
	}*/

	DoMethod(data->lv_useragents, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (struct useragentnode *) &un);

	if(un)
	{
        if ((un->uan_flags & (UA_DEFAULT | UA_SHOW)) == (UA_DEFAULT | UA_SHOW)) {
            nnset(data->ch_showinmenu, MUIA_Selected, TRUE);
            return 0;
        }
        if (!(un->uan_flags & UA_SHOW) && get_user_agent_count() >= UA_MAX_INMENU) {
            nnset(data->ch_showinmenu, MUIA_Selected, FALSE);
            return 0;
        }
		if( (un->uan_flags&UA_SHOW) ) un->uan_flags &= ~(UA_SHOW); // remove ENABLE/SHOW flag
		else un->uan_flags |= UA_SHOW;
		D(bug("Changed status to 0x%08x\n",un->uan_flags));

		// Update uan_status string
		free(un->uan_status);
		if( un->uan_flags&UA_DEFAULT ) un->uan_status = strdup(GSI(MSG_USERAGENTLIST_STATUS_DEFAULT));
		else if( un->uan_flags&UA_SHOW ) un->uan_status = strdup(GSI(MSG_USERAGENTLIST_STATUS_ENABLED));
		     else un->uan_status = strdup(GSI(MSG_USERAGENTLIST_STATUS_DISABLED));

		DoMethod(data->lv_useragents, MUIM_List_Redraw, MUIV_List_Redraw_Entry, un);

		//rebuild_ua_array_menu();
	}

	return 0;
}

DEFTMETHOD(UserAgentGroup_Change)
{
	GETDATA;
	struct useragentnode *un;

	DoMethod(data->lv_useragents, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (struct useragentnode *) &un);

	if(un)
	{
		free(un->uan_label);
		un->uan_label = strdup((char *) getv(data->st_useragent_label, MUIA_String_Contents));
		free(un->uan_string);
		un->uan_string = strdup((char *) getv(data->st_useragent, MUIA_String_Contents));

		DoMethod(data->lv_useragents,  MUIM_List_Redraw, MUIV_List_Redraw_Entry, un);
	}

	return 0;
}

DEFTMETHOD(UserAgentGroup_Add)
{
	GETDATA;
	struct useragentnode *un;
	D(bug("UserAgentGroup_Add: %d\n",get_user_agent_fullcount()));
	if(get_user_agent_fullcount() == UA_MAX_INLIST)
	{
		return 0;
	}

	un = useragent_create(0, "", "", &useragent_list);
	if(un)
	{
		DoMethod(data->lv_useragents, MUIM_List_InsertSingle, un, MUIV_List_Insert_Bottom);
		set(data->lv_useragents, MUIA_List_Active, MUIV_List_Active_Bottom);
		set(_win(obj), MUIA_Window_ActiveObject, data->st_useragent_label);
	}

	return 0;
}

DEFTMETHOD(UserAgentGroup_Remove)
{
	GETDATA;
	struct useragentnode *un;

	DoMethod(data->lv_useragents, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (struct useragentnode *) &un);

	if(un)
	{
		DoMethod(data->lv_useragents, MUIM_List_Remove, MUIV_List_Remove_Active);
		useragent_delete(un);
	}

	return 0;
}

DEFMMETHOD(Import)
{
	GETDATA;
	APTR n;//, m;
	D(bug("UserAgent IMPORT:\n"));
    init_useragents();
    DoMethod(data->lv_useragents, MUIM_List_Clear);
    set(data->lv_useragents, MUIA_List_Quiet, TRUE);
    ITERATELIST(n, &useragent_list)
        DoMethod(data->lv_useragents, MUIM_List_InsertSingle, (useragentnode *)n, MUIV_List_Insert_Bottom);
    set(data->lv_useragents, MUIA_List_Quiet, FALSE);
    if (IsMinListEmpty(&ua_list_bak))
        clone_ua_list(&useragent_list, &ua_list_bak);

	set(data->bt_active, MUIA_Disabled, TRUE);
	set(data->bt_remove, MUIA_Disabled, TRUE);

	nnset(data->st_useragent_label, MUIA_String_Contents, "");
	nnset(data->ch_showinmenu, MUIA_Selected, FALSE);
	nnset(data->st_useragent, MUIA_String_Contents, "");

	set(data->st_useragent_label, MUIA_Disabled, TRUE);
	set(data->st_useragent, MUIA_Disabled, TRUE);
	set(data->ch_showinmenu, MUIA_Disabled, TRUE);

	return 0;
}

DEFMMETHOD(Export)
{
	D(bug("UserAgent EXPORT:\n"));
/* SAVE (if clicked on SAVE button) is done in appclass.cpp's OWBApp_PrefsSave
 * method (MM_PrefsWindow_SaveUserAgents).
 */
	return 0;
}


BEGINMTABLE
DECNEW
DECDISP
DECMMETHOD(Import)
DECMMETHOD(Export)
DECTMETHOD(UserAgentGroup_Add)
DECTMETHOD(UserAgentGroup_Remove)
DECTMETHOD(UserAgentGroup_Change)
DECTMETHOD(UserAgentGroup_Refresh)
DECTMETHOD(UserAgentGroup_ShowInMenu)
DECTMETHOD(UserAgentGroup_Active)
ENDMTABLE

DECSUBCLASS_NC(MUIC_Group, useragentgroupclass)
