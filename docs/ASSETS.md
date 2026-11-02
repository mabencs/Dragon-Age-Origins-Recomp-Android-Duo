# Game assets — verification, formats, pipeline

Reference for the disc images, the title updates, and the file formats inside them.
Everything here was established by direct inspection on **2026-10-22**, not from
documentation. Re-verify any time with:

```bash
python tools/verify_assets.py .
```

**No game content is committed.** `.gitignore` excludes every format listed here and
that exclusion is verified with `git check-ignore`.

---

## 1. Verification results

### Disc images

| | Origins | Awakening |
|---|---|---|
| File size | 7,838,695,424 | 7,838,695,424 |
| Format | XGD2 | XGD2 |
| Game partition base | `0xFD90000` | `0xFD90000` |
| `default.xex` size | 19,021,824 | 20,037,632 |
| Title ID | `454108C0` | `454108C0` |
| **Media ID** | **`186A1115`** | **`2E6F7560`** |
| XEX version | 0.0.0.1 | 0.0.0.9 |
| Disc | 1 of 1 | 1 of 1 |
| **Verdict** | **Build target** | **Archived — see below** |

Both are genuine XGD2 Redump-size images with valid `MICROSOFT*XBOX*MEDIA` volume
descriptors. The Origins disc's media ID matches exactly what the recompiler requires.

**Why Awakening is archived:** same title ID but a **different media ID and a different
executable**. The recompiler's function database and patch set are built against one
specific executable and will not match Awakening's. See decision
[D2](DECISIONS.md#d2--base-game-plus-tu7-only-awakening-deferred).

### Title updates

Both are Xbox 360 **STFS** containers with the `LIVE` signature and content type
`000B0000` (title update).

| File | Display name | Title ID | Media ID | Verdict |
|---|---|---|---|---|
| `TU_..._00000000001O1` | Dragon Age: Origins Title Update **#7** | `454108C0` | `186A1115` | **Build target** (optional for a player, recommended) |
| `title_update.bin` on the Ultimate Edition's disc 2 (PIRS) | Dragon Age: Origins Title Update **#6** | `454108C0` | `186A1115` | **Build target too** (PLAN 12.3): the app takes it from disc 2 |
| `TU_..._00000000000G1` | Dragon Age: Origins Title Update **#2** | `454108C0` | `186A1115` | Keep, unused |

TU7 is the last Dragon Age: Origins title update. TU6 already loads the DLC campaigns -
DAORecomp's one TU config takes either (`config/DAO_tu6.toml`), and its README names TU6
or TU7 for Awakening and the DLC - so either is enough (PLAN 10.10, 12.3).

The display name is stored as **UTF-16 big-endian at offset `0x411`**, which is how the
TU number was read. Useful STFS offsets:

| Offset | Field |
|---|---|
| `0x000` | Magic — `LIVE`, `PIRS` or `CON ` |
| `0x344` | Content type |
| `0x354` | Media ID |
| `0x360` | Title ID |
| `0x411` | Display name, UTF-16BE |

---

## 2. Disc layout

### Origins (the build target)

```
/
├── $SystemUpdate/        system update blob, not needed
├── default.xex           19,021,824   ← the recompilation input
├── modules/
│   └── single player/
│       ├── audio/{sound,vo}/
│       ├── data/         ~245 MB, 224 .rim area files
│       ├── locale/{es-es,it-it}/
│       └── singleplayer.cif
├── packages/
│   └── core/
│       ├── audio/{sound,vo}/
│       ├── data/         ~771 MB — the main content set
│       ├── data_tools/
│       └── env/          several hundred per-area environment dirs
├── nxeart                1,200,128   dashboard art
└── splash.dds            460,928
```

### `packages/core/data` — the main content set

The notable archives:

| File | Size | Contains |
|---|---|---|
| `models.erf` | 643,293,519 | All models — by far the largest single file |
| `face.erf` | 35,362,717 | Head/face models |
| `chargen.gpu.rim` | 25,858,443 | Character creation GPU resources |
| **`guiexport.erf`** | **18,591,364** | **Exported UI art — glyph work lives here** |
| **`gui.erf`** | **17,035,033** | **UI definitions** |
| `global.rim` | 13,082,772 | Global resources |
| `2da.erf` | 548,000 | 755 game data tables (GDA) |
| `shaders.erf` | 534,344 | Shaders |
| `designerscripts.rim` | 3,140,068 | Scripts |
| `fonts.erf` | 1,118 | Font references |

### Awakening (archived)

Differs structurally in two interesting ways:

```
/
├── Addins/
│   └── DAO_PRC_EP_1/           ← Awakening as a standard DAO addin
│       ├── manifest.xml  12,250
│       ├── core/{data,env}/
│       ├── module/   dao_prc_ep_1.cif
│       └── packages/core_ep1/
└── packages/core/
    ├── locale/
    └── patch/                  ← overridable game data (!)
        ├── 2DA_base.GDA                     25,022
        ├── Achievements.GDA                 26,198
        ├── DesignerTags.GDA                  7,990
        ├── M2DA_base.GDA                    28,846
        ├── TrainingSteps.GDA                 3,276
        ├── X360_ControllerInteraction.GDA    1,280   ← the glyph table
        └── coreupdate_<locale>.tlk           × 7
```

Two findings that matter even though the disc is archived:

1. **`Addins/DAO_PRC_EP_1`** — Awakening uses the same addin mechanism as the PC
   release. This is the basis for the deferred Q5 route.
2. **`packages/core/patch/`** — the engine supports a patch overlay of game data, and
   `X360_ControllerInteraction.GDA` sits in it. Controller interaction mapping is
   therefore **overridable data, not compiled-in constants**. This is the single most
   important finding for the glyph feature.

Awakening also ships an expanded `guiexport.erf` (22,305,125 vs 18,591,364) while
`gui.erf` is byte-identical in size.

---

## 3. File formats

### ERF V2.1 — content archives

**The Xbox 360 build differs from the PC release.** PC uses ERF V2.0 with UTF-16 names;
the 360 uses **`ERF V2.1`** with little-endian fields and 32-byte ASCII names.
**PC-oriented DAO tools will not read these.** `tools/dao_erf.py` does - and, since
session 40, the ERF V2.0 archives of the downloadable content's packages (PLAN 10.10),
which are the PC release's layout (a UTF-16 signature, 72-byte entries with 32-character
UTF-16 names, nothing compressed), so `ls` and `extract` work on a DLC module's
`designerscripts.erf` or `designerareas.erf` pulled from the Thor
(`files/content/00000002/<package>/<module>/module/data/`).

