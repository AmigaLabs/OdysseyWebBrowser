#pragma once
// Memory diagnostics on serial (docs/MEMORY.md). Compiled in only with -DOWB_MEMORY_DEBUG=ON, otherwise no-ops.
namespace WebKit {
#if defined(OWB_MEMORY_DEBUG) && OWB_MEMORY_DEBUG
void memoryProbeStart();               // starts the periodic serial report (10 s)
void memoryProbe(const char* tag);     // one report now
#else
inline void memoryProbeStart() { }
inline void memoryProbe(const char*) { }
#endif
}
