# Kernel imports

What the recompiled title needs from outside itself. This is the work list for Phase
5.1, and it is measured rather than estimated.

```bash
python tools/gen_import_stubs.py --list     # the full list, grouped
python tools/gen_import_stubs.py            # regenerate the weak stubs
```

**251 imports**, derived from `ppc_recomp_shared.h`, where XenonRecomp declares each
one as `PPC_EXTERN_FUNC(__imp__<Name>);`. Derived from the source rather than scraped
from linker output, which would be circular and — since lld caps its error list and
orders it arbitrarily — incomplete.

---

## 1. How the boundary works

Every import is a function with the same shape as a translated guest function:

```cpp
void __imp__KeQuerySystemTime(PPCContext& ctx, uint8_t* base);
```

Arguments arrive in `ctx.r3` onwards and the return value goes in `ctx.r3`, per the
PowerPC ABI. `base` is the guest memory base, so a pointer argument is
`base + ctx.r3.u32`.

`tools/gen_import_stubs.py` emits a **weak** stub for all 251, which is what makes this
a scaffold instead of a wall:

- the library links today, with no import implemented;
- writing a strong definition in a runtime file (`warden_imports_kernel.cc`,
  `warden_xam.cc` and the others) overrides the stub at link time, with **no list to keep
  in step**. The stubs are compiled into the runtime (`warden_import_stubs`, linked into
  `libwarden.so`) since PLAN 12.2, and the game's own library (`libdaogame.so`) reaches
  every import there at load;
- each stub records that it was called.

That last point is the useful one. After a boot attempt the report names every import
the title actually reached, in the order first seen, with call counts.

**A stub returns its import's failure value, never success** (since 2026-10-23, BUG-030):
`STATUS_NOT_IMPLEMENTED` for an `NTSTATUS`, `ERROR_FUNCTION_FAILED` for a Win32 error,
`E_NOTIMPL` for an `HRESULT`, 0 for a pointer, handle or value. Stubs used to leave
`r3` at zero on the reasoning that they could not know what 0 meant to the caller - but
for an `NTSTATUS` it means success, and the title then used outputs nobody wrote, which
caused the first two startup crashes. The family is read from Xenia's kernel, and
`python tools/gen_import_stubs.py --list` shows it per import with the line it came
from. The log line says what the stub returned:

```
unimplemented import reached: NtOpenFile - returns STATUS_NOT_IMPLEMENTED
```

Before any guest code has run, the report reads:

```
KERNEL IMPORTS
  declared by the title          251
  reached so far                 0
  nothing has called one yet - no guest code has run
```

So "implement 251 kernel functions" becomes "implement the ones in the way", in the
order they block progress.

---

## 2. The surface, by group

| Group | Count | What it is | Needed to reach the menu? |
|---|---|---|---|
| `Net*` | **47** | networking (`NetDll_*` sockets, XNet) | **almost certainly not** — see below |
| `Xam*` | 42 | user profile, content/DLC, UI, input | partly — profile and content enumeration |
| `Nt*` | 30 | NT syscalls: files, events, threads | **yes** — this is where the filesystem shim lives (5.2) |
| `Ke*` | 28 | kernel: threads, sync, timing, IRQL | **yes** |
| `Vd*` | 20 | video driver — how the title talks to the GPU | **yes** |
| `Rtl*` | 20 | runtime library: strings, critical sections | **yes**, and mostly mechanical |
| `XMA*` | 16 | XMA audio decoder | **yes** - a context pool and a silent decoder (`warden_xma.cc`); real decoding belongs with sound output, PLAN 4.5 |
| other | 13 | `_vsnprintf`, `__C_specific_handler`, misc | some |
| `Mm*` | 8 | memory manager | **yes** |
| `XAudio*` | 6 | XAudio2 | **yes** - the render driver client, the title's audio clock (`warden_audio.cc`); frames measured and discarded until AAudio |
| `Ex*` | 4 | executive: threads, config settings | **yes** |
| `XGet*` | 4 | system info | probably |
| `Ob*` | 4 | object manager | probably |
| `Xe*` | 3 | misc Xenon | ? |
| `Dbg*` | 2 | debug output | trivial, and useful early |
| `Interlocked*` | 2 | interlocked singly-linked lists | **yes** |
| `XNotify*` | 1 | notification listener | no |
| `Hal*` | 1 | `HalReturnToFirmware` (i.e. reboot) | no |

### The 47 networking imports are worth a comment