```
0x00  char[8]    "ERF V2.1"
0x08  byte[8]    reserved
0x10  uint32     file count          (little-endian)
0x14  uint32     reserved
0x18  uint16     build day / misc
0x1C  uint32     0xFFFF0000
0x20  entries[]  44 bytes each:
        char[32]  name, ASCII, NUL-padded
        uint32    offset
        uint32    packed length
        uint32    unpacked length    (!= packed  =>  zlib stream)
```

Some entries are raw deflate without a zlib wrapper; the tool falls back to
`-MAX_WBITS` automatically.

### Scaleform GFx — the UI

`.gfx` files are **`CFX`-signature zlib containers**:

```
0x00  char[4]   "CFX\x08"
0x04  uint32    uncompressed length
0x08  ...       zlib stream (0x78 0xDA)
```

Decompressing reveals a SWF-like tag structure with ActionScript symbol names. This is
how the glyph MovieClips (`AButton_mc` and friends) were found.

### Texture atlases

UI bitmaps are atlased. Each atlas group is:

- `atl_<name>_dxt{1,5}_NN.xds` — atlas pages (DXT1 or DXT5 compressed)
- `atl_<name>_dxt{1,5}_dat.xml` — **plain-XML layout descriptor**

```xml
<AtlasData>
  <AtlasTexture Name="atl_Tutorial_dxt5_00.dds">
    <SourceTexture Name="Tutorial_I3E.dds"
                   OffsetAndScale="0.789063 0.058594 0.023438 0.070313"
                   AtlasSize="1024 512" OriginalImageSize="24 36"/>
```

