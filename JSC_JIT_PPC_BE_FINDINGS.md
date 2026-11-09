# JavaScriptCore JIT on AmigaOS 4 / PowerPC Big Endian – Findings

Analysis only. No code was changed. Tree: WebKit 2.48-based (`Source/JavaScriptCore`).

## 1. Current state

- AmigaOS4 build (`Source/cmake/amigaos.cmake`, `-DPORT=MUI`) → `build/cmakeconfig.h` has
  `ENABLE_C_LOOP 1`, `ENABLE_JIT 0`, `ENABLE_DFG_JIT 0`, `ENABLE_FTL_JIT 0`, `ENABLE_WEBASSEMBLY_*JIT 0`.
  So JS runs on the **C-Loop interpreter** (LLInt compiled as C++). No machine code is generated.
- `Source/cmake/OptionsMUI.cmake` (lines 13-21): JIT ON only for `WTF_CPU_X86_64`; everything else → C_LOOP ON.
- `PlatformEnable.h` (~630-660): JIT default only on X86_64, ARM64, and ARMv7-Thumb2-Linux. All other 32-bit → forced `ENABLE_JIT 0`; C_LOOP forced 1.
- PPC32 is `CPU(PPC)`, **32-bit register → `USE(JSVALUE32_64)`**, **`CPU(BIG_ENDIAN)`**, `OS(AMIGAOS)`.
- WebKit upstream has **no PowerPC backend** (old ppc/ppc64 JSC JIT was removed years ago). Verified: no `*PPC*` file in `assembler/`, `offlineasm/`; only references to PPC are in `MachineContext.h`, `PlatformRegisters.h`, `PageBlock.h`, `ThreadingPOSIX.cpp` (register/context access).
- The only 32-bit JIT that exists upstream is **ARMv7/Thumb2** (`ARMv7Assembler.h` 3.4k lines + `MacroAssemblerARMv7.h` 3.7k lines). There is also a MIPS remnant in `PlatformEnable.h` but no MIPS assembler in tree.

**Conclusion:** "making the JIT work" = writing a **new PPC32 BE backend** for JSC. Not a config switch. It is a large project.

## 2. What exists that can be reused

| Piece | Where | Reuse for PPC32 |
|---|---|---|
| JSVALUE32_64 baseline JIT | `jit/JIT*32_64.cpp`, `CallFrameShuffler32_64.cpp` | Yes, written against MacroAssembler, ISA-independent |
| JSVALUE32_64 LLInt | `llint/LowLevelInterpreter32_64.asm` | Yes, needs a PPC offlineasm backend |
| Big-endian support in JSValue layout | `runtime/JSCJSValue.h:102` `CPU(BIG_ENDIAN)`, `LLIntOfflineAsmConfig.h:173` `OFFLINE_ASM_BIG_ENDIAN`, `yarr/YarrJIT.cpp:1987-2021`, `InstructionStream.h:262` | Partly – BE is handled in generic code; was exercised historically by MIPS/PPC, **never tested in recent upstream** → expect bugs |
| C_LOOP | already working | Fallback, keep |

## 3. Work items (in dependency order)

### 3.1 Assembler layer (`Source/JavaScriptCore/assembler/`) – NEW
1. `PPCAssembler.h` (name e.g. `PPC32Assembler`): instruction encoders (always 4-byte, fixed width, **BE** encoding: write words as host-order uint32, since host is BE no swap, but ensure the `AssemblerBuffer` writes `uint32_t` not byte-by-byte LE-style).
   - Branch ranges: `b` ±32MB, `bc` ±32KB → need **branch islands / long-jump** (`lis/ori/mtctr/bctr`) like ARMv7 `ENABLE_JUMP_ISLANDS`. Conditional branches need inversion + unconditional branch when out of range.
   - Immediate load: `lis+ori` (32-bit) – patchable pair; repatching must modify both words (`ARMv7` movw/movt equivalent).
   - Condition register model (CR0-CR7, LT/GT/EQ/SO) – map to `MacroAssembler::RelationalCondition/ResultCondition/DoubleCondition`. Unordered FP compare needs `cror`/CR bit tricks. Carry-based `Overflow` conditions need `addo.`/XER-SO (`mcrxr`) handling.
2. `MacroAssemblerPPC.h/.cpp`: implement the ~full MacroAssembler API surface used by baseline JIT + Yarr + thunks (loads/stores with indexed addressing, `Call`/`Jump`/`DataLabel32`/`PatchableJump`, `farJump`, `far call`, `patchableBranch*`, `moveWithPatch`, `repatch*`, `linkCall`, `linkJump`, `relinkJumpToNop`, etc.).
   - Wide-int ops on 32-bit: `add64/sub64/mul64`-style helpers via `addc/adde`, `JSVALUE32_64` tag/payload pairs. Big-endian: **tag at lower address on BE** (check `PayloadOffset/TagOffset` in `JSCJSValue.h` & `LowLevelInterpreter32_64.asm`; `OFFLINE_ASM_BIG_ENDIAN`).
   - FPU: PPC FPRs are 64-bit double; no native 32→int conversion with exact semantics → `fctiwz` + stack store/reload; `truncateDoubleToInt32` needs care (JS ToInt32 modulo semantics). No FP unit denormal assumptions: check FPSCR / flush-to-zero.
   - Float ↔ int transfer needs memory round trip (no `mtfprd` on G3/G4/classic 750/74xx/ 440/460 – AmigaOS4 targets: **e500/G4/PA6T/440/460/P50xx**, need to know target).
   - Atomics: `lwarx/stwcx.` + `sync/lwsync/isync` for `MacroAssembler::*Atomic*`, `memoryFence`. 64-bit atomics unavailable on 32-bit PPC (needs lock fallback or disabled).
3. `PPCRegisters.h`: define `RegisterID` (r0-r31) with ABI roles; r0 reads as 0 in some address forms (`addi rD,r0,x`); r1 = SP, r2 = TOC/reserved (SysV ABI on AmigaOS4 uses r2 as small-data?? – **confirm ABI**), r13 = small data (`-msdata`), r11/r12 scratch, r3-r10 args, r14-r31 callee saved.
   - Choose `callFrameRegister`, `GPRInfo` (jit/GPRInfo.h) and `FPRInfo.h` additions: `argumentGPR*`, `returnValueGPR/GPR2`, `regT0..`, `nonPreservedNonArgumentGPR`, `reservedForLR`, `patchableJumpScratch`, `fpRegT0...`.
4. `LinkBuffer`/`AssemblerCommon`/`CPU.h`/`MacroAssembler.h`/`AbstractMacroAssembler.h`/`MacroAssemblerCodeRef.h`: add `CPU(PPC)` branches (`#if CPU(PPC) #include "MacroAssemblerPPC.h"`), `CPU::isPPC`, instruction size constants, `MacroAssembler::maxJumpReplacementSize`, `patchableJumpSize`, `repatchCall` etc.
5. Disassembler (optional) – `ENABLE_DISASSEMBLER` via `USE(CAPSTONE)`, only if Capstone is ported. Strongly recommended for debugging; otherwise bring-up is blind.

