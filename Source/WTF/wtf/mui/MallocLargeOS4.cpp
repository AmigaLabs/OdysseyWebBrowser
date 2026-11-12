// Large-block allocator for AmigaOS 4 (see docs/MEMORY.md, H1).
//
// clib4 malloc only returns memory to the system when a whole region is empty, so big temporary
// buffers placed between long-lived small blocks stay pinned and the process grows without bound.
// Blocks >= LargeThreshold are served straight from exec AllocVec so FreeVec gives them back at once.
// Everything else still goes to clib4 malloc.
//
// Hooked in with the linker (--wrap=malloc,calloc,realloc,free,memalign,malloc_usable_size on the Odyssey
// target), so every caller in the executable (WTF, cairo, freetype, curl, sqlite...) is covered and any
// pointer can be passed to free()/realloc(). To switch this off, remove the --wrap options.
//
// A large block is laid out as: [padding][16-byte header][user data], the header being
// {magic, magic ^ user-pointer, AllocVec base, user size}. free() recognises large blocks by that header.
// shortcut: reads 16 bytes before every freed pointer, fine for clib4 blocks (they have a header), upgrade if a
// foreign allocator's pointers are ever freed here.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <atomic>

#include <proto/exec.h>

extern "C" {
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void* __real_memalign(size_t, size_t);
void __real_free(void*);
size_t __real_malloc_usable_size(void*);
void* __wrap_malloc(size_t);
void __wrap_free(void*);
}

// Live-byte statistics for the memory probe (docs/MEMORY.md). Only compiled in with -DOWB_MEMORY_DEBUG=ON: they cost a
// malloc_usable_size() call and atomic updates on every allocation.
#if defined(OWB_MEMORY_DEBUG) && OWB_MEMORY_DEBUG
#define OWB_MEM_STATS 1
#else
#define OWB_MEM_STATS 0
#endif