That is the largest group in the list, in a **single-player game**. Dragon Age: Origins
has no multiplayer. They are the engine's network layer linked into the executable
whether or not it is used — plausibly reached only for achievement or online-status
checks, if at all.

This matters because it changes the shape of the work: the biggest group is probably
also the cheapest, since "there is no network" is a legitimate and complete answer for
all of them. But *probably* is doing real work in that sentence, and the import log
will say for certain rather than leaving it to be assumed.

### Not an import, but in the same boundary

`PPCQueryTimebase()` is a XenonRecomp runtime hook, not a kernel import — the guest
reads the Xenon's timebase register through it. **Implemented**, as
`xe::Clock::QueryGuestTickCount()`, so the guest and the GPU agree about what time it
is; a separate epoch here would desynchronise them for no reason. Since session 35, less
the time the guest has spent suspended (`warden_suspend.cc`, BUG-057): it stands still
while the app is in the background, as the kernel's time stamp bundle does, so the title
sees no time pass; the wall clock (`KeQuerySystemTime`) runs on.

Phase 2's differential harness deliberately aborted in this function, because any
guest function reaching the timebase was excluded from comparison and arriving there
meant the filter had a hole. At runtime it is simply needed.

---

## 3. What the title actually reaches

**Measured, and reproducible** — `./tools/appbringup/boot_repeat.sh 3` gives the same
crash site, the same twelve imports and the same two allocations every run. Twelve
imports, in the order the title calls them:

```
 1 KeGetCurrentProcessType               7 KeTlsAlloc
 2 RtlInitializeCriticalSection          8 KeTlsSetValue
 3 RtlEnterCriticalSection               9 KeTlsFree
 4 RtlLeaveCriticalSection              10 RtlTryEnterCriticalSection
 5 XexCheckExecutablePrivilege          11 MmAllocatePhysicalMemoryEx
 6 RtlInitializeCriticalSectionAndSpinCount                12 MmQueryStatistics
```

Mostly mechanical: critical sections over pthread mutexes, TLS slots, a
physical-memory allocator beside the virtual one, two queries.
`RtlInitializeCriticalSection` is reached before `RtlEnterCriticalSection`, so the
guest's `RTL_CRITICAL_SECTION` layout has to be respected — the title passes a pointer
to one in guest memory and reads fields back from it.

An earlier capture listed fourteen, including `NtAllocateVirtualMemory` first and
`HalReturnToFirmware` fourth. That was before the allocator existed: the stub returned
0, the title got no memory, and it headed for "reboot the console". With the allocator
in place both are gone from the sequence.

### After `MmQueryStatistics` (2026-10-23)

With all twelve above implemented, the title gets past its allocator and reaches ten
more, again identically over three runs:

```
 1 NtCreateEvent                         6 ExGetXConfigSetting
 2 NtCreateSemaphore                     7 XGetGameRegion
 3 KeQueryPerformanceFrequency           8 RtlInitAnsiString
 4 XamNotifyCreateListener               9 NtOpenFile
 5 XamUserGetSigninInfo                 10 NtQueryDirectoryFile
```

Then it crashes with its own code overwritten (`PLAN.md` 5.1f). Several of these
write through pointer arguments - an event or file handle, an `ANSI_STRING`, a
directory listing - and the stubs write nothing while returning 0, which these
callers read as `STATUS_SUCCESS` (BUG-030). The overwrite is the title's
`FindFirstFile` copying a file name whose length it read from the directory listing
`NtQueryDirectoryFile` never wrote.

### After the stubs fail instead (2026-10-23)

With every stub returning its failure value (5.1g), the title stops crashing and takes
its error paths. Sixteen imports, identical over three runs:

```
 1 NtCreateEvent                 9 RtlInitAnsiString
 2 RtlNtStatusToDosError        10 NtOpenFile
 3 NtCreateSemaphore            11 RtlRaiseException
 4 KeQueryPerformanceFrequency  12 ExCreateThread
 5 XamNotifyCreateListener      13 ObReferenceObjectByHandle
 6 XamUserGetSigninInfo         14 NtReleaseSemaphore
 7 ExGetXConfigSetting          15 NtWaitForSingleObjectEx
 8 XGetGameRegion               16 KeDelayExecutionThread
```

Then the guest thread spins: the `KeDelayExecutionThread` stub returns at once instead
of sleeping. **Read only up to 11 as a work list.** `NtOpenFile` failing makes the
title raise, and the `RtlRaiseException` stub returns where the real one unwinds
(BUG-032), so 12 onwards come from code that should not be running. `NtOpenFile` is
the filesystem shim, `PLAN.md` 5.2.

