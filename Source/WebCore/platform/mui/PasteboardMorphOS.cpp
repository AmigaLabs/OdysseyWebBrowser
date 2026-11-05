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
#include "Logging.h"
#include "Pasteboard.h"
#include "CachedImage.h"
#include "DataObjectMorphOS.h"
#include "DragData.h"
#include "DocumentFragment.h"
#include "Editor.h"
#include "Frame.h"
#include "HTMLImageElement.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "HTMLParserIdioms.h"
#include "Image.h"
#include <wtf/URL.h>
#include <wtf/text/MakeString.h>
#include "PasteboardHelper.h"
#include "RenderImage.h"
#include "markup.h"

#if ENABLE(SVG)
#include "SVGNames.h"
#include "XLinkNames.h"
#endif

#include <cstdio>

#include <devices/clipboard.h>
#include <libraries/locale.h>
#include <proto/exec.h>    
#include <proto/iffparse.h>
#include <proto/intuition.h>
#if OS(AMIGAOS)
#define ODYSSEY
#endif
#include <proto/datatypes.h>
#include <proto/iffparse.h>
#include <datatypes/textclass.h>
#include <datatypes/pictureclass.h>
#include "utils.h"
#include "clipboard.h"

// shortcut: local copies of the WebKitLegacy clipboard.h codeset ids, which this file cannot reach from WebCore include paths
#ifndef CODESET_UTF8
#define CODESET_UTF8 1
#endif
#ifndef CODESET_LOCAL
#define CODESET_LOCAL 2
#endif

#undef String

#define D(x)

namespace WebCore {

static void writeUTF8(const char* data, size_t len)
{
    IFFHandle *ih;

    bool copied = false;

    if(data && len)
    {
        char *converted = utf8_to_local(data);

        if(converted)
        {
            if ((ih = AllocIFF()))
            {
                if (ClipboardHandle *ch = OpenClipboard(PRIMARY_CLIP))
                {
                    ih->iff_Stream = (IPTR)ch;
                    InitIFFasClip(ih);

                    if (0 == OpenIFF(ih, IFFF_WRITE))
                    {
                        if (0 == PushChunk(ih, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN))
                        {
                            if (0 == PushChunk(ih, 0, ID_CHRS, IFFSIZE_UNKNOWN)
                             && WriteChunkBytes(ih, (APTR)converted, strlen(converted)) > 0
                             && 0 == PopChunk(ih)
                             && 0 == PushChunk(ih, 0, ID_UTF8, IFFSIZE_UNKNOWN))
                            {
                                if (WriteChunkBytes(ih, (APTR)data, len) > 0)
                                    copied = true;
                                PopChunk(ih);
                            }

                            PopChunk(ih);
                        }
                        CloseIFF(ih);
                    }
                    CloseClipboard(ch);
                }
                FreeIFF(ih);
            }
            free(converted);
        }
    }

    if (!copied)
    {
        DisplayBeep(NULL);
    }
}

enum ClipboardDataType {
    ClipboardDataTypeText,
    ClipboardDataTypeMarkup,
    ClipboardDataTypeURIList,
    ClipboardDataTypeURL,
    ClipboardDataTypeImage,
    ClipboardDataTypeUnknown
};

class MorphOSPasteboardContext final : public PasteboardContext {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(MorphOSPasteboardContext);
public:
    MorphOSPasteboardContext(RefPtr<DataObjectMorphOS>&& dataObject, int clipboard)
        : dataObject(WTFMove(dataObject))
        , clipboard(clipboard)
    {
    }