namespace {

constexpr size_t LargeThreshold = 64 * 1024;
constexpr uint32_t Magic = 0x4F574C42; // "OWLB"
constexpr uint32_t PointerMask = 0xA5C3A5C3;

struct Header {
    uint32_t magic;
    uint32_t check;
    void* base;
    size_t size;
};
static_assert(sizeof(Header) == 16, "header must be 16 bytes");

#if OWB_MEM_STATS
// Buckets: <=128, <=1K, <=4K, <=16K, <=64K, <=1M, >1M.
std::atomic<size_t> s_live { 0 };
std::atomic<size_t> s_buckets[7];
std::atomic<size_t> s_largeLive { 0 };
std::atomic<size_t> s_largeCount { 0 };

inline unsigned bucketFor(size_t n) { return n <= 128 ? 0 : n <= 1024 ? 1 : n <= 4096 ? 2 : n <= 16384 ? 3 : n <= 65536 ? 4 : n <= 1048576 ? 5 : 6; }
inline void track(size_t n) { s_live.fetch_add(n, std::memory_order_relaxed); s_buckets[bucketFor(n)].fetch_add(n, std::memory_order_relaxed); }
inline void untrack(size_t n) { s_live.fetch_sub(n, std::memory_order_relaxed); s_buckets[bucketFor(n)].fetch_sub(n, std::memory_order_relaxed); }
inline size_t statSize(void* p) { return __real_malloc_usable_size(p); }
#else
inline void track(size_t) { }
inline void untrack(size_t) { }
inline size_t statSize(void*) { return 0; }
#endif

inline Header* largeHeader(void* p)
{
    auto* h = reinterpret_cast<Header*>(static_cast<char*>(p) - sizeof(Header));
    return (h->magic == Magic && h->check == (Magic ^ PointerMask ^ reinterpret_cast<uintptr_t>(p))) ? h : nullptr;
}

void* largeAlloc(size_t size, size_t alignment, bool clear)
{
    if (alignment < 16)
        alignment = 16;
    size_t total = size + alignment + sizeof(Header);
    if (total < size)
        return nullptr;
    char* base = static_cast<char*>(AllocVec(total, MEMF_SHARED | (clear ? MEMF_CLEAR : 0)));
    if (!base)
        return nullptr;
    uintptr_t p = (reinterpret_cast<uintptr_t>(base) + sizeof(Header) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    auto* h = reinterpret_cast<Header*>(p - sizeof(Header));
    h->magic = Magic;
    h->check = Magic ^ PointerMask ^ static_cast<uint32_t>(p);
    h->base = base;
    h->size = size;
    track(size);
#if OWB_MEM_STATS
    s_largeLive.fetch_add(size, std::memory_order_relaxed);
    s_largeCount.fetch_add(1, std::memory_order_relaxed);
#endif
    return reinterpret_cast<void*>(p);
}

void largeFree(Header* h)
{
    untrack(h->size);
#if OWB_MEM_STATS
    s_largeLive.fetch_sub(h->size, std::memory_order_relaxed);
    s_largeCount.fetch_sub(1, std::memory_order_relaxed);
#endif
    void* base = h->base;
    h->magic = 0; // a stale pointer can no longer be mistaken for a live large block
    FreeVec(base);
}

inline void* smallTrack(void* p)
{
    if (p)
        track(statSize(p));
    return p;
}

} // namespace

#if OWB_MEM_STATS
namespace WTF {
// Used by the memory probe (WebKitLegacy/mui/MemoryProbeMUI.cpp).
size_t fastMallocLiveBytes() { return s_live.load(std::memory_order_relaxed); }
size_t fastMallocLiveBucket(unsigned i) { return s_buckets[i].load(std::memory_order_relaxed); }
size_t fastMallocLargeBytes() { return s_largeLive.load(std::memory_order_relaxed); }
size_t fastMallocLargeCount() { return s_largeCount.load(std::memory_order_relaxed); }
}
#endif

extern "C" {

void* __wrap_malloc(size_t n)
{
    if (n >= LargeThreshold)
        return largeAlloc(n, 16, false);
    return smallTrack(__real_malloc(n));
}

void* __wrap_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
        return nullptr;
    size_t n = count * size;
    if (n >= LargeThreshold)
        return largeAlloc(n, 16, true);
    return smallTrack(__real_calloc(count, size));
}

void* __wrap_memalign(size_t alignment, size_t n)
{
    if (n >= LargeThreshold)
        return largeAlloc(n, alignment, false);
    return smallTrack(__real_memalign(alignment, n));
}

void __wrap_free(void* p)
{
    if (!p)
        return;
    if (auto* h = largeHeader(p)) {
        largeFree(h);
        return;
    }
    untrack(statSize(p));
    __real_free(p);
}

size_t __wrap_malloc_usable_size(void* p)
{
    if (!p)
        return 0;
    if (auto* h = largeHeader(p))
        return h->size;
    return __real_malloc_usable_size(p);
}

void* __wrap_realloc(void* p, size_t n)
{
    if (!p)
        return __wrap_malloc(n);
    if (!n) {
        __wrap_free(p);
        return nullptr;
    }

    auto* h = largeHeader(p);
    if (h || n >= LargeThreshold) {
        size_t oldSize = h ? h->size : __real_malloc_usable_size(p);

        if (h && n >= LargeThreshold && n <= oldSize && n >= oldSize / 2)
            return p; // shrink a little in place

        // Moving between the two allocators (or growing a large block): copy, then free the old block.
        void* q = __wrap_malloc(n);
        if (!q)
            return nullptr;
        memcpy(q, p, oldSize < n ? oldSize : n);
        __wrap_free(p);
        return q;
    }

    size_t statOld = statSize(p);
    untrack(statOld);
    void* q = __real_realloc(p, n);
    if (!q) {
        track(statOld); // p is still valid
        return nullptr;
    }
    return smallTrack(q);
}

} // extern "C"