**Corrected the same day:** the raise was the title naming its thread
(`Eclipse::StartupThread`, exception `0x406D1388`), which it does on purpose and
continues from - found once `RtlRaiseException` logged its record. So the whole list
is the title's normal path, and the work list after all. The paths it passes to
`RtlInitAnsiString` - now implemented and logged - are `D:\packages\core\audio\vo\*`
and `D:\NS\*`, two directory listings it tolerates failing; what stops it is
`ExCreateThread` (`PLAN.md` 5.1h).

### After threads, files and the video driver (2026-10-23, end of day)

With 5.1h and 5.2's reads in, the title runs its whole startup and parks. Stubs still
reached, in order - the next work list, and the first few are worth knowing about:

```
XamNotifyCreateListener  XamUserGetSigninInfo  XGetGameRegion
ExRegisterTitleTerminateNotification  VdInitializeRingBuffer  VdEnableRingBufferRPtrWriteBack
KiApcNormalRoutineNop  KeSetBasePriorityThread  VdCallGraphicsNotificationRoutines
KeEnterCriticalRegion  VdRetrainEDRAMWorker  VdRetrainEDRAM  KeLeaveCriticalRegion
VdIsHSIOTrainingSucceeded  MmQueryAddressProtect  KeEnableFpuExceptions
VdSwap  VdSetDisplayMode  VdGetCurrentDisplayInformation  XamGetSystemVersion
NetDll_XNetStartup  NetDll_WSAStartup  XamInputGetState  XAudioGetSpeakerConfig
XAudioRegisterRenderDriverClient  XamUserGetSigninState  XamUserGetXUID
XamUserCreateAchievementEnumerator  NetDll_WSAWaitForMultipleEvents  NetDll_WSAResetEvent
```

- `VdInitializeRingBuffer` / `VdEnableRingBufferRPtrWriteBack` / `VdSwap` are the GPU:
  the command processor is Phase 5's remaining piece.
- `XGetGameRegion` returning 0 may be behind the `patchULES` directory probe.
- ~~The `NetDll_*` stubs return 0, which for sockets is success~~ Done 2026-10-24:
  `warden_net.cc` answers as a console with no cable, Winsock events are real wait
  objects, and the generated stubs' `net` family returns `SOCKET_ERROR` (BUG-035).
- `XamInputGetState` and the XAudio pair are what the pad and sound need.

### After the network answers "no cable" (2026-10-24)

26 stubs reached; the four network ones are gone. Every engine thread is parked in a
wait except **`Eclipse::Update`, which spins in `Sleep(0)`** in `sub_826CF7D0` (TU6),
polling the byte at `0x832127E5` right after its sign-in screen code (`LoginButton`,
`ReConnect`, `OnlineFeatures` strings) - nothing in the recompiled code stores that
byte with an immediate offset, so it is set through a pointer, most likely from a
notification or the graphics interrupt callback. The D3D worker times out every 30 ms
on an event nothing signals. The GPU interrupt (`VdSetGraphicsInterruptCallback`'s
callback, never called) is the suspect; see BUG-036. The `guest threads` report
(`warden_trace.cc`, every 5 s) is what shows this.

### After sign-in (2026-10-24, end of day)

