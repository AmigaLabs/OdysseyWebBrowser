/*
 * Copyright 2009 Fabien Coeurjoly <fabien.coeurjoly@wanadoo.fr>
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

#include "utils.h"

#include <libraries/locale.h>
#if OS(MORPHOS)
#include <proto/charsets.h>
#endif
#if OS(AROS)
#include <proto/codesets.h>
#endif
#include <proto/locale.h>

#if OS(AMIGAOS)
#include <proto/codesets.h>
#include <proto/application.h>
#endif

/* Preferred Languages list */

struct countrycode
{
    CONST_STRPTR language;
    CONST_STRPTR code;
};

static struct countrycode countrycode_table[] = {
#if OS(AMIGAOS)
    { "albanian",          "sq" },
    { "belarusian",        "be" },
    { "bosnian",           "bs" },
    { "bulgarian",         "bg" },
    { "catalan",           "ca" },
    { "croatian",          "hr" },
    { "czech",             "cs" },
    { "danish",            "da" },
    { "dutch",             "ne" },
    { "english",           "en-US" },
    { "english-british",   "en-GB" },
    { "esperanto",         "eo" },
    { "estonian",          "et" },
    { "faroese",           "fo" },
    { "finnish",           "fi" },
    { "french",            "fr" },
    { "galician",          "gl" },
    { "german",            "de" },
    { "greek",             "el" },
    { "hebrew",            "he" },
    { "hungarian",         "hu" },
    { "icelandic",         "is" },
    { "irish",             "ga" },
    { "italian",           "it" },
    { "latvian",           "lv" },
    { "lithuanian",        "lt" },
    { "macedonian",        "mk" },
    { "malay-indonesian",  "ms-ID" },
    { "malay-malaysian",   "ms-MY" },
    { "maltese",           "mt" },
    { "norwegian",         "no" },
    { "norwegian-nynorsk", "nn" },
    { "polish",            "pl" },
    { "portuguese",        "pt" },
    { "portuguese-brazil", "pt-BR" },
    { "romanian",          "ro" },
    { "russian",           "ru" },
    { "serbian",           "sr" },
    { "slovak",            "sk" },
    { "slovenian",         "sl" },
    { "spanish",           "sp" },
    { "swedish",           "sv" },
    { "thai",              "th" },
    { "turkish_int",       "tr" },
    { "turkish",           "tr" },
    { "ukrainian",         "uk" },
    { 0,                   0 }
#else	  

	{"dansk", "da"},
	{"deutsch", "de"},
	{"english", "en"},
	{"español", "sp"},
	{"français", "fr"},
	{"greek",    "gr"},
	{"italiano", "it"},
	{"magyar", "hu"},
	{"nederlands", "nl"},
	{"norsk", "no"},
	{"polski", "pl"},
	{"português", "pt"},
	{"suomi", "fi"},
	{"svenska", "sv"},
	{"türkiye", "tr"},
	{"èe¹tina", "cs"},
	{"russian", "ru"},
	{NULL, NULL}
#endif	
};

static CONST_STRPTR getcode(STRPTR language)
{
    int i;
    for(i = 0; countrycode_table[i].language; i++)
    {
        if(!stricmp(countrycode_table[i].language, language))
        {
            return countrycode_table[i].code;
        }
    }

    return "en"; /* Just default to "en" */
}

STRPTR get_language(STRPTR code, ULONG len)
{
    struct Locale *l = OpenLocale(NULL);
    STRPTR ret = NULL;

    if(l)
    {
        if(l->loc_PrefLanguages[0])
        {
            ret = code;
            stccpy(code, (char *) getcode(l->loc_PrefLanguages[0]), len);
        }

        CloseLocale(l);
    }

    if(!ret)
    {
        ret = code;
        stccpy(code, "en", len);
    }

    return ret;
}