`OffsetAndScale` is a normalised UV rect: `u v du dv`. Source names are opaque
Scaleform symbol IDs (`I3E` is hex), not semantic names — which is why identifying the
glyph cells needs frame captures as well as the XML (task 7.1).

37 atlas groups exist in `guiexport.erf`. Dump them all:

```bash
python tools/dao_erf.py atlas guiexport.erf
```

### GFF4 / GDA — game data tables

```
0x00  char[4]  "GFF "
0x04  char[4]  "V4.0"
0x08  char[4]  "X360"      platform — implies BIG-ENDIAN fields
0x0C  char[4]  "G2DA"      generic 2-D array
0x10  char[4]  "V0.2"
0x14  uint32   struct count
0x18  uint32   data offset
0x1C  struct definitions: char[4] type, uint32 field count,
                          uint32 field offset, uint32 struct size
      field definitions:  uint32 label, uint32 type, uint32 offset
```

Note the **`X360` platform marker means big-endian**, unlike the little-endian ERF
container holding it. Field labels are numeric, not strings, so column semantics have to
be cross-referenced against the base game's `2da.erf`.

`X360_ControllerInteraction.GDA` parses as: 3 structs (`gtop` 2 fields, `colm` 2 fields,
`rows` 19 fields with a 76-byte stride), field labels `0x2715`–`0x2727`.

### Other formats on disc

| Extension | Meaning |
|---|---|
| `.rim` | Resource image — like ERF, used for areas and bulk resources |
| `.gpu.rim` | GPU-resident resources |
| `.erf` | Encapsulated resource file |
| `.xds` | Xbox 360 texture (DXT-compressed) |
| `.tlk` | Talk table — localised strings |
| `.gda` | Game data array (GFF4 table) |
| `.cif` | Module/campaign definition |
| `.gfx` | Scaleform Flash UI |

---

## 4. Extraction pipeline

### Host side, one time (Phase 1)

```bash
# 1. Confirm the editions first — don't skip this
python tools/verify_assets.py .

# 2. Pull the recompilation input
python tools/dao_iso.py extract "Dragon Age - Origins (World) (En,Es,It).iso" \
    default.xex work/default.xex

# 3. Apply TU7 to produce the patched executable (Phase 1.2)

# 4. Pull the game data the runtime needs
python tools/dao_iso.py pull "Dragon Age - Origins (World) (En,Es,It).iso" \
    packages work/game/packages
python tools/dao_iso.py pull "Dragon Age - Origins (World) (En,Es,It).iso" \
    modules work/game/modules
```

`work/` is gitignored.

### How big the extraction actually is

**Measured in Phase 4.4, not estimated.** This document previously said "roughly 20 GB",
which was out by a factor of three:

| Entry | Bytes | Files |
|---|---|---|
| `modules/` | 4,025,577,252 | 1,658 |
| `packages/` | 2,812,884,639 | 7,789 |
| `default.xex` | 19,021,824 | 1 |
| `nxeart`, `splash.dds` | 1,661,056 | 2 |
| `$SystemUpdate/` | 7,915,844 | 2 — **skipped** |
| **Extracted total** | **6,859,144,771** | **9,450** |

That is **6.39 GiB**, against a 7.30 GiB disc image. `$SystemUpdate/` is the console's
own system-update payload and is not game data.

Reproduce with `python tools/check_xdvdfs.py --all`, which also checks the native
reader against the Python one — 9,668 listing lines identical on Origins and 12,610 on
Awakening.

### Device side (Phase 4.4)

Extract to app storage on first run, with progress UI and resumability.
Implemented in `app/src/main/cpp/warden_extract.cc`.

**Do not read from the ISO at runtime.** Seek latency plus Storage Access Framework
overhead make it a bad idea — extract once, then use plain file I/O.