With user 0 signed in (`warden_xam.cc`) the title leaves its sign-in screen for the
title screen and reaches four more stubs there, all once: `XamContentGetDeviceData`,
`XamUserCreateAchievementEnumerator`, `XexGetModuleHandle`, `XexGetProcedureAddress`.
DAORecomp answers them with a 20 GB "Hard Drive" as device 1, an enumerator of 0x1C-byte
achievement items, the module handle for `default.xex` and no procedures. None of them
is behind the stall that follows (BUG-043 is FMOD's memory).

## 4. Implemented so far

| Import | Notes |
|---|---|
| `PPCQueryTimebase` | `xe::Clock::QueryGuestTickCount()` less the time spent suspended (`warden_suspend.cc`; a runtime hook, not an import) |
| `NtAllocateVirtualMemory` | guest heap, `warden_guest_heap.cc` |
| `MmAllocatePhysicalMemoryEx` | physical heap; returns an address in the `0xA0000000` view |
| `KeGetCurrentProcessType` | 1 — a game from disc is a user process |
| `XexCheckExecutablePrivilege` | 0 for everything; this build is signed for nothing |
| `RtlInitializeCriticalSection` | host-side lock keyed on the guest pointer |
| `RtlInitializeCriticalSectionAndSpinCount` | same; the spin count is accepted and ignored |
| `RtlEnterCriticalSection` | |
| `RtlLeaveCriticalSection` | |
| `RtlTryEnterCriticalSection` | |
| `KeTlsAlloc` / `KeTlsSetValue` / `KeTlsGetValue` / `KeTlsFree` | slots from 1; values per guest thread |
| `MmQueryStatistics` | Xenia's 104-byte layout; available pages = what the physical heap can still hand out |
| `RtlInitAnsiString` | fills the caller's `ANSI_STRING` (`u16 Length, u16 MaximumLength, u32 Buffer`); logs the first 64 strings - the paths the title opens |
| `RtlRaiseException` | a thread name (`0x406D1388`) is logged and returned from; anything else is logged with the chain, and the guest thread stops (BUG-032) |
| `NtOpenFile` | `warden_fs.cc`; **six arguments** - share access in `r7`, open options in `r8`, from the title's own call; Xenia's has five (BUG-033) |
| `NtQueryDirectoryFile` | one entry per call; a pattern restarts the scan, no pattern continues; `restart_scan` from the stack at `r1 + 0x54` |
| `NtClose` | `warden_objects.cc`; any handle in the shared object table |
| `NtCreateFile`, `NtReadFile`, `NtReadFileScatter`, `NtQueryInformationFile`, `NtSetInformationFile`, `NtQueryFullAttributesFile`, `NtQueryVolumeInformationFile`, `NtFlushBuffersFile`, `NtWriteFile` | `warden_fs.cc`. The disc (`D:\`, `game:\`) is read-only: writes and creating dispositions are refused there. A **content root** (`save:\` while `XamContentCreateEx` has it mounted) is a writable folder in the app's files directory: every NT disposition (supersede, open, create, open-if, overwrite, overwrite-if) for files and folders, `NtWriteFile` at an offset or at the current position (-1 / -2), `NtFlushBuffersFile` as an `fsync`, and `NtSetInformationFile` for the position, the end of file (truncate or extend), delete-on-close (class 13), rename (class 10, within the root) and the basic and allocation classes (accepted). Its volume reports a hard drive; its listings are taken again on each new scan. Log lines name the root (`save:\Saves\savegame.das`). Written in run 199 |
| `ExCreateThread`, `ExTerminateThread`, `NtResumeThread`, `NtSuspendThread`, `KeDelayExecutionThread`, `NtYieldExecution`, `KeQueryPerformanceFrequency`, `KeQuerySystemTime`, `KeSetAffinityThread` | `warden_threads.cc`; a host thread per guest thread on its own stack. `KeDelayExecutionThread` and `NtYieldExecution` hold the thread while the guest is suspended (`warden_suspend.cc`, BUG-057) |
| `NtCreateEvent`, `NtSetEvent`, `NtPulseEvent`, `NtClearEvent`, `KeSetEvent`, `KeResetEvent`, `NtCreateSemaphore`, `NtReleaseSemaphore`, `KeInitializeSemaphore`, `KeReleaseSemaphore` | `warden_threads.cc`; `Ke*` by guest pointer, adopting the title's own in-structure objects |
| `NtWaitForSingleObjectEx`, `NtWaitForMultipleObjectsEx`, `KeWaitForSingleObject`, `KeWaitForMultipleObjects` | one wait mutex and condition variable for every object. The timeouts are in registers: r6, r8, r7, and r9 for `KeWaitForMultipleObjects`, whose eight arguments all fit - it read the stack's r1 + 0x54 until session 32 (BUG-050). Each, and `NetDll_WSAWaitForMultipleEvents`, holds the thread at its entry and its exit while the guest is suspended - outside the wait mutex (`warden_suspend.cc`, BUG-057) |
| `ObReferenceObjectByHandle`, `ObDereferenceObject` | a 0x100-byte guest body per object, with the dispatcher header |
| `XGetVideoMode`, `VdQueryVideoMode`, `VdQueryVideoFlags`, `VdGetCurrentDisplayGamma`, `VdInitializeEngines`, `VdShutdownEngines`, `VdSetGraphicsInterruptCallback`, `VdSetSystemCommandBufferGpuIdentifierAddress`, `VdGetSystemCommandBuffer` | `warden_video.cc`; 1280x720, Xenia's answers; the callback kept for `DispatchInterrupt` |
| `ExGetXConfigSetting` | Xenia's defaults, sized by the title's own calls; its refusal had been cancelling device creation |
| `MmGetPhysicalAddress` | `xe::Memory::GetPhysicalAddress`; the stub's 0 was what the ring buffer was set up on |
| `RtlNtStatusToDosError` | the statuses this runtime returns, mapped as Windows maps them; unknown failures become 317 and are logged |
| `NetDll_XNetStartup`, `NetDll_XNetCleanup`, `NetDll_WSAStartup`, `NetDll_WSACleanup`, `NetDll_WSAGetLastError`, `NetDll_XNetGetEthernetLinkStatus`, `NetDll_XNetGetTitleXnAddr`, `NetDll_XNetGetConnectStatus`, `XNetLogonGetMachineID`, `XNetLogonGetTitleID`, `NetDll_XNetDnsLookup`, `NetDll_XNetDnsRelease`, `NetDll_XNetQosServiceLookup`, `NetDll_XNetQosRelease`, `NetDll_XNetRandom`, `NetDll_inet_addr`, `NetDll_XNetInAddrToString`, `NetDll_socket`, `NetDll_WSAGetOverlappedResult` | `warden_net.cc`: a console with no cable. Startups succeed (they always do), the link is down, no XNADDR, DNS finds nothing, `socket` fails with `WSAENETDOWN`. **Nothing here ever touches the device's network, by decision** - the app has no INTERNET permission and must not get one |
| `NetDll_WSACreateEvent`, `NetDll_WSACloseEvent`, `NetDll_WSASetEvent`, `NetDll_WSAResetEvent`, `NetDll_WSAWaitForMultipleEvents` | `warden_threads.cc`: Winsock events are manual-reset dispatcher events, so the network thread blocks in its wait (timeout in ms, `WSA_INFINITE` forever) instead of spinning through a stub |
| the other 42 `NetDll_*` | generated stubs, family `net`: `SOCKET_ERROR` / `INVALID_SOCKET` (`~0`) - a 0 would be success |
| `VdInitializeRingBuffer`, `VdEnableRingBufferRPtrWriteBack` | `warden_video.cc` → `warden_gpu.cc`: the PM4 ring (a physical address, size 2^(n+3)) and the read-pointer write-back, handed to Xenia's command processor |
| `VdSwap` | `warden_video.cc`; with the GPU up, the front buffer's fetch constant and an `XE_SWAP` packet go into the 64 words the title reserved at its command buffer (Xenia's `VdSwap`); the last two arguments (width, height pointers) are on the stack at `r1 + 0x54` and `+0x5C`. Without the GPU, a pending swap the fallback interrupt thread reports complete |
| the GPU | not an import: `warden_gpu.cc` starts Xenia's Vulkan graphics system at `Runtime::BootGuest` (its own Vulkan device, presenter and command-processor thread, over the guest memory). The write-pointer kick reaches it through `PPC_MM_STORE_U32` - the hook force-included into the generated code (`CMakeLists.txt`) for stores into `0x7FC80000` - and `GraphicsSystem::WriteRegister`. Interrupts come through `xe::dao_host::DispatchInterrupt` (`warden_dao_host.cc`): source 0 from the vsync worker at 60 Hz, source 1 from the title's own `INTERRUPT` packets, each with the core number written into the calling thread's PCR first. The register window is plain guest memory preset with Xenia's `ReadRegister` answers. Without a graphics system, `warden_video.cc`'s fallback thread delivers a vblank and a swap-complete |
| `KeTlsAlloc`, `KeTlsSetValue`, `KeTlsGetValue`, `KeTlsFree` | values in guest memory, in the calling thread's TLS block after its static part (`warden_guest.cc`): the title reads its TLS straight through r13 too |
| `KfAcquireSpinLock`, `KfReleaseSpinLock`, `KeAcquireSpinLockAtRaisedIrql`, `KeReleaseSpinLockFromRaisedIrql`, `KeRaiseIrqlToDpcLevel`, `KfLowerIrql` | `warden_threads.cc`; a compare-and-swap on the guest lock word (big-endian 1 when held), spin then yield; IRQLs are nominal |
| `VdIsHSIOTrainingSucceeded` | `warden_video.cc`; 1, as Xenia and DAORecomp. The stub's 0 told Direct3D the GPU link had failed, and it stopped kicking the ring from the second frame (BUG-038) |
| `VdRetrainEDRAM`, `VdRetrainEDRAMWorker` | 0: nothing to retrain |
| `DbgPrint` | `warden_imports_kernel.cc`; the title's own diagnostics to the log as `[thread] DbgPrint: ...` - a printf over guest memory (d i u x X o c s p, flags, width, precision; arguments from r4 then the stack). Direct3D's GPU-hang messages arrive this way |
| the twelve **imported variables** | not functions: `warden_kernel_variables.cc` gives `ExThreadObjectType`, `KeDebugMonitorData`, `KeTimeStampBundle`, `XboxHardwareInfo`, `XboxKrnlVersion`, `XexExecutableModuleHandle`, `ExLoadedCommandLine`, `KeCertMonitorData`, `VdGlobalDevice`, `VdGlobalXamDevice`, `VdGpuClockInMHz` and `VdHSIOCalibrationLock` a home with Xenia's values and writes its address into the import record, as the console's loader does. The records had held raw ordinal words, which the title dereferenced (BUG-038's null call). `KeTimeStampBundle` is refreshed at 1 kHz |
| `XamInputGetState`, `XamInputGetCapabilities`, `XamInputSetState` | `warden_xam_input.cc`; the pad state the Java side keeps (`ControllerManager` → `warden_input.cc`, task 4.6), as XInput's big-endian structures; the packet number advances when the state changes; an empty slot is `ERROR_DEVICE_NOT_CONNECTED`; rumble is accepted and dropped |

| `XAudioRegisterRenderDriverClient`, `XAudioUnregisterRenderDriverClient`, `XAudioSubmitRenderDriverFrame`, `XAudioGetSpeakerConfig`, `XAudioGetVoiceCategoryVolume`, `XAudioGetVoiceCategoryVolumeChangeMask` | `warden_audio.cc`, DAORecomp's design: a worker calls the registered callback every 256 samples at 48 kHz (5.333 ms) with the address of a guest cell holding the title's argument; submitted frames (6 x 256 big-endian floats, channel-major) are mixed down to stereo as Xenia does (front left and right, the back pair, half the centre, no low frequency, times 1 / 2.5) and played through an AAudio stream (PLAN 4.5, run 207). While the stream consumes, the worker's schedule runs 0.5% fast and the stream's queue (two of its bursts and a frame) holds it back, so the title's audio clock follows the device's; otherwise the steady clock keeps the console's schedule. The stream pauses with the app. Without the callback the title's FMOD has no clock |
| `XMACreateContext`, `XMAReleaseContext`, `XMAInitializeContext`, `XMAEnableContext`, `XMADisableContext`, `XMASetInputBuffer0Valid`, `XMAIsInputBuffer0Valid`, `XMASetInputBuffer1Valid`, `XMAIsInputBuffer1Valid`, `XMASetInputBufferReadOffset`, `XMAGetInputBufferReadOffset`, `XMASetOutputBufferValid`, `XMAIsOutputBufferValid`, `XMASetOutputBufferReadOffset`, `XMAGetOutputBufferReadOffset`, `XMAGetOutputBufferWriteOffset` | `warden_xma.cc`, DAORecomp's port of Xenia's `XmaContextFake`: 320 contexts of 64 bytes in physical memory (16 big-endian dwords of bit fields, Xenia's `XMA_CONTEXT_DATA`), a worker that runs a pass over each enabled context - DAORecomp's real decoder (`DAORecomp/apu/xma_decoder.cpp`, a port of Xenia's `XmaContextNew`, compiled unmodified behind `dao_compat/`) over FFmpeg's `xmaframes` codec, decoding input packets into the output ring and keeping the offsets, valid bits and loop state (session 32; the silent pass, Xenia's `XmaContextFake`, stands in only if FFmpeg lacks the codec). `XMACreateContext` as a stub made FMOD unregister its render client 43 ms after registering it |
| `NtCreateMutant`, `NtReleaseMutant` | `warden_threads.cc`; a recursive mutant (`GuestMutant`, owner = a per-thread identity), waitable like every other object; `STATUS_MUTANT_NOT_OWNED` from a thread that does not hold it |
| `NtFreeVirtualMemory` | `warden_guest_heap.cc`; accepted and counted, the range kept - this heap does not reuse address space. The stub's failure read as heap corruption |
| `RtlFillMemoryUlong` | `warden_imports_kernel.cc`; length / 4 big-endian words |
| `RtlTimeToTimeFields`, `RtlTimeFieldsToTime` | `warden_imports_kernel.cc`; a FILETIME (100 ns since 1601, `KeQuerySystemTime`'s clock) to and from the console's `TIME_FIELDS` - eight big-endian u16s: year, month, day, hour, minute, second, milliseconds, weekday (Sunday 0) - in UTC, by calendar arithmetic as Xenia's do; an impossible date is FALSE. Reached when saves are listed, loaded and written: the title writes the save time into each save's display name and sorts the lists by it. The stubs had left every save's time garbage (BUG-048) |
| `VdInitializeScalerCommandBuffer`, `VdPersistDisplay`, `VdSetDisplayMode`, `VdGetCurrentDisplayInformation` | `warden_video.cc`; the scaler buffer is filled with type-2 no-op packets (twelve arguments: the destination and count are on the stack at `r1 + 0x64` and `+0x6C`), persist hands out a 64-byte physical block, the mode is accepted, the display information is DAORecomp's 0x58-byte layout for 1280x720 at 60 Hz |
| `XMsgStartIORequest`, `XMsgStartIORequestEx`, `XMsgInProcessCall` | `warden_xam.cc`; the music player app (`0xFA`) messages answered as DAORecomp does (`0x00070009`, `0x0007000B`, `0x0007001B`, `0x00070029`: no title music, nothing playing); an `X_OVERLAPPED` is completed in place (result, length, extended) and its event signalled through the kernel objects, the call returning `ERROR_IO_PENDING` (0x3E5) as the console does |
| `XamUserGetSigninState`, `XamUserGetXUID`, `XamUserGetSigninInfo` | `warden_xam.cc`; user 0 is signed in locally (state 1) as "Warden" with XUID `B13EBABEBABEBABE`, the other three slots are empty. This is what took the title from its sign-in screen to the title screen |
| `XGetGameRegion`, `XamGetSystemVersion` | `0xFFFF` (every region) and `0x20446700` (dashboard 2.0.17489), DAORecomp's values |
| `XamNotifyCreateListener`, `XNotifyGetNext` | a manual-reset event that is never signalled, and "nothing queued" (0): the title polls both every frame and carries on |
| `InterlockedPushEntrySList`, `InterlockedPopEntrySList`, `InterlockedFlushSList` | `warden_imports_kernel.cc`; the console's 8-byte big-endian `SLIST_HEADER` (first entry +0, depth +4, sequence +6) updated with a 64-bit compare-and-swap on its raw bytes, as Xenia does, so the kernel's calls stay atomic against the copies the title inlines. Reached when the character stage loads; the stub's null would have been an empty list that was not |
| `MmSetAddressProtect` | accepted and not applied (the GPU's write watch owns the physical pages' protection); logged four times |
| `XexGetModuleHandle`, `XexGetProcedureAddress` | `warden_imports_kernel.cc`; `default.xex` (or no name) is this executable and gets a stable token (`0x0DEF0001`, nothing dereferences it), any other module - the title asks for `xam.xex` at its title screen - is `STATUS_NO_SUCH_FILE`; procedures are `STATUS_NOT_IMPLEMENTED` with the ordinal or name logged, as DAORecomp |
| `XamContentGetDeviceData`, `XamContentGetDeviceState` | `warden_xam.cc`; one storage device, id 1: a 20 GB "Hard Drive" with 16 GB free, connected; any other id is `ERROR_DEVICE_NOT_CONNECTED` (DAORecomp's answers) |
| `XamUserGetName`, `XamUserCheckPrivilege`, `XamUserAreUsersFriends`, `XamUserGetMembershipTierFromXUID`, `XamUserGetOnlineCountryFromXUID` | `warden_xam.cc`; "Warden"; **no privilege is granted** - a locally signed-in profile has no Live privileges and this app has no online functionality (DAORecomp grants them all; each privilege asked for is logged once, in case a feature the title should have offline sits behind one); nobody is a friend; tier 0, country 103 (US) |
| `XamUserReadProfileSettings`, `XamUserWriteProfileSettings` | `warden_xam.cc`; the gamer profile settings as 40-byte `X_USER_PROFILE_SETTING` entries (layout in the source); the well-known ones default as DAORecomp's do (vibration on, sensitivity medium, auto-aim and auto-centre on, the rest 0), and what the title writes is kept per setting id as raw bytes under `files/profile/<id>.bin`, written through, so its options survive a restart. The read's overlapped is the ninth argument, on the stack at `r1 + 0x54` |
| `XamContentCreateEx`, `XamContentClose` | `warden_xam.cc`; a save is a folder, `files/content/<type, 8 hex digits>/<the title's file name>` (`00000001/0xd5332a7_1_u!0` for the first save), mounted as the title's root (`save:`) until `XamContentClose` unmounts it. Nine arguments, the overlapped on the stack at `r1 + 0x54`. The low four bits of the flags are the disposition (DAORecomp's reading): 1 create new, 2 create always, 3 open existing, 4 open always, 5 truncate existing; replaced content is moved to `<type>/.backup/<name>` first, so the previous save survives a write that never completes. The 256-byte display name is kept raw beside the folder as `<name>.name`, written for new content only. A save, manual or auto: flags `0x13` (not found), then `0x12` (created), six files, close; over an existing save `0x12` again, the old folder moved to `.backup/` first (run 203). **A load** (runs 202-203): `0x13`, disposition 2, then `savegame.das.x` and `savegame.das`. Folder names: `0xd5332a7_<n>_u!0` is "Save (n)", `_<n>_a!0` "Auto-save (n)" |
| `XamContentCreateEnumerator`, `XamContentGetCreator` | `warden_xam.cc`; the content of a type on the device as 0x134-byte `XCONTENT_DATA` items: one per folder under `files/content/<type>/`, sorted, with the hard drive's device id, the type, the display name from `<name>.name` and the folder's name as the file name, as DAORecomp lists its saves folder; every piece of content is the signed-in user's. Type 2 skips the packages `files/dlc-off` names (mods turned off, PLAN 10.19); `files/hide-dlc` hides all but the mods' `mod-<id>` packages |
| `XamUserCreateAchievementEnumerator`, `XamEnumerate` | `warden_xam.cc`; a `GuestEnumerator` kernel object (`warden_objects.h`) holding fixed-size items - none, for achievements (0x1C-byte details) - that `XamEnumerate` pages out: `ERROR_NO_MORE_FILES` synchronously, or `ERROR_FUNCTION_FAILED` with the extended error through an overlapped |
| `XamShowDeviceSelectorUI`, `XamShowSigninUI`, `XamShowMessageBoxUI`, `XamShowMessageBoxUIEx`, `XamShowDirtyDiscErrorUI`, `XamShowAchievementsUI`, `XamShowMarketplaceUI`, `XamShowKeyboardUI` | `warden_xam.cc`; the system blades answered without being shown: the hard drive is chosen (the title asks when the player answers "Yes" to its own "You have not selected a storage device" prompt), a message box presses its focused button (text logged; the box's overlapped is the ninth argument, on the stack at `r1 + 0x54`), sign-in is done, the dirty-disc error logs and returns instead of exiting, the marketplace shows nothing - no online functionality. **`XamShowKeyboardUI` is real** (run 193, `warden_keyboard.h`): it files the title's prompt, description and default text, returns `ERROR_IO_PENDING`, and the app shows an Android text dialog with them and the system keyboard (`GameActivity` polls `NativeBridge.keyboardRequest()` and answers with `keyboardAnswer(text, accepted)`); the answer, UTF-16 big-endian, completes the title's overlapped. A `files/keyboard-default` marker brings back the old answer (the default text, at once) for unattended runs |

`KeTlsGetValue` is implemented although the title had not reached it: a title that sets
a TLS value reads it back, and the weak stub would have returned 0 for a value that had
been set — a wrong answer rather than a missing one.

**`MmQueryStatistics` was left as a stub at first**, for want of a documented layout,
on the reasoning that plausible numbers in the wrong fields would be worse than a stub
that logs. The reasoning missed that the stub was not neutral: returning 0 told the
caller the call had *succeeded* and handed it an all-zero structure, so the title's
allocator sized itself to nothing and the first `operator new` returned null - the
startup crash 5.1 chased for several sessions. The layout was in
`third_party/xenia-canary/src/xenia/kernel/xboxkrnl/xboxkrnl_memory.h` all along, and
the title confirms it by writing 104 into `size` before the call.

**The critical sections keep their lock host-side**, keyed on the guest pointer, because
the console's `RTL_CRITICAL_SECTION` layout is not documented here either. Correct for a
title that only passes the pointer back to these functions; wrong for one that reads the
structure's fields — a recursion count, an owning thread id. If locking misbehaves, that
is the first thing to suspect, and the fix is to find the real layout.

**The diagnostic logs are not flushed per line any more.** Xenia's logger buffers, and a
line still buffered when the process dies is lost — one run appeared to show the
allocator never being called when its log simply had not been written — so the first
answer was `xe::FlushLog()` after every line. But that call sleeps 10 ms, which made
each guest thread's pace depend on how much it logged (BUG-034). The fault handler now
drains the logger once before its report; do not add per-line flushes to imports.

## 5. Related

- [`PLAN.md`](PLAN.md) — Phase 5 tasks
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — where the host boundary sits
- `app/src/main/cpp/warden_imports.cc` — the real implementations
- `build/gen/guest_import_stubs.cc` — generated, not committed
