// Memory diagnostics, see docs/MEMORY.md. Serial output only. Compiled in with -DOWB_MEMORY_DEBUG=ON.
#include "config.h"
#include "MemoryProbeMUI.h"

#if defined(OWB_MEMORY_DEBUG) && OWB_MEMORY_DEBUG

#include <WebCore/CommonVM.h>
#include <WebCore/Document.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/MemoryCache.h>
#include <WebCore/Page.h>
#include <WebCore/WorkerOrWorkletGlobalScope.h>
#include <WebCore/WorkerOrWorkletThread.h>
#include <JavaScriptCore/Heap.h>
#include <JavaScriptCore/VM.h>
#include <wtf/RunLoop.h>
#include <wtf/Threading.h>

#include <proto/exec.h>
#include <stdlib.h>
#include <string.h>
#include <clib/debug_protos.h>

namespace WTF { size_t fastMallocLiveBytes(); size_t fastMallocLiveBucket(unsigned); size_t fastMallocLargeBytes(); size_t fastMallocLargeCount(); }

namespace WebKit {

// One-time check of what clib4 malloc gives back to the system (docs/MEMORY.md, H1).
static void allocatorSelfTest()
{
    auto freeK = [] { return (unsigned long)(AvailMem(MEMF_ANY) / 1024); };
    static volatile char sink;
    constexpr int blocks = 256;
    constexpr size_t big = 256 * 1024;
    static void* ptrs[blocks];
    static void* smalls[blocks];

    unsigned long f0 = freeK();
    char* p = (char*)malloc(64 * 1024 * 1024);
    memset(p, 1, 64 * 1024 * 1024);
    sink = p[100];
    unsigned long f1 = freeK();
    free(p);
    unsigned long f2 = freeK();
    DebugPrintF("[mem] selftest A (1x64MB): start=%luK alloc=%luK freed=%luK\n", f0, f1, f2);

    for (int i = 0; i < blocks; ++i) {
        ptrs[i] = malloc(big);
        memset(ptrs[i], 1, big);
    }
    unsigned long f3 = freeK();
    for (int i = 0; i < blocks; ++i)
        free(ptrs[i]);
    unsigned long f4 = freeK();
    DebugPrintF("[mem] selftest B (256x256KB): alloc=%luK freed=%luK\n", f3, f4);

    for (int i = 0; i < blocks; ++i) {
        ptrs[i] = malloc(big);
        memset(ptrs[i], 1, big);
        smalls[i] = malloc(1024);
        memset(smalls[i], 1, 1024);
    }
    unsigned long f5 = freeK();
    for (int i = 0; i < blocks; ++i)
        free(ptrs[i]);
    unsigned long f6 = freeK();
    DebugPrintF("[mem] selftest C (256x256KB interleaved with live 1KB): alloc=%luK big-freed=%luK\n", f5, f6);
    for (int i = 0; i < blocks; ++i)
        free(smalls[i]);
    DebugPrintF("[mem] selftest C all freed: %luK\n", freeK());
}

void memoryProbe(const char* tag)
{
    using namespace WebCore;

    unsigned docs = 0, detached = 0;
    for (auto& document : Document::allDocuments()) {
        ++docs;
        if (!document.get().frame())
            ++detached;
    }

    unsigned threads = 0;
    {
        Locker locker { Thread::allThreadsLock() };
        threads = Thread::allThreads().size();
    }

    size_t jsSize = 0, jsCap = 0, jsExtra = 0;
    if (auto* vm = commonVMOrNull()) {
        jsSize = vm->heap.size();
        jsCap = vm->heap.capacity();
        jsExtra = vm->heap.extraMemorySize();
    }

    // Worker threads each have their own JS VM (racy read, diagnostics only).
    unsigned workers = 0;
    size_t wSize = 0, wCap = 0, wExtra = 0;
    WorkerOrWorkletThread::workerOrWorkletThreads().forEach([&](auto& t) {
        if (auto* gs = t.globalScope()) {
            auto& heap = gs->vm().heap;
            ++workers;
            wSize += heap.size();
            wCap += heap.capacity();
            wExtra += heap.extraMemorySize();
        }
    });

    DebugPrintF("[mem] %s: free=%luK largest=%luK wk=%luK pages=%u docs=%u detached=%u threads=%u js=%luK/%luK extra=%luK cache=%uK\n",
        tag,
        (unsigned long)(AvailMem(MEMF_ANY) / 1024), (unsigned long)(AvailMem(MEMF_LARGEST) / 1024), (unsigned long)(WTF::fastMallocLiveBytes() / 1024),
        Page::nonUtilityPageCount(), docs, detached, threads,
        (unsigned long)(jsSize / 1024), (unsigned long)(jsCap / 1024), (unsigned long)(jsExtra / 1024),
        MemoryCache::singleton().size() / 1024);
    DebugPrintF("[mem]   large=%luK/%lu workers=%u wjs=%luK/%luK wextra=%luK sizes(K)<=128:%lu <=1K:%lu <=4K:%lu <=16K:%lu <=64K:%lu <=1M:%lu >1M:%lu\n",
        (unsigned long)(WTF::fastMallocLargeBytes() / 1024), (unsigned long)WTF::fastMallocLargeCount(),
        workers, (unsigned long)(wSize / 1024), (unsigned long)(wCap / 1024), (unsigned long)(wExtra / 1024),
        (unsigned long)(WTF::fastMallocLiveBucket(0) / 1024), (unsigned long)(WTF::fastMallocLiveBucket(1) / 1024),
        (unsigned long)(WTF::fastMallocLiveBucket(2) / 1024), (unsigned long)(WTF::fastMallocLiveBucket(3) / 1024),
        (unsigned long)(WTF::fastMallocLiveBucket(4) / 1024), (unsigned long)(WTF::fastMallocLiveBucket(5) / 1024),
        (unsigned long)(WTF::fastMallocLiveBucket(6) / 1024));
}

void memoryProbeStart()
{
    static RunLoop::Timer* timer = new RunLoop::Timer(RunLoop::main(), [] { memoryProbe("tick"); });
    static bool selfTestDone = false;
    static RunLoop::Timer* once = new RunLoop::Timer(RunLoop::main(), [] {
        if (!selfTestDone) {
            selfTestDone = true;
            allocatorSelfTest();
        }
    });
    once->startOneShot(5_s);
    timer->startRepeating(10_s);
}

} // namespace WebKit

#endif // OWB_MEMORY_DEBUG