### 3.2 Calling convention / frame layout
- AmigaOS4 uses **SysV PPC EABI** (PowerOpen/AIX-style TOC not used). Verify exactly: stack alignment (16 on SysV; check AmigaOS4/clib4 `-mcrt=clib4` ABI), link register save slot (`SP+4`), red zone (none on SysV/EABI), callee-saved set, vararg handling, `r2`/`r13` small-data usage (`-msdata` flags in toolchain, `-mlongcall` already used here).
- JSC's `JITOperation` calls use C ABI with up to **8 GPR args (r3-r10)** and 64-bit values passed in aligned register pairs (e.g. `r3:r4`, odd register skipped!). JSVALUE32_64 passes EncodedJSValue (64-bit) as pair → alignment rule for pairs **must be encoded in `CCallHelpers` (`setupArguments` with `JSVALUE32_64` pair alignment)** and `GPRInfo::argumentGPR*`. ARMv7 has equivalent logic; PPC (even/odd pair, starts at odd reg r3,r5,r7,r9) differs from ARM EABI (even/odd also, r0-r1, r2-r3). Return: 64-bit in `r3:r4` (hi:lo on BE = tag:payload order: confirm vs `EncodedJSValue` layout).
- `jit/CCallHelpers.h`, `JITOperations`, `ThunkGenerators`, `JITThunks`, `AssemblyHelpers` (`emitFunctionPrologue/Epilogue`, `tagReturnAddress` no-op, `preserveReturnAddressAfterCall` from LR), `CallFrame` `returnPC` slot semantic with LR (`mflr/mtlr`).
- Exception unwinding: JSC does **not** use C++ unwind through JIT frames (uses `ExceptionHandler` thunk & `vm.callFrameForCatch` + restoring SP) – good, no `.eh_frame` for JIT code. But `JIT_OPERATION` helper functions must not unwind through JIT frames.

### 3.3 offlineasm backend (LLInt in assembly) – optional but recommended
- Needed if you want LLInt asm interpreter (much faster than C_LOOP) and it is **required** for JIT tier-up bootstrap (LLInt→Baseline entry thunks, `llint_entry`, `LLIntThunks`).
  - Without LLInt-asm, the JIT can still be used with `ENABLE(C_LOOP)`? **No**: `C_LOOP` and `JIT` are mutually exclusive in practice (`PlatformEnable.h` forces `C_LOOP=0` when JIT). Interpreter tiers are tied together with JIT via LLInt asm for non-C_LOOP builds. So the PPC offlineasm backend is **mandatory** for JIT builds.
- Add `offlineasm/ppc.rb` (model on `arm.rb` / `risc.rb`, 32-bit): register map (`t0..t5, a0..a3, r0..r1, cfr, sp, lr, csr*`), instruction lowering via `risc.rb` (`riscLowerMisplacedAddresses`, immediates >16 bit, etc.), `lowerPPC`, `PPC`-specific: `lis/ori`, `cmpw/bc` using CR0, `rlwinm`, `addze`, `mullw/mulhw`, `divwu`, `lwz/stw/lbz/lhz/lfd/stfd`, **BE-aware** field offset helpers for tag/payload in `.asm` (`TagOffset`, `PayloadOffset` handled by `OFFLINE_ASM_BIG_ENDIAN` in `LowLevelInterpreter32_64.asm`).
- Register in `offlineasm/backends.rb` `BACKENDS` / `WORKING_BACKENDS`, `config.rb`, `settings.rb`, `LLIntOfflineAsmConfig.h` (`OFFLINE_ASM_PPC`, `OFFLINE_ASM_PPC_JIT`?), `LowLevelInterpreter.asm` `macro` blocks guarded by `ARMv7`, `ARM64`… need `PPC` branches (`checkStackPointerAlignment`, `callSlowPath`, `cCall2` with 64-bit pairs, `pushCalleeSaves`, `jumpToException`, `vmEntryRecord`, `doVMEntry`).
- Note: offlineasm output is GNU `as` syntax; the AmigaOS cross GCC (`ppc-amigaos-gcc` 11?) assembler syntax is compatible with standard GNU PPC as. Verify `-mcpu` target, e.g. e500 SPE ≠ classic FPU.
- Needs **Ruby** in the cross build host (already required by JSC build: check CMake `find_package(Ruby)`).

### 3.4 Platform enable/config
- `WTF/wtf/PlatformEnable.h`: add `CPU(PPC) && OS(AMIGAOS)` to JIT default and to `ENABLE_DFG_JIT` (later; start with baseline only: `ENABLE_JIT 1`, `ENABLE_DFG_JIT 0`, `ENABLE_FTL_JIT 0`, `ENABLE_YARR_JIT` follows JIT). Remove from the "Disable JIT on all other 32bit architectures" block. Adjust `ENABLE_C_LOOP`.
- `ENABLE_JUMP_ISLANDS` for PPC (branch range).
- `Source/cmake/WebKitFeatures.cmake` (80-95) and `OptionsMUI.cmake` (13-21): add `WTF_CPU_PPC` branch. Also `Source/cmake/WebKitCommon.cmake`/`WebKitCompilerFlags.cmake` offlineasm arch detection (`JavaScriptCore/CMakeLists.txt` ~227, `offlineasm` arch list); `OFFLINEASM_ARCH` mapping for ppc.
- `JSC::Options`: JIT-related options and `useJIT` (`runtime/Options.cpp:769`). Set safe defaults (`useBaselineJIT`, `useDFGJIT=false`, `useFTLJIT=false`, `useWebAssembly=false`, `useJITCage=false`).
- `ENABLE_ASSEMBLER` must be true (`ENABLE(JIT) || ENABLE(YARR_JIT) ...`).
- Check `USE_SYSTEM_MALLOC ON` (OptionsMUI) and bmalloc not needed for JIT; `ExecutableAllocator` must still work with system malloc (see 3.5).

### 3.5 Executable memory (**AmigaOS-specific, high risk**)
- `WTF/wtf/posix/OSAllocatorPOSIX.cpp` currently does `malloc(bytes)` for `OS(AMIGAOS)` (lines ~60, 214, 270, 281): **no mmap, no protection control, no exec flag**. JIT needs memory that is executable.
  - AmigaOS4 has no W^X enforcement on classic hardware (no NX); `AllocVecTags` with `AVT_Type, MEMF_SHARED` memory is executable. But must **verify** with the exact CPU/MMU/OS version (OS 4.1 FE etc.). Memory protection granularity exists via `SetMemoryAttrs`/MMU attributes? Confirm.
  - `ExecutableAllocator.cpp` (fixed VM pool `ENABLE(JUMP_ISLANDS)`, `useJITCage`, `mprotect` in lines ~700-745 for `ENABLE(SEPARATED_WX_HEAP)`) → ensure these paths are off: use `JIT_FIXED_VM_POOL`/`ExecutableMemoryHandle` with plain malloc'd pool; keep one contiguous pool (reserve e.g. 32-64MB; **branches within ±32MB** keep calls cheap).
  - Must allocate pool with alignment (page/32B cache line) — plain `malloc` may not give that; use `memalign`/`posix_memalign` from clib4 or `AllocVecTags` aligned.