    RefPtr<DataObjectMorphOS> dataObject;
    int clipboard;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(MorphOSPasteboardContext);

// Pasteboard has no slot for the data object, so it lives in the context.
static DataObjectMorphOS* dataObjectOf(const Pasteboard& pasteboard)
{
    return static_cast<const MorphOSPasteboardContext&>(*pasteboard.context()).dataObject.get();
}

static int clipboardOf(const Pasteboard& pasteboard)
{
    return static_cast<const MorphOSPasteboardContext&>(*pasteboard.context()).clipboard;
}

static std::unique_ptr<Pasteboard> makeMorphOSPasteboard(RefPtr<DataObjectMorphOS>&& dataObject, int clipboard)
{
    return makeUnique<Pasteboard>(makeUnique<MorphOSPasteboardContext>(WTFMove(dataObject), clipboard));
}

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context)
    : m_context(WTFMove(context))
{
}

std::unique_ptr<Pasteboard> Pasteboard::createForCopyAndPaste(std::unique_ptr<PasteboardContext>&&)
{
    D(kprintf("Pasteboard::createForCopyAndPaste\n"));
    return makeMorphOSPasteboard(DataObjectMorphOS::forClipboard(0), 0);
}

std::unique_ptr<Pasteboard> Pasteboard::createForDragAndDrop(std::unique_ptr<PasteboardContext>&&)
{
    D(kprintf("Pasteboard::createForDragAndDrop\n"));
    return makeMorphOSPasteboard(DataObjectMorphOS::create(), 1);
}

std::unique_ptr<Pasteboard> Pasteboard::create(const DragData& dragData)
{
    D(kprintf("Pasteboard::createForDragAndDrop(dragData)\n"));
    // Drag data arrives as SelectionData; copy it into a DataObjectMorphOS. Images are not carried over.
    RefPtr<DataObjectMorphOS> dataObject = DataObjectMorphOS::create();
    const SelectionData& source = *dragData.platformData();
    if (source.hasText())
        dataObject->setText(source.text());
    if (source.hasMarkup())
        dataObject->setMarkup(source.markup());
    if (source.hasURL())
        dataObject->setURL(source.url(), source.urlLabel());
    if (source.hasURIList())
        dataObject->setURIList(source.uriList());
    return makeMorphOSPasteboard(WTFMove(dataObject), 1);
}

static ClipboardDataType dataObjectTypeFromHTMLClipboardType(const String& rawType)
{
    String type(rawType.trim(isASCIIWhitespace));

    D(kprintf("dataObjectTypeFromHTMLClipboardType %s\n", type.utf8().data()));

    // Two special cases for IE compatibility
    if (type == "Text"_s || type == "text"_s)
        return ClipboardDataTypeText;
    if (type == "URL"_s)
        return ClipboardDataTypeURL;

    // From the Mac port: Ignore any trailing charset - JS strings are
    // Unicode, which encapsulates the charset issue.
    if (type == "text/plain"_s || type.startsWith("text/plain;"_s))
        return ClipboardDataTypeText;
    if (type == "text/html"_s || type.startsWith("text/html;"_s))
        return ClipboardDataTypeMarkup;
    if (type == "Files"_s || type == "text/uri-list"_s || type.startsWith("text/uri-list;"_s))
        return ClipboardDataTypeURIList;

    // Not a known type, so just default to using the text portion.
    return ClipboardDataTypeUnknown;
}

void Pasteboard::writeString(const String& type, const String& data)
{
    D(kprintf("Pasteboard::writeString %s %s\n", type.utf8().data(), data.utf8().data()));

    switch (dataObjectTypeFromHTMLClipboardType(type)) {
    case ClipboardDataTypeURIList:
    case ClipboardDataTypeURL:
    {
        if(clipboardOf(*this) == 0)
        {
            /* Copy to system clipboard first (clipboard monitor will reset rich content as a result) */
            char *text = strdup(data.utf8().data());
            writeUTF8(text, strlen(text));
            free(text);
        }
        dataObjectOf(*this)->setURIList(data);
        return;
    }
    case ClipboardDataTypeMarkup:
        dataObjectOf(*this)->setMarkup(data);
        return;
    case ClipboardDataTypeText:
    {
        if(clipboardOf(*this) == 0)
        {
            /* Copy to system clipboard first (clipboard monitor will reset rich content as a result) */
            char *text = strdup(data.utf8().data());
            writeUTF8(text, strlen(text));
            free(text);
        }
        dataObjectOf(*this)->setText(data);
        return;
    }
    case ClipboardDataTypeImage:
    case ClipboardDataTypeUnknown:
        break;
    }

    return;
}

void Pasteboard::writePlainText(const String& text, SmartReplaceOption)
{
    D(kprintf("Pasteboard::writePlainText %s %d\n", text.utf8().data(), clipboardOf(*this)));

    if(clipboardOf(*this) == 0)
    {
        /* Copy to system clipboard first (clipboard monitor will reset rich content as a result) */
        char *data = strdup(text.utf8().data());
        writeUTF8(data, strlen(data));
        free(data);
    }

    /* Then store to internal clipboard */
    dataObjectOf(*this)->clearAll();
    dataObjectOf(*this)->setText(text);
}

void Pasteboard::write(const PasteboardURL& pasteboardURL)
{
    D(kprintf("Pasteboard::writeURL %s %d\n", pasteboardURL.url.string().utf8().data(), clipboardOf(*this)));

    ASSERT(!pasteboardURL.url.isEmpty());

    if(clipboardOf(*this) == 0)
    {
        /* Copy to system clipboard first (clipboard monitor will reset rich content as a result) */
        WTF::CString utf8 = pasteboardURL.url.string().utf8();
        const char *data = utf8.data();
        writeUTF8(data, strlen(data));
    }

    /* Then store to internal clipboard */
    dataObjectOf(*this)->clearAll();
    dataObjectOf(*this)->setURL(pasteboardURL.url, pasteboardURL.title);
}

#if 0
// broken 2.20
void Pasteboard::writePasteboard(const Pasteboard& sourcePasteboard)
{
    D(kprintf("Pasteboard::writePasteboard\n"));

    RefPtr<DataObjectMorphOS> sourceDataObject = sourcePasteboard.dataObject();
    dataObjectOf(*this)->clearAll();

    if (sourceDataObject->hasText())
        dataObjectOf(*this)->setText(sourceDataObject->text());
    if (sourceDataObject->hasMarkup())
        dataObjectOf(*this)->setMarkup(sourceDataObject->markup());
    if (sourceDataObject->hasURL())
        dataObjectOf(*this)->setURL(sourceDataObject->url(), sourceDataObject->urlLabel());
    if (sourceDataObject->hasURIList())
        dataObjectOf(*this)->setURIList(sourceDataObject->uriList());
    if (sourceDataObject->hasImage())
        dataObjectOf(*this)->setImage(sourceDataObject->image());

#warning "XXX: what to do here, exactly?"
    /*
    if (m_gtkClipboard)
        PasteboardHelper::defaultPasteboardHelper()->writeClipboardContents(m_gtkClipboard);
    */
}
#endif

void Pasteboard::clear()
{
    D(kprintf("Pasteboard::clear\n"));

    // We do not clear filenames. According to the spec: "The clearData() method
    // does not affect whether any files were included in the drag, so the types
    // attribute's list might still not be empty after calling clearData() (it would
    // still contain the "Files" string if any files were included in the drag)."
    dataObjectOf(*this)->clearAllExceptFilenames();

#warning "XXX: what to do here, exactly?"
}

void Pasteboard::clear(const String& type)
{
    D(kprintf("Pasteboard::clear(%s)\n", type.utf8().data()));

    switch (dataObjectTypeFromHTMLClipboardType(type)) {
    case ClipboardDataTypeURIList:
    case ClipboardDataTypeURL:
        dataObjectOf(*this)->clearURIList();
        break;
    case ClipboardDataTypeMarkup:
        dataObjectOf(*this)->clearMarkup();
        break;
    case ClipboardDataTypeText:
        dataObjectOf(*this)->clearText();
        break;
    case ClipboardDataTypeImage:
        dataObjectOf(*this)->clearImage();
        break;
    case ClipboardDataTypeUnknown:
        dataObjectOf(*this)->clearAll();
        break;
    }

#warning "XXX: what to do here, exactly?"
}

bool Pasteboard::canSmartReplace()
{
    return false;
}

static String getPlainText(Frame* frame);

static void setWithClipboardContents(Pasteboard *pasteboard, int clipboard)
{
    String text = getPlainText(0);
    if(clipboard == 0)
       dataObjectOf(*pasteboard)->setText(text);
}

void Pasteboard::setDragImage(DragImage, const IntPoint& )
{
    D(kprintf("Pasteboard::setDragImage()\n"));
}

static void copycollection(String &str, CollectionItem* ci, uint32 codeSet)
{
    if (ci->ci_Next)
        copycollection(str, ci->ci_Next, codeSet);

    if (CODESET_UTF8 == codeSet)
    {
        str = WTF::makeString(str, String::fromUTF8(std::span<const char>(static_cast<const char*>(ci->ci_Data), ci->ci_Size)));
    }
    else if (CODESET_LOCAL == codeSet)
    {
        char *local = (char *) malloc(ci->ci_Size + 1);

        if(local)
        {
            char *converted;

            memcpy(local, (const char *) ci->ci_Data, ci->ci_Size);
            local[ci->ci_Size] = 0;

            converted = local_to_utf8(local);

            if(converted)
            {
                str = WTF::makeString(str, String::fromUTF8(converted));
                free(converted);
            }

            free(local);
        }
    }
}

static String getPlainText(Frame* )
{
    D(kprintf("Pasteboard::plainText()\n"));

    String result;

    if (IFFHandle *ih = AllocIFF()) {
        if (ClipboardHandle *ch = OpenClipboard(PRIMARY_CLIP)) {
            ih->iff_Stream = (IPTR)ch;
            InitIFFasClip(ih);

            if (0 == OpenIFF(ih, IFFF_READ)) {
                const LONG chunks[4] = { ID_FTXT, ID_CHRS, ID_FTXT, ID_UTF8 };

                if (0 == CollectionChunks(ih, (LONG *)chunks, 2)
                 && 0 == StopOnExit(ih, ID_FTXT, ID_FORM))
                {
                    while (true)
                        if (IFFERR_EOC == ParseIFF(ih, IFFPARSE_SCAN))
                        {
                            CollectionItem *ci, *ci2;

                            // First chunk is expected to have local charset
                            ci = FindCollection(ih, ID_FTXT, ID_CHRS);

                            // Second chunk is optional, and expected to have UTF8 charset if present
                            ci2 = FindCollection(ih, ID_FTXT, ID_UTF8);

                            if (ci2)
                                copycollection(result, ci2, CODESET_UTF8);
                            else if(ci)
                                copycollection(result, ci, CODESET_LOCAL);
                        }
                        else
                            break;
                }
                CloseIFF(ih);
            }
            CloseClipboard(ch);
        }
        FreeIFF(ih);
    }

    return result;

// XXX: use system or internal
/*
    return dataObjectOf(*this)->text();
*/
}

void Pasteboard::read(PasteboardPlainText& text, PlainTextURLReadingPolicy, std::optional<size_t>)
{
    dataObjectOf(*this)->setText(getPlainText(0));
    text.text = dataObjectOf(*this)->text();
}

void Pasteboard::read(PasteboardWebContentReader&, WebContentReadingPolicy, std::optional<size_t>)
{
    notImplemented();
}

void Pasteboard::read(PasteboardFileReader& reader, std::optional<size_t>)
{
    notImplemented();
}

bool Pasteboard::hasData()
{
    D(kprintf("Pasteboard::hasData()\n"));

    //setWithClipboardContents(this, clipboardOf(*this));

    return dataObjectOf(*this)->hasText() || dataObjectOf(*this)->hasMarkup() || dataObjectOf(*this)->hasURIList() || dataObjectOf(*this)->hasImage();
}

Vector<String> Pasteboard::typesForLegacyUnsafeBindings()
{
    D(kprintf("Pasteboard::types()\n"));

    setWithClipboardContents(this, clipboardOf(*this));

    Vector<String> types;
    if (dataObjectOf(*this)->hasText()) {
        types.append(("text/plain"));
        types.append(("Text"));
        types.append(("text"));
    }

    if (dataObjectOf(*this)->hasMarkup())
        types.append(("text/html"));

    if (dataObjectOf(*this)->hasURIList()) {
        types.append(("text/uri-list"));
        types.append(("URL"));
    }

    if (dataObjectOf(*this)->hasFilenames())
        types.append(("Files"));

    return types;
}

String Pasteboard::readString(const String& type)
{
    D(kprintf("Pasteboard::readString(%s)\n", type.utf8().data()));

    setWithClipboardContents(this, clipboardOf(*this));

    switch (dataObjectTypeFromHTMLClipboardType(type)) {
    case ClipboardDataTypeURIList:
        return dataObjectOf(*this)->uriList();
    case ClipboardDataTypeURL:
        return dataObjectOf(*this)->url().string();
    case ClipboardDataTypeMarkup:
        return dataObjectOf(*this)->markup();
    case ClipboardDataTypeText:
        return dataObjectOf(*this)->text();
    case ClipboardDataTypeImage:
    case ClipboardDataTypeUnknown:
        break;
    }

    return String();
}

#if 0
// broken 2.20
Vector<String> Pasteboard::readFilenames()
{
    D(kprintf("Pasteboard::readFilenames()\n"));

    setWithClipboardContents(this, clipboardOf(*this));

    return dataObjectOf(*this)->filenames();
}
#endif

void Pasteboard::writeMarkup(const String& markup)
{
}

void Pasteboard::writeTrustworthyWebURLsPboardType(const PasteboardURL&)
{
}

void Pasteboard::write(const PasteboardImage&)
{
}

void Pasteboard::write(const PasteboardWebContent&)
{
}

void Pasteboard::write(const PasteboardBuffer&)
{
}

String Pasteboard::readOrigin()
{
    return { };
}

void Pasteboard::writeCustomData(const Vector<PasteboardCustomData>&)
{
}

Vector<String> Pasteboard::typesSafeForBindings(const String&)
{
    return { };
}

String Pasteboard::readStringInCustomData(const String&)
{
    return { };
}

void Pasteboard::write(const Color&)
{
}

Pasteboard::FileContentState Pasteboard::fileContentState()
{
    return FileContentState::NoFileOrImageData;
}

}