The SAF part deserves a note, because the usual warning about it is half the story: it
is only slow if every read goes through a `ContentResolver`. `openFileDescriptor`
returns a real, seekable file descriptor, and once native code has that it uses `pread`
like any other file. So the framework is in the way for exactly one call.

Destination is `getExternalFilesDir(null)/gamedata` — app-scoped, on the 942 GB volume,
no permission needed. Device has 209 GB free, so 6.4 GiB is comfortable.

**Resumability** works by writing each file under a `.part` name and renaming it on
completion. A destination file that exists with exactly the right size was therefore
renamed into place by a finished write, so it can be skipped. Without the rename, a run
interrupted mid-file would leave a short file at the real name, and the resume check
could not tell "short because interrupted" from "short because that is its size".

### `default.xex` - what loading it actually requires

Measured from the disc in Phase 5, because an earlier note here guessed and guessed
partly wrong. Read the headers with `python tools/dao_iso.py info <image>`; the fields
below come from the XEX2 optional header table.

| Field | Value |
|---|---|
| Entry point | **`0x829E8698`** |
| Image base | `0x82000000` — agrees with `PPC_IMAGE_BASE` in the recompiled code |
| Default stack size | `0x40000` (256 KB) |
| Title workspace | `0x180000` (1,536 KB) |
| Encryption | **AES-128-CBC** (`encryption type 1`) |
| Compression | **raw block list** (`compression type 1`) — **not** LZX |

The compression field is the correction. The image is *not* LZX-compressed: type 1 is a
plain block list, four blocks totalling **19,005,440 bytes of data plus 753,664 bytes
of zero-fill**, which accounts exactly for `default.xex`'s 19,021,824 bytes minus the
0x4000 header. So loading it is decrypt-and-assemble, with no decompressor to write.

Note the header key for this is `0x000003FF`, not `0x00000003` — XEX2 header keys carry
their data size in the low byte, so looking for the bare id finds nothing and makes the
section look absent when it is present.

### Verifying a decrypted image without a disassembler

`python tools/check_xex.py` settles whether the loader got it right, and the method is
worth recording because the failure mode is nasty: a wrong key, a mis-walked header
offset or botched block arithmetic does not produce an error, it produces **noise**,
and noise is indistinguishable from code until something executes it.

XenonRecomp emits each original instruction as a comment above the translated one:

```cpp
PPC_FUNC_IMPL(__imp__sub_82210008) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    // mflr r12            <- the first instruction, at 0x82210008
```

Thousands of functions begin with `mflr r12`. Whatever that encodes to, **every one of
those addresses must hold the same four bytes** — so grouping addresses by their
instruction text and requiring agreement within each group tests the whole pipeline,
with no opcode table and no disassembler. Relative branches are excluded, since their
encoding legitimately differs by address.

Result against the base recompilation: **839 groups with two or more sites, all 839
agreeing**, `mflr r12` encoding to `7D8802A6` at 36,403 sites.

### The recompilation and the image must be the same generation