- **Instruction cache coherency** (critical on PPC, split I/D caches, no snoop for icache): after writing code and every repatch, need `dcbst` / `sync` / `icbi` / `sync` / `isync` over the range (cache-line size 32B on G3/G4/e500, 128B on G5/PA6T? PA6T = 64B; query at runtime). `MacroAssemblerCodeRef`/`ExecutableAllocator` → `cacheFlush(void*, size_t)` in `assembler/AssemblerCommon.h`/`MacroAssemblerPPC.h`. AmigaOS4: use `IExec->CacheClearE(addr, len, CACRF_ClearI|CACRF_ClearD)` (exec.library) – preferred over hand-rolled asm. Must be called in `LinkBuffer::performFinalization` and every `repatch*`.
- Repatching atomically while other threads run (concurrent JIT, `ENABLE_CONCURRENT_JS`) – PPC cannot patch multiple instructions atomically; JSC patches only from main thread at safepoints, but `lis/ori` pairs patched with cache flush must be done before the code runs. Check `ENABLE_CONCURRENT_JS` is OK. Consider disabling concurrent JIT: `useConcurrentJIT=false`.
- `PageBlock.h` / `MachineContext.h` already know PPC register layout, but AmigaOS4 `ucontext`/signals differ: **signal-based stack overflow / Wasm trap / `VMTraps` / SamplingProfiler** need thread suspend + register capture (`MachineThreads`, `ThreadingPOSIX.cpp` `suspend/resume`). On AmigaOS4 with `-athread=native`... verify how thread suspend/register capture is done today for GC conservative stack scanning: **GC scans machine stack and registers (setjmp-style, `callee-save registers`)**. With JIT the GC must scan registers of JIT code frames: r14-r31 callee-saved must be spilled onto stack (`setjmp`/`getcontext` fallback, `MachineThreads::tryCopyOtherThreadStack` requires `pthread_kill`/signals – on clib4 may not exist; C_LOOP builds avoid this for other threads only partially). Verify how the current build handles `Thread::suspend` (not supported → single mutator thread assumption?).
- `VMTraps` (JIT watchpoint / termination) uses signals (`SIGUSR`) → likely unsupported; keep `useJIT` working with `VMTraps` disabled or polled-only (`ENABLE(SIGNAL_BASED_VM_TRAPS)` should be 0; non-signal traps polling is slower but fine).
- Fixed-address / `MAP_JIT`/ `pthread_jit_write_protect_np`: N/A.

### 3.6 Alignment & misc CPU quirks
- Build flag `-mstrict-align` is already on. JIT code must obey the same: unaligned `lwz/stw` OK on 750/74xx within cache line but traps/slow on e500/440; 64-bit `lfd/stfd` must be 8-byte aligned or split. JSVALUE32_64 `EncodedJSValue` is 8-byte aligned in memory normally; `JSValue` in bytecode registers (`Register` 8 bytes) aligned. Yarr JIT accesses 16-bit chars unaligned → use byte loads or check alignment.
- Bytecode stream: `InstructionStream.h:262-275` BE special case for narrow opcodes (offlineasm reads `.q`/`.h` and expects LE-style ordering). On BE, offlineasm `loadb/loadh` of the `Opcode` operand offsets need `OFFLINE_ASM_BIG_ENDIAN` adjustments – already present in generator (`BytecodeStructs`/`generate_bytecode`); **verify** `Generator`'s `Argument` offset for `uint8_t` narrow operands (they are placed at "end of word" on BE – see comment). Highest-risk area in LLInt on BE; write a test that runs C_LOOP vs asm results.
- `ENABLE_FTL_JIT` off (32-bit unsupported). `DFG` on JSVALUE32_64 + PPC: possible after baseline works, needs `DFGSpeculativeJIT32_64.cpp` plus many ABI constants (`DFGOSRExit` register save/restore (`ARMv7: 16 FPRs`), `ThunkGenerators` OSR exit, `AssemblyHelpers`). **Phase 2**; estimated weeks.
- WebAssembly: keep **off** (`ENABLE_WEBASSEMBLY` requires 64-bit pointers; BBQ/OMG are 64-bit only).
- `JSC::Options` JSC `useJIT` runtime-guard: running on machines without needed instructions (e.g. `fsqrt` missing on some 603/e500 cores: use `fsqrt` check/fallback; AltiVec not required).
- FP: `-mcpu=e500`/SPE builds have no classic FPR → backend impossible. Need target list.
- 64-bit integer ops on JSVALUE32_64 (`JSValue::asInt64`, `Int52`: `Int52` disabled on 32-bit already).
- `double` representation in `JSVALUE32_64`: tag `0xfffffff8`-style `EmptyValueTag...`; NaN canonicalization `PNaN`: BE-safe? (constants in `JSCJSValue.h` use layout union – check `bitwise_cast<double>`).

### 3.7 Tooling / bring-up
- Need a way to **run and debug** generated code: emulator (QEMU `qemu-system-ppc` w/ AmigaOS4 on Sam460/PegasosII?, or QEMU user-mode on Linux/PPC BE for first bring-up – much easier: write backend against **ppc32 Linux BE (e.g. in QEMU user mode `qemu-ppc`)** using standard SysV ABI, then port the OS bits). Strongly recommended order.
- `testmasm` (`assembler/testmasm.cpp`, `JSC_TESTMASM`) and `testb3`-like harness to unit test the macro assembler. Add PPC cases.
- JSC `--dumpDisassembly` needs disassembler (see 3.1.5).
- `jsc` shell available for AmigaOS (`JavaScriptCore/shell`), plus `Source/JavaScriptCore/tools`, JSTests/stress tests not in this repo (get from upstream) – needed for validation.
- Debug flags: `--useJIT=false`, `--useLLInt=false`, `--forceJITThreshold`, `--useBaselineJIT=true --useDFGJIT=false` etc.

## 4. Suggested phases

1. **Phase 0 (cheap, can do now):** Keep C_LOOP; make sure BE-related bugs are not hiding there: run JS conformance (test262 / `jsc` stress) on C_LOOP to validate BE core correctness (`JSValue` layout, typed arrays, LiteralParser, InstructionStream narrow encoding). Optionally tune C_LOOP (`-O2/-O3`, `computed goto` `ENABLE_COMPUTED_GOTO_OPCODES` – GCC supports it; check it is enabled for C_LOOP) – large speedup at ~zero effort.
2. **Phase 1:** Regexp-only JIT? Yarr JIT depends on `ENABLE_JIT`/MacroAssembler, so needs the PPC assembler (3.1, 3.2 part, 3.5) but **not** offlineasm/LLInt-asm/baseline. Hmm – `ENABLE_YARR_JIT` is gated `ENABLE(JIT)` in this tree (`PlatformEnable.h:814`) → would need decoupling (allowed upstream: Yarr JIT can run with C_LOOP on x86/ARM… upstream supports `ENABLE_YARR_JIT` with `ENABLE_C_LOOP`? Check). Good first milestone: smallest assembler surface, easy to test, good perf win on regex-heavy pages.
3. **Phase 2:** offlineasm PPC backend + LLInt asm (no JIT yet): `ENABLE_JIT=0`, C_LOOP=0 (JSC supports LLInt-asm with `ENABLE_JIT=0`? upstream only for ARM64/X86 w/ `useJIT=false`; requires `ENABLE(ASSEMBLER)`).
4. **Phase 3:** Baseline JIT (`jit/JIT*32_64.cpp`).
5. **Phase 4 (optional):** DFG 32_64. FTL/B3 not possible (32-bit).

