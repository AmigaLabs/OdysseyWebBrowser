# Notes: tab memory leak and diagnostics (AmigaOS 4 / MUI port)

Status: investigation paused. Decision: keep the current WebKit base (2.34 port).
Plan: upgrade to newer WebKit in a future, separate project.

## 1. Problem

Memory used by the browser is not returned after a tab is closed.
Free memory keeps dropping with each closed tab (about 1.37 GB -> 1.27 GB free
over several tests).

## 2. What was checked

- `WebView::~WebView()` already calls `requestMemoryRelease()`, which runs
  `WebCore::releaseMemory(Critical::Yes, Synchronous::Yes)`. The default
  arguments already prune the back/forward cache and the memory cache, so
  cached pages are not the cause.
- On AmigaOS, WebKit's allocators (FastMalloc and bmalloc) use the C library's
  `memalign`/`free`. `releaseFastMallocFreeMemory()` only scavenges bmalloc
  chunks. clib4 has `malloc_dump` but no `malloc_trim`, so freed blocks may be
  kept by the C library. This explains some of the memory, but not the
  documents below.
- The `Document` and `DOMWindow` objects of a closed tab are never freed.

## 3. Findings (test results)

Test pages (created on the Amiga in `RAM Disk:`; copies are in `assets/`):
- `static.html`: plain HTML, no scripts.
- `script.html`: same, plus a `setInterval` timer.

Diagnostic output per tab close:
- A closed page's document stays alive with `page=0x0` after the close, for
  both `static.html` and `script.html`, and for `topsites://` and `https://`
  pages too. So this is not specific to scripts.
- Document count grows with each closed tab (3 -> 5 -> 7 -> 9 -> 11 in one run).
- Free memory does not come back after the close.
- After the release, a closed document has `refs=1`, and its `DOMWindow` has
  `winrefs=2`. One window reference is the document, the other is likely the
  JavaScript wrapper of the window.
- The main frame is freed when the page is deleted: its count is 2 before the
  delete and 1 after (our temporary reference only). So the frame is not the
  holder.
- Calling `stopAllLoaders()` and `stopActiveDOMObjects()` before the delete did
  not free the documents.
- Calling `willDetachPage()` and `detachFromPage()` before the delete did not
  change the counts either. These calls were removed again.
- WebCore expects `Document::detachFromFrame()` before a frame is destroyed
  (`Document::frameDestroyed()` asserts it). Our teardown does not do that
  detach, and calling `detachFromFrame()` directly is risky (it has a
  `RELEASE_ASSERT`). This is the most likely area to look at next.

Crash-related findings (fixed, kept in the code):
- Opening the cookie window during a page load could crash. The cookie database
  (`CookieJarDB`) was used from more than one thread without a lock. A lock was
  added, plus a 5 second busy timeout.
- The cookie window kept a reference to the default network session, which
  shutdown frees. The window now fetches the session on each use, and it is
  closed before the sessions are destroyed at shutdown.
- Session cookies have no expiry, and the window called `expires.value()` on
  them. Now it uses `value_or(0)`.
- Local storage (`StorageAreaSync`, thread "LocalStorage") was also crashing in
  SQLite while a page loaded. Disabling local storage stopped that crash.

## 4. Current code state

Kept (intended changes):
- `Source/WebCore/platform/network/curl/CookieJarDB.{h,cpp}`: recursive lock on
  all public methods, and `setBusyTimeout(5000)` when opening.
- `Source/WebKitLegacy/mui/UI/cookiemanagergroupclass.cpp`: per-use session
  fetch and `value_or(0)`.
- `Source/WebKitLegacy/mui/UI/appclass.cpp`: close the cookie window before
  `destroyAllSessions()` in dispose.

Kept but not effective (may be removed later):
- `Source/WebKitLegacy/mui/WebView.cpp` `close()`: `stopAllLoaders()` and
  `stopActiveDOMObjects()` before `delete m_page`. Did not free the documents.

Reverted:
- All `[MemDiag]` diagnostics (see section 5).
- Temporary `[CookieDB]`, `[CookieWin]`, `[OWBApp]`, `[main]` serial logs.

## 5. Diagnostics that were added and then removed

To recreate them, the following was used:
- `DebugPrintF` (AmigaOS 4 SDK), with `#include <proto/exec.h>`.
- `WebView.cpp`: a helper `logWebCoreObjects(const char*)` that printed, for
  each `WebCore::Document::allDocuments()` entry, its address, `refCount()`,
  `page()`, `domWindow()` and its `refCount()`, the URL, plus the page count
  and `AvailMem(MEMF_PUBLIC/MEMF_LARGEST)`. It was called at the start of
  `WebView::~WebView()`, before and after `requestMemoryRelease()`.
- `WebView.cpp` `close()`: a `Ref<WebCore::Frame>` held across `delete m_page`,
  with the frame `refCount()` printed before and after the delete.
- Needed includes for the above: `WebCore/Document.h`, `WebCore/DOMWindow.h`,
  `WebCore/Frame.h`, `WebCore/Page.h`.
- Build note: `DOMWindow` must be a complete type (include `WebCore/DOMWindow.h`)
  to call `domWindow()->refCount()`.

## 6. Next steps (future)

1. Plan a WebKit upgrade as a separate project. The MUI layer
   (`Source/WebKitLegacy/mui`) must be ported as part of it.
2. When starting the upgrade, retest the leak first on a newer base, using the
   static/script test pages above, and compare the document counts.
3. Before the upgrade, a possible local fix is to call the proper
   `Document::detachFromFrame()` path during close. Check the WebCore code path
   for `Document::frameDestroyed()` and `FrameLoader` teardown first.
4. Decide whether to keep the `stopAllLoaders()`/`stopActiveDOMObjects()` block
   in `WebView::close()`.