`recomp/generated/ppc_tu6` (the developer's build) was recompiled from the **Title Update
7 patched** image, not from the disc's `default.xex`; on a player's device the game's code
is made from whichever title update is in place, and the image is patched with the same
one (`FindTitleUpdate` picks it by the game library's image size). So loading the base executable under that build gives
code that mostly-but-not-quite matches — which is how it was caught, because partial
disagreement is something AES-CBC cannot produce. A wrong key gives noise everywhere; a
wrong *generation* gives 16,043 distinct encodings where there should be one.

The app checks this itself at startup, using the function mapping table it already
links: read the first word at each of the 53,108 entry points and count how many are
`mflr r12`. A matched pair scores about 71%; the base image under the TU7 build scores
**1.8%**.

Applying the patch needs `title_update/default.xexp` — the same file the overlay below
comes from. XenonRecomp's `XexPatcher::apply` does the work and is self-contained but
for `lzxDecompress` (in the same file, needing libmspack's `lzxd.c`); both compile for
Android, so this can be done on-device once the `.xexp` is there.

### The title update, on the device

**Done in Phase 5.0f.** The app reads the title update out of its LIVE package itself,
so the only thing asked of the user is to put the file somewhere the app can see:

| Where | Notes |
|---|---|
| `getExternalFilesDir()` | app-owned, no permission, reachable over adb or MTP |
| beside the extracted game data, or its parent | also searched |
| a loose `title_update/default.xexp` | checked first — cheaper, and what a host-side extraction leaves |

The package is read by `app/src/main/cpp/warden_stfs.cc`, a port of DAORecomp's
`scripts/stfs.py`, verified against it byte-for-byte by `python tools/check_stfs.py`:
26 files, 3,370,037 bytes, every path, size and byte identical.

A candidate must be `default.xexp` at exactly **2,758,656 bytes** (TU7) or **2,684,928
bytes** (TU6), and the making checks its SHA-1 (`dd5b493f...` or `4141b608...`). Title
Update 2 is also in circulation and is **not** usable — the title itself passes over
anything that is not TU6's or TU7's — so the patch is identified rather than taken on
trust. Disc 2's Title Update 6 is copied out of the disc image into the app's files when
disc 2 is installed (`title_update_disc2.bin`), and found there like a TU7 package.

The patch is applied with XenonRecomp's own `XexPatcher`, because that is what produced
`recomp/generated/ppc_tu6`; an image patched any other way could disagree with the code
translated from it. `tools/gen_xex_patcher.py` trims its one Win32-only overload.

**The patched executable is a different shape from the base one**, which is why the
loader handles both:

| | Base (disc) | Patched (TU7) |
|---|---|---|
| Entry point | `0x829E8698` | **`0x82AB2650`** |
| Encryption | AES-128-CBC | **none** |
| Compression | basic, 4 blocks | **none, 1 block** |
| Assembled size | 19,759,104 | 20,971,520 |

Verified end to end: `python tools/check_xex.py --build ppc_tu6 --patch` gives **843 of
843 instruction groups agreeing**, and on the device the app reports **70.5%** of its
53,108 entry points starting with `mflr r12` against the 1.8% the unpatched image gave.

### The patch overlay's data files — done on the device

Separate from the executable patch above, TU7 also carries **25 data files** under
`packages/core/patch/` — GDA tables, `.tlk` strings, a few plot and item files, 3.3 MB
in all. Those take precedence over the base ERFs in the engine's own lookup order
(below), so the game needs them next to the disc extraction, not only inside the
package.

**Done since Phase 5.0f** (2026-10-23): `WriteTitleUpdateOverlay` in
`app/src/main/cpp/warden_boot.cc` writes them into the game data directory straight
from the package, while it is open and verified for the executable patch. Idempotent —
a file already there at the right size is skipped. Checked against the host extraction:
every path and size identical. (This section said "still to do" until 2026-10-25,
written when the overlay was copied in separately; the STFS reader was the missing
piece, and it has been verified byte for byte against `stfs.py` since session 15.)

### Asset override folder

A loose-file directory taking precedence over the ERF archives. Used by both the glyph
system (7.3) and texture packs (9.9). Lookup order:

```
1. override folder (loose files)      ← our glyph packs, texture packs
2. packages/core/patch/               ← the engine's own patch overlay
3. addins                             ← future Awakening route
4. packages/core/data/*.erf           ← base game content
```

This mirrors the engine's own precedence, which is why `patch/` existing on the
Awakening disc is such a useful signal.

---

## 5. Tooling

| Tool | Purpose |
|---|---|
| `tools/verify_assets.py` | Check discs and title updates are the right editions |
| `tools/dao_iso.py info` | Disc identity — media ID, title ID, version |
| `tools/dao_iso.py ls` | List disc contents (`-r` for recursive) |
| `tools/dao_iso.py extract` | Extract one file |
| `tools/dao_iso.py pull` | Extract a directory tree |
| `tools/dao_erf.py ls` | List archive contents (`--filter` to narrow) |
| `tools/dao_erf.py extract` | Extract and decompress entries |
| `tools/dao_erf.py atlas` | Dump all atlas layouts — glyph hunting |

All three were written and verified against the real files on 2026-10-22.