## 5. Effort / risk (rough)

| Item | Size | Risk |
|---|---|---|
| PPC assembler + MacroAssembler | ~6-10k lines | high (CR/overflow/FP semantics, patching) |
| offlineasm backend + LLInt tuning | ~3k lines Ruby + asm fixes | high (BE narrow operands, ABI) |
| Baseline JIT bring-up | many bug-fix cycles | high |
| OS/memory/icache glue | small | **unknown on AmigaOS4** |
| DFG | large | very high |

Realistically: months of work for a single developer for baseline JIT; Yarr-only is the best value/effort.

## 6. Risks to know
- Upstream drops 32-bit paths progressively; this 2.48 tree's JSVALUE32_64 JIT is only CI-tested on ARMv7 Linux. Rebase burden on every WebKit update is permanent for an out-of-tree PPC backend.
- BE + 32_64 combination is untested upstream today.
- Without executable-memory and icache control confirmed on AmigaOS4, nothing else matters. Verify this first.
- Cross-compiler (`ppc-amigaos-gcc`) version must support C++20 (WebKit 2.48 needs it; Makefile flags show `-std=c++14` which looks stale/wrong for this tree – outside JIT scope but check).

## 7. Answers received / decisions
- Target CPUs: G3, G4 (AltiVec optional), e500-non-SPE (i.e. classic FPU: PPC440/460, e300/e500v-FPU parts such as A1222's *not* SPE P1022 is excluded), PA6T (X1000). **Excluded: G5 (PPC970/64-bit), P1022 SPE.** => baseline ISA = **PPC32 with hardware FPU (FPRs, `fadd/fmul/fsqrt?`), no 64-bit GPR, no AltiVec dependency**. Toolchain default confirmed: `-mhard-float` on, `-maltivec` off, `-mpowerpc64` off, `__SIZEOF_LONG__ 4`.
- Test environment: emulated AmigaOne and Pegasos 2 (QEMU-class emulation: G3/G4 style). Implications in 9.
- Goal: faster JS on websites via JIT.
- Compiler: `ppc-amigaos-gcc (adtools build 11.5.0)`. `-std=c++20` accepted (`__cplusplus 202002L`). NOTE `Source/cmake/OptionsCommon.cmake:1` already sets `CMAKE_CXX_STANDARD 23` and the toolchain adds `-std=c++14`; the CMake-added flag is normally appended later so the effective standard depends on ordering – check `build/**/flags.make` to see the real `-std=` used. WebKit 2.48 officially needs GCC >= 13 (libstdc++ C++23 features). With GCC 11 expect missing features (`std::expected`, `<ranges>` gaps, `std::format`, `constexpr` gaps); WTF has fallbacks for some. Since the tree currently builds with it, it works in practice, but JIT code uses more `concepts/requires`, `std::bit_cast`-like (`bitwise_cast`), `std::span` (WTF-provided) – verify when you get there.
- Signals: `clib4/include/signal.h` has `sigaction`, `SA_SIGINFO`, `SIGUSR1/2`, `pthread_kill`; `ucontext.h` has `mcontext_t.gregs[]` with NIP at index 32 (PPC Linux-like layout). **But** `ThreadingPOSIX.cpp` has `Thread::suspend/resume` compiled out for `OS(AMIGAOS)` (`#elif !OS(MORPHOS) && !OS(AMIGAOS)` at lines ~379/490/514/541), and `PlatformRegisters.h:42` includes nothing for AMIGAOS. Signal *delivery to a pthread target* needs to be tested on real clib4 (whether handlers run on the target thread and whether `ucontext` is real). See 8.3.

## 8. Findings from the SDK (`/opt/ppc-amigaos/ppc-amigaos/SDK`)

### 8.1 Executable memory – feasible (good news)
- `exec/memory.h:126`: `#define MEMF_EXECUTABLE (1UL<<4)` – "memory that contains executable code… never swapped… basically defines a new memory type".
- `exec/exectags.h` / `AllocVecTags`: `AVT_Type` accepts `MEMF_EXECUTABLE` (autodoc `exec.doc` ~1782: "Allocate memory that is marked executable. *No other value or combination allowed*" for `AVT_Type`; also `MEMF_SHARED`/`MEMF_PRIVATE` are *types*, so a single type is chosen). Also autodoc warns **"ALLOCATING MEMF_EXECUTABLE MIGHT BREAK A FORBID"** → never allocate while in Forbid/Disable.
- Extra: `AVT_Alignment` (alignment), `AVT_Contiguous`, `AVT_Clear`. Use `AllocVecTags(size, AVT_Type, MEMF_EXECUTABLE, AVT_Alignment, 4096?, AVT_ClearWithValue, 0, TAG_DONE)`. Free with `FreeVec`.
- MMU API (`interfaces/exec.h:310`, `struct MMUIFace`): `SetMemoryAttrs(addr, len, attrib)` with `MEMATTRF_EXECUTE (1<<9)`, `MEMATTRF_READ_WRITE`, `MEMATTRF_READ_ONLY`, `MEMATTRF_COHERENT`, `MEMATTRF_GUARDED`, `MEMATTRF_CACHEINHIBIT`, `MEMATTRF_WRITETHROUGH`. This allows W^X-style toggling per page if wanted, but not needed: allocate **RWX once** via `MEMF_EXECUTABLE` and skip protection toggling (`ENABLE_SEPARATED_WX_HEAP` off).
- clib4 `sys/mman.h` has `mmap/mprotect/PROT_EXEC` declared, but **its behaviour is unverified** (clib4.library 1.2M binary only; no source here). Do not rely on it. Recommended: a dedicated executable pool allocator in WTF (replace the `malloc`/`fastMalloc`-based `OSAllocatorAmigaOS.cpp`) that calls `IExec->AllocVecTags(..., AVT_Type, MEMF_EXECUTABLE, ...)` when `executable==true`.
- **Where to hook:** `Source/WTF/wtf/OSAllocatorAmigaOS.cpp` (included via `PlatformMUI.cmake:23`; the POSIX version's `#if OS(AMIGAOS) return malloc` branches are not the active implementation for this port). Today its `reserveAndCommit` just `fastMalloc` + `memset` and ignores `executable`. Note that `ExecutableAllocator` (`jit/ExecutableAllocator.cpp`) calls `OSAllocator::reserveUncommitted(..., executable=true, jitCage)` for the fixed pool ("FixedVMPoolExecutableAllocator") – must return **one contiguous block**, ideally **32 MB or less so PPC `b` (±32 MB) reaches everywhere**; 16-32 MB is a sensible default for a 32-bit OS with limited RAM (Pegasos 2 often 512MB–2GB). Allocation up front with `MEMF_EXECUTABLE` may commit physical RAM immediately (no overcommit) → make size configurable (`JSC_JITMemoryPoolSize` / `Options::jitMemoryReservationSize`).
- `decommit`/`commit` are no-ops (no partial release) → fine.
- Open: does `MEMF_EXECUTABLE` memory remain RW for the user task? Autodoc says "code loaded into this memory can be jumped to", and loaders write to it, so yes. **Verify in the emulator** (UAE-like/QEMU with the AmigaOS4 MMU emulation; QEMU's sam460ex/pegasos2 can run OS4 only with emulation constraints – see 9).

### 8.2 Instruction cache
- `IExec->CacheClearE(APTR address, ULONG length, ULONG caches)` (`interfaces/exec.h:137`), flags `CACRF_ClearI | CACRF_ClearD` (`exec/execbase.h:253-271`). Autodoc: flushes (pushes dirty D lines first) and invalidates I-cache; explicitly listed use cases include "self modifying code / run-time code patches". → use for **`cacheFlush()`** in the PPC MacroAssembler. Call after `LinkBuffer` finalize and every `repatch*`/`relink*`.
- Cost: system call per patch. Batch where JSC allows it (`ExecutableAllocator::performFinalization` flush once per linked region; repatch sites call flush on a few words each). Consider own `dcbst;sync;icbi;sync;isync` loop for short ranges (needs cache line size: 32 B on G3/G4/e500/440; 64 B on PA6T... PA6T-1682M line size is 64 B for L1; **query at runtime**: use `IExec->GetCPUInfoTags(GCIT_CacheLineSize...)` if available – verify in `exec` autodocs `GetCPUInfo`).
- Emulators: QEMU does not model icache incoherency (TCG invalidates translated blocks on writes with some caveats and uses `icbi`/`isync` as TB flush hints on PPC) → **missing icache flushes will NOT show up in emulation but will crash on real hardware**. Make a debug assertion/test that checks every patched range was passed to `cacheFlush` (add a debug-only tracking set in the MacroAssembler).

### 8.3 Threads / signals / GC
- `-athread=native`, pthreads from clib4. `Thread::suspend/resume` are **compiled out** for AMIGAOS in `ThreadingPOSIX.cpp` → the Heap cannot stop another thread to scan its stack/registers via the signal approach. Options:
  - (a) **Run JSC single-threaded**: `useConcurrentJIT=false`, `useConcurrentGC=false`, `numberOfGCMarkers=1`, `numberOfDFGCompilerThreads=0`, `useJITCage=false`, `useSamplingProfiler=false`. Only the JS main thread touches the VM. Workers (`Worker`/`WebKit` threads) each have their own VM and own stack, so cross-thread scanning isn't needed except for `JSLock`-protected shared heap items. This is almost certainly the current C_LOOP situation; check `Options` set for it. **Recommended for JIT v1.**
  - (b) Implement suspend via `pthread_kill(SIGUSR2)` + `sigaction(SA_SIGINFO)` + `ucontext` (`mcontext.gregs`, NIP idx 32) + semaphores. Test on real clib4: whether the signal handler runs asynchronously on target pthread (clib4 pthread = exec Process; signals might be emulated via exec signal bits and deliver only at clib4 call points → not async!). If not async → option (a) only.
- `VMTraps` (JS termination, watchdog, `Debugger`): uses `SIGUSR`+ `ucontext` PC redirect to JIT code when `ENABLE(SIGNAL_BASED_VM_TRAPS)` (`PlatformEnable.h`, only X86_64/ARM64 with HAVE(MACH_EXCEPTIONS) etc.). Keep it off → polling (`VMTraps` check at loop back-edges / function entries via `checkTraps` in baseline JIT; LLInt always polls). Fine.
- Stack overflow detection is by explicit stack limit checks (`VM::softStackLimit`, `StackBounds`) – no guard-page SIGSEGV needed. `StackBounds` needs `pthread_getattr_np`-equivalent on clib4/AmigaOS (`FindTask()->tc_SPLower/tc_SPUpper`); already handled for C_LOOP (check `StackBounds.cpp` – JIT frames are larger than C_LOOP frames; adjust `Options::maxPerThreadStackUsage`, `reservedZoneSize`).
- Machine context for the crash handler/SamplingProfiler (`MachineContext.h:95 CPU(PPC)`) uses Linux-style `gregs` – matches clib4 `ucontext.h` (NIP at 32). Only needed for (b).
- `jsc` shell can run signal-free.

### 8.4 ABI facts confirmed from toolchain
- `ppc-amigaos-g++ -mcrt=clib4 -std=c++20 -dM -E`: `__PPC__`, `_ARCH_PPC`, `__BIG_ENDIAN__`, `__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__`, `__FLOAT_WORD_ORDER__ big`, `__SIZEOF_LONG__ 4`, `__amigaos4__`. → `CPU(PPC)`, `CPU(BIG_ENDIAN)` true; **`CPU(REGISTER64)` false → `USE(JSVALUE32_64)`**.
- AmigaOS4 uses **SysV ELF PPC ABI (32-bit)**, not EABI-embedded: stack 16-byte aligned, LR saved in caller frame at `SP+4`, back-chain at `0(SP)`, **no red zone**, args r3–r10, FP args f1–f8, return r3(:r4) / f1, 64-bit integers in odd/even register pairs *starting at an odd register* (r3:r4, r5:r6, r7:r8, r9:r10; skip a register to align), `r2` = system reserved (do not touch), `r13` = small-data pointer (present with `-msdata=sysv`, check build flags; treat r2/r13 as reserved in JSC register allocation), callee-saved r14–r31, f14–f31, CR2–CR4. Varargs: CR bit 6 set if FP args used (`crxor 6,6,6` before calling a varargs function). `-mlongcall` is used by this port: external calls go through `lis/ori/mtctr/bctrl` – **JIT calls to C++ helper functions must do the same** (helpers are far outside ±32MB of the JIT pool: always load absolute address into a register + `mtctr/bctrl`).
- Confirm exact ABI by compiling a sample with `ppc-amigaos-gcc -S` (pair alignment, struct return for `EncodedJSValue` = `int64_t`).
- AmigaOS4 is not run by the OS in "user/kernel split with NX"; no PROT_EXEC enforcement beyond `MEMF_EXECUTABLE`. Executing from normal heap *might* work on current OS4.1 releases but is not guaranteed → use `MEMF_EXECUTABLE`.

## 9. Emulation specifics (AmigaOne / Pegasos 2 under emulation)
- QEMU `pegasos2` (G4 MPC7447/7457 model) and `sam460ex` (PPC440EP, no FPU? 440EP has FPU; QEMU supports), AmigaOne XE/SE (G3/G4) via **QEMU `-M pegasos2` or UAE-style PPC emulators (e.g. `QEmu-OS4`, PPC via `pegasos2`)**. Each model has different support for: AltiVec (fine, not needed), FPU denormals/rounding (QEMU soft-float uses a software model; fully IEEE so fine), `fsqrt` (all supported), `fres/frsqrte` (**avoid** – approximate values differ between hardware and emulators; JSC does not need them), `mffs/mtfsf` semantics, `lwarx/stwcx` (fine), `dcbz` (line size! dcbz on 440 zeroes 32 B; on 750 32 B; PA6T 64 B → do NOT use `dcbz`), `lmw/stmw` (fine, but slow/unaligned traps; avoid), unaligned FP loads (`lfd` on 4-byte aligned data → alignment exception on some cores/emulations; use `-mstrict-align`-compatible 8-byte alignment of doubles, `Register` slots are 8-aligned).
- Emulation is slow → JIT speedup will be visible but test suites run long. Prefer bring-up on **qemu-ppc user-mode or a PPC32 BE Linux guest** (Debian-ports `powerpc` / `qemu-system-ppc -M mac99/g3beige`) to run `jsc` directly against JS test suites at full QEMU TCG speed with scripts; switch to AmigaOS4 guest only for OS glue (`MEMF_EXECUTABLE`, `CacheClearE`). Make the OS-glue layer tiny and `#if OS(AMIGAOS)`-isolated so the rest compiles for `OS(LINUX)` + PPC BE (cross-compiler `powerpc-linux-gnu-g++` or in a Debian ppc32 chroot).
- Because icache coherency can't be validated in emulation, final validation on real hardware (X1000/A1222/Sam460 or AmigaOne G4) is mandatory before release.

## 10. What is DFG?
JSC has 4 tiers: **LLInt** (interpreter) → **Baseline JIT** (template JIT, direct translation of bytecode to machine code, ~2-4x faster than interpreter, no speculation) → **DFG JIT** (Data Flow Graph: optimizing JIT; builds an SSA-ish graph from bytecode + profiling data, speculates on types (e.g. "this is always an int32"), inlines, removes redundant checks/allocations, has OSR exit back to baseline when speculation fails; fast to compile, 2-10x faster than baseline in hot loops) → **FTL/B3** (heavy optimizer via LLVM-like B3/Air backend; 64-bit only, long compile times). Also **Yarr JIT** (regex) and **WebAssembly** (BBQ/OMG) are separate JITs sharing the same assembler.
  - Baseline alone gives most of the "visible" speedup for typical web pages (short scripts, DOM glue); DFG matters for compute-heavy JS. DFG for a 32-bit ARMv7 exists (`DFGSpeculativeJIT32_64.cpp`), so it is portable to PPC32 later, but it's ~50k+ lines of backend-specific surface (OSR exit, speculation check helpers, `AssemblyHelpers` ops, register pressure, FPR handling) – plan it as a separate, later project; FTL is out of reach on 32-bit.
  - Memory: DFG+baseline JIT code uses more RAM; on 512MB–1GB AmigaOS machines the JIT pool size and `Options::` thresholds (`thresholdForJITAfterWarmUp`, `thresholdForOptimizeAfterWarmUp`) must be tuned.

## 11. Detailed technical notes for future investigation

### 11.1 Files to touch (checklist, relative to `Source/`)
WTF:
- `WTF/wtf/PlatformEnable.h` (JIT/DFG/YARR/JUMP_ISLANDS gating; currently forces `ENABLE_JIT 0` under `USE(JSVALUE32_64)` unless ARMv7 Linux).
- `WTF/wtf/PlatformCPU.h` (PPC macros exist: `WTF_CPU_PPC`; add e.g. `CPU(PPC32_HARDFP)` helper if wanted).
- `WTF/wtf/PlatformUse.h` (`USE_JSVALUE32_64` derived; nothing to do).
- `WTF/wtf/OSAllocatorAmigaOS.cpp` (executable pool), `WTF/wtf/PlatformMUI.cmake`.
- `WTF/wtf/posix/ThreadingPOSIX.cpp` (suspend/resume if (b)), `WTF/wtf/PlatformRegisters.h` (register struct for AMIGAOS), `WTF/wtf/StackBounds.cpp`.
- `WTF/wtf/FastMalloc`/`bmalloc` not involved (USE_SYSTEM_MALLOC=ON in `OptionsMUI.cmake:12`): fine, but `ExecutableAllocator` must not depend on `libpas`/`bmalloc` jit heap (`ENABLE(LIBPAS_JIT_HEAP)` default only on X86_64/ARM64 Darwin/Linux; keep off).
JSC:
- `JavaScriptCore/CMakeLists.txt`: add `assembler/MacroAssemblerPPC.cpp`, offlineasm arch detection (`WEBKIT_OPTION`/`OFFLINEASM_...`), Ruby `offlineasm/asm.rb` invocation (`JavaScriptCore/CMakeLists.txt` ~220-300), `LLIntOffsetsExtractor`, `LLIntSettingsExtractor` (these are *host-built-and-run?* no: they are compiled for the target and then parsed via `generate_offset_extractor.rb` as **binary data** – offsets are read from the ELF/Mach-O object bytes; with a cross-compile to PPC **BE** the extractor must read **big-endian** magic numbers. Check `offlineasm/offsets.rb`/`generate_offset_extractor.rb`: they search for a pattern of bytes and interpret ints with `unpack("V")`/`"L<"`; PPC BE needs `"N"`/`"L>"`. **This is a known-risk place** – BE support exists upstream for MIPS BE? Verify; there is `OFFLINE_ASM_BIG_ENDIAN` handled in `offsets.rb` `Offsets.walk`?). Also `build/` must run `ruby` scripts on the host (Linux x86_64) – fine.
- `JavaScriptCore/assembler/`: `AssemblerBuffer.h` (`putInt`), `AssemblerCommon.h`, `AbstractMacroAssembler.h`, `MacroAssembler.h` (CPU dispatch + `isPtr/…` helpers + `branchPtr` 32-bit overloads), `MacroAssemblerCodeRef.h`, `CPU.h` (`isPPC`), `LinkBuffer.cpp`, `RegisterInfo.h`, `JITOperationList`, `SecureARM64EHashPins` (N/A).
- `JavaScriptCore/jit/`: `GPRInfo.h`, `FPRInfo.h`, `RegisterSet.cpp` (reserved/callee-save sets), `CCallHelpers.h` (argument setup with 64-bit pair alignment), `AssemblyHelpers.h`, `ThunkGenerators.cpp`, `JITStubRoutine`, `ExecutableAllocator.cpp`, `JITOperations`, `CallFrameShuffler*`, `SlowPathCall`, `JITInlineCacheGenerator` (patchable code sequences must have fixed length → `lis/ori` pairs, `PatchableJump` = `lis/ori/mtctr/bctr`), `RepatchBuffer`, `BinarySwitch`.
- `JavaScriptCore/llint/`: `LowLevelInterpreter.asm`, `LowLevelInterpreter32_64.asm` (many `if ARMv7` / `if X86_64` conditional macros; each needs PPC variant: `callSlowPath`, `cCall2/4`, `pushCalleeSaves`/`popCalleeSaves`, `checkStackPointerAlignment`, `vmEntryRecord`, `doVMEntry`, `doCallToJavaScript`, `makeJavaScriptCall`, `restoreCalleeSavesFromVMEntryFrameCalleeSavesBuffer`), `LLIntOfflineAsmConfig.h`, `LLIntThunks.cpp`, `LLIntData`.
- `JavaScriptCore/offlineasm/`: new `ppc.rb`; `backends.rb`, `settings.rb`, `registers.rb`, `risc.rb` (lowering passes shared with arm/arm64: reuse), `config.rb`, `generate_offset_extractor.rb`, `asm.rb` (`.asm` emitter for GNU as; PPC syntax: register names `r3`/`%r3`? GNU as accepts `3` or `r3` with `-mregnames`; use `-mregnames` or emit bare numbers).
- `JavaScriptCore/yarr/YarrJIT.cpp`: has `CPU(BIG_ENDIAN)` bits; verify 16-bit char loads and `YarrJITRegisters.h` (register assignment per CPU: add PPC block).
- `JavaScriptCore/bytecode/`: `InstructionStream.h:262-275` BE narrow-operand handling; `BytecodeGenerator`/`Opcode` `generate_bytecode.rb` (`Fits<>` templates, `OpcodeSize` narrow/wide16/wide32) — **BE affects how `Wide16/Wide32` prefixes and narrow operand bytes are read by the asm** (`loadb`/`loadh`/`loadi` with BE offsets). Key piece of risk: `LowLevelInterpreter.asm` `macro getOperand` and `generate_bytecode` `BE` fixups.
- `JavaScriptCore/runtime/`: `JSCJSValue.h:102` (BE layout: tag/payload order), `Options*` (defaults/ gating), `MachineContext.h`, `JSCPtrTag`/`PtrTag` (no-ops), `VM::…` `jitStubs`, `SamplingProfiler` (off), `TypedArray` BE correctness (`JSGenericTypedArrayView*` have BE branches; DataView/typed array little-endian semantic requirement: JS typed arrays use platform endianness, DataView default big-endian is explicit; **`CPU(BIG_ENDIAN)` paths in typed arrays are exercised only in C_LOOP today—good**).
- `Source/cmake/OptionsMUI.cmake:13-21`, `WebKitFeatures.cmake:80-95`, `Source/cmake/amigaos.cmake` (flags: add `-mcpu=750`? Possibly keep default; `-mhard-float`).
- `Tools/Scripts`/tests: none in this repo.

### 11.2 Design decisions to take before coding
1. **Register map** (PPC32 SysV, avoid r0(reads as 0 in base slot), r1, r2, r13): proposal sketch – `callFrameRegister = r31`? (callee-saved; JSC wants cfr, plus `numberTag`/`notCellMask` not needed for 32_64), tag/payload temps `regT0..regT7 = r3..r10` (arg regs, caller-saved), `regT8/9 = r11/r12` scratch (also used by PLT stubs/long calls → pick `r12` for `bctr` targets), `regCS0.. = r14–r30` callee-saved (baseline uses `s0=metadata table, s1=PC base` etc.), `linkRegister = LR` (special), `ctr` for indirect branches; FPR: `fpRegT0..` = f1–f13 (caller saved), `fpRegCS` = f14–f31, scratch `f0`/`f13`. Reserve `r11, r12` + `f0` as MacroAssembler scratch (like `dataTempRegister`/`memoryTempRegister` on ARM).
2. **Branch strategy**: all JIT-to-JIT jumps `b`/`bc` direct while pool ≤ 32 MB (bc has ±32 KB only: use `bc` over an unconditional `b` for conditional-jump long form always, or use "conditional-skip + b" pattern giving ±32 MB; implement `ENABLE_JUMP_ISLANDS` only if pool > 32MB). JIT→C++ calls: always `lis/ori/mtctr/bctrl` (≥4 words).
3. **Patchable sequences**: constant pointer = `lis r,hi; ori r,r,lo` fixed 2 words; `repatchInt32`, `repatchPointer`, `linkJump` patch the immediate or the `b` displacement; always followed by `cacheFlush` over exactly the touched words (+ `sync; isync` on the executing thread — for the same-thread case it's sufficient to flush; other threads are not an issue in single-mutator mode).
4. **Condition codes**: PPC has CR fields; MacroAssembler flags-based (`ARM64`-style `branchAdd32(Overflow…)`) → implement overflow ops with `addo./subfo./mullwo.` (XER[OV/SO] → `mcrxr`; note `mcrxr` is removed on POWER7+ but present on all targets listed here; PA6T OK; for e500/440 `mcrxr` exists (440 is Book E: mcrxr exists)), or compute overflow with `xor/and/rlwinm` sequences (portable, deterministic, preferred – 3-4 more instructions). `DoubleCondition` NaN handling: `fcmpu` sets CR bit FU (unordered) → combine with `cror`.
5. **Double→int32** (`truncateDoubleToInt32`): `fctiwz f0,fN; stfd f0,-8(sp)` uses **red zone → not allowed in SysV PPC** → reserve scratch space in JIT frame (e.g. keep 16 bytes at `sp+8`), then `lwz r,12(sp)` (BE: low word of the double at +4). JS ToInt32 modulo behaviour when out of range needs a slow-path (`fctiwz` saturates) → branch to a helper (as ARMv7 does with `branchTruncateDoubleToInt32`).
6. **Int↔double**: `int→double` = stw hi=0x43300000 with xor 0x80000000 trick (magic number) + `lfd` + `fsub` (classic PPC idiom), or `stw` + `lfd` w/ bias constant.
7. **Frame layout**: Use own frame set up by `emitFunctionPrologue`: `stwu r1,-N(r1)`; `mflr r0; stw r0,N+4(r1)`; JSC `CallFrame` header (callerFrame, returnPC) must be **at cfr+0 and cfr+4** in the same position the machine ABI expects *or* the VM must emulate (ARMv7 uses `fp/lr` pair at 0/4 = exactly SysV PPC `backchain/LR save`!). PPC SysV frame: `0(sp)=backchain`, `4(sp)=LR save slot of the **callee** stored in caller's frame (i.e. `4(oldsp)`)`. This is *different* from ARM: LR is saved in the **caller's** frame, not the callee's. JSC convention requires `CallerFrame` at `cfr[0]` and `ReturnPC` at `cfr[1]` in the callee frame – so JSC-JIT functions should store LR themselves at `4(cfr)` in their *own* frame after `stwu` (not in the caller's). C++ helpers called from JIT use normal ABI. `CallFrame::returnPC` consumers (stack walker, `StackVisitor`, `ExceptionHandler`) only read the JSC convention → OK.
8. **Atomics/fences**: `lwsync` (not on 440? `lwsync` is on Book E 440 as `msync`/`mbar`; on e300/G3 `lwsync` traps as illegal/undefined — **G3 (750) has no `lwsync`**; use `sync`). Pegasos/AmigaOne G3/G4: use `sync`/`isync` only. 64-bit atomic ops unsupported → JSC uses `Atomics` via compiler builtins (libatomic? `__atomic_*_8` calls to libatomic or inline `lwarx` loops; check link errors).
9. **ISA floor**: PPC 604/750 (G3). No `popcnt`, no `isel` (e500 only), no `fcfid`, no `fsel`? (`fsel` exists on 603+/750 – fine but avoid), no `mfocrf`, no `lwsync`, no VMX. `fsqrt` is optional (absent on 603/750; present on G4? **MPC7400/7450 have `fsqrt`? No – 7450 lacks `fsqrt`/`fsqrts`? (they are in the optional "graphics" group `fres/frsqrte/fsel/fsqrt`: 604/750 lack `fsqrt`; G4 has `fsqrt`?)** — verify per CPU; treat `Math.sqrt` as a C helper call (`sqrt()` from libm) on JIT v1 and use `supportsFloatingPointSqrt() = false` in the MacroAssembler. Same for `floor/ceil/trunc/round`: `supportsFloatingPointRounding() = false`. `abs` = `fabs`, `neg` = `fneg` (ok). `fctiwz` ok.
10. **`USE(JSVALUE32_64)` baseline requires 64-bit `EncodedJSValue` in two GPRs on return (`r3:r4`)**; on BE the **tag is the high word if the JSValue union is `{ int32 tag; int32 payload }`; `JSCJSValue.h:102` under `CPU(BIG_ENDIAN)` places `tag` first → `r3 = tag, r4 = payload`** (int64 value = tag<<32|payload; matches `EncodedJSValue` as `int64_t`). Check `JSVALUE_...` encoding `EncodedJSValue` = `int64_t` on 32-bit; return/arg pair order then is natural for BE (hi in lower-numbered reg).

### 11.3 Known gaps/risks specific to this repo
- Branch is "Starting to implement 2.48 branch.. a lot of things to fix" (last commit): build is in flux. **Make sure C_LOOP build works and passes a JS conformance baseline before starting the JIT work.**
- `Source/cmake/OptionsMUI.cmake` has `ENABLE_FTL_JIT` ON only for x86_64 (leftover from cross-testing); the AmigaOS flag set (`-mstrict-align -mlongcall -mno-sched-prolog`) should be reviewed for JIT objects: `-mstrict-align` fine.
- `-fpermissive`, `-O1` currently (TEMP build speed) → performance numbers will be wrong until `-O2/-O3`; also affects C_LOOP speed (COMPUTED_GOTO: `HAVE_COMPUTED_GOTO` (PlatformHave.h:267) – confirm it's on for GCC so C_LOOP uses `&&label` dispatch; huge effect).
- No JS test corpus in tree. Obtain `JSTests/stress`, test262, `Tools/Scripts/run-javascriptcore-tests` or just run `jsc` with `--useJIT` vs C_LOOP differential tests.
- Heap: `USE_SYSTEM_MALLOC=ON` implies no bmalloc/libpas; GC `MarkedBlock` alignment (`MarkedBlock::blockSize` 16KB aligned allocation via `AlignedMemoryAllocator` → `posix_memalign`/`memalign` in clib4? check). Not JIT-specific, but JIT code embeds absolute addresses of GC objects/structures (constants); no relocation → fine.
- Endianness: **PPC BE is not tested upstream for the JIT and only rarely for LLInt (MIPS BE was dropped)**. Areas expected to need fixes beyond the backend: `generate_bytecode`/offlineasm narrow-operand loads, `StructureID` encoding, `JSString`/`StringImpl` flags (`bitfields` order in structs with `CPU(BIG_ENDIAN)` like `CellState`/`JSType` bytes in `JSCell` header: `JSCell` is 8 bytes `{StructureID(4), indexingType(1), type(1), flags(1), cellState(1)}`; offlineasm reads them as bytes with offsets from the extractor → OK; but any asm that does `loadi`/`loadh` multi-byte reads of byte-packed fields must be BE-aware, e.g. `JSCell::indexingTypeAndMisc` combined loads in `LowLevelInterpreter32_64.asm`), `Int52`/`double` bit masks, `PropertyOffset`, `Butterfly` (`IndexingHeader` public/vector length order `uint32` pair), `ArrayBuffer`/`DataView` (`getUint32 default big-endian` etc.), `JSValue::EncodeAsDouble` helpers, `MathCommon`, Yarr 16-bit char loads (done in `YarrJIT.cpp` BE blocks).

## 12. Suggested concrete roadmap (updated with answers)
| Step | Goal | Output | Validate |
|---|---|---|---|
| 0 | C_LOOP baseline: confirm `-std`, compile flags, computed goto, -O2; run JS conformance (jsc + test262 subset) | baseline numbers + BE bug fixes | emulated Pegasos2 / qemu-ppc Linux |
| 1 | Linux-PPC32-BE bring-up env (qemu-ppc, Debian ports ppc or crossgcc + chroot), `jsc` target buildable with `ENABLE_JIT=0` | reproducible fast test loop | `jsc` runs C_LOOP |
| 2 | PPC assembler + MacroAssembler + `testmasm` PPC tests (all MacroAssembler ops, patching, branches, FP, conditions) | `PPCAssembler.h`, `MacroAssemblerPPC.*`, GPR/FPR info | `testmasm` passing under qemu |
| 3 | Yarr JIT only (decouple from LLInt if needed) | regex JIT enabled | regex test262 + `--useRegExpJIT` diff |
| 4 | offlineasm `ppc.rb`, LLInt asm for 32_64 BE (JIT off) | asm interpreter | whole JS test-suite vs C_LOOP |
| 5 | Baseline JIT (`ENABLE_JIT=1`, DFG off) + executable pool + cache flush glue on OS4 | working JIT | stress tests with `--useBaselineJIT --thresholdForJITAfterWarmUp=0` (force JIT everywhere) |
| 6 | AmigaOS4 glue (MEMF_EXECUTABLE pool, CacheClearE, single-thread options), test on real HW | release candidate | real X1000/G4 |
| 7 | optional DFG 32_64 | later | later |

## 13. Still-open questions
1. Is `AllocVecTags(AVT_Type, MEMF_EXECUTABLE)` memory writable by user code on your OS4.1 version (I could only confirm docs, not run)? A 20-line test program on the emulator answers it.
2. Does clib4 `pthread_kill` + `sigaction(SA_SIGINFO)` give asynchronous signal delivery to another pthread? (Needed only for multi-thread GC/JIT; otherwise single-thread options.)
3. Is the emulator you use able to run the Linux/PPC route in 9 for bring-up (speeds up the dev loop massively)?
4. Minimum supported CPU: G3 (no `lwsync`, no `fsqrt`) or can I assume G4+ (also e500/440 compat)? Dropping G3 simplifies little; dropping `lwsync`/`fsqrt` use costs little – recommended to keep G3 as floor.
5. RAM budget for JIT pool (suggest 16–32 MB) and whether `jsc` shell (JavaScriptCore/shell) is part of your build for testing.
