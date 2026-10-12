# cm3d2.dll source reconstruction

This is an evidence-led, clean reconstruction started from the native x64 DLL at
`E:\COM3D2Test 2.5\COM3D2_5\COM3D2x64_Data\Plugins\cm3d2.dll`.
It is inferred source code, not recovered original source.

Current coverage is 215 named exports (every export of the original DLL) across the file-object, filesystem-core,
CSV-parser, EnumData, AnmParse, FileSystemDelegate, TJSVariant, OGG, HoleDevice,
TJS script-wrapper, KAG scenario interpreter, and archive file-system boundaries. It includes the common file interface, semantic
implementations of the 0x20-byte `Win32File` and 0x38-byte `FileMemory` objects,
filesystem handle layout, loose-file search paths, existence/file retrieval,
lifecycle, and the decoded WCSV cell table with string, binary, integer, real,
and boolean access. `DLL_CSV_Open` also decrypts the game's AES-CBC carrier using
the recovered static key and XSHIFT-derived IV. An internal menu carrier decoder
and UTF-16LE native filename hash are also reconstructed, along with all 28 menu
exports. See `docs/menu-reconstruction.md` and the latest `COLLABORATION.md` entries
for compatibility limits.
The full binary exposes 212 named internal
functions across filesystem, archive, menu, CSV, animation, KAG, TJS, OGG, and
device groups. The catalog also retains six exported aliases; see
`docs/exports.csv` for the working inventory.

## Third-party code

`third_party/stb/stb_vorbis.c` (Sean Barrett, v1.22) is the embedded Ogg Vorbis decoder. It is dual-licensed MIT or public domain; the full notice is at the end of the file.

`third_party/tjs2` is Claude's vendored Kirikiri Z TJS2 core, self-identifying as
2.4.28, with its license retained. It substitutes for the native VM, rather than
being recovered original source; its download URL/commit is not recorded.
See `third_party/tjs2/RECONSTRUCTION-NOTES.md` for build adaptations.

## Build

Use a Visual Studio x64 developer prompt:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

Validated on October 11, 2026 with CMake 4.4.4, Visual Studio 18 2026,
MSVC 19.51.36260, and Windows SDK 10.0.26100. Both Debug and RelWithDebInfo
builds complete; a KAG vendor-header size-conversion warning remains on a full rebuild.
Latest run: 22 tests pass and the optional warp-fixture test is skipped in each
configuration (23 registered tests; OGG/archive fixtures supplied).
The resulting DLL is x64 and exposes all 215 reconstructed names at their observed ordinals
(`node tools/check-def-ordinals.mjs <original dll> exports/cm3d2_file.def` re-verifies them).

The CSV tests exercise both direct decoded tables and a controlled encrypted
carrier. Real NEI and archive-member checks have also been run against the
original DLL. Set `CM3D2_WARC_SAMPLE` to the tested DLC `GameData\csv.arc` for the
8 x 24 `edit_bg.nei` fixture, and `CM3D2_OGG_SAMPLE` to a supplied Ogg file to run
the optional tests; CTest reports these as skipped when fixtures are absent.

OGG accepts reconstructed Windows/archive wide interfaces and delegate callbacks.
For backward compatibility, a null filesystem also allows direct disk reads
(a deliberate normalization); a failed supplied provider never falls back to disk.
The substituted decoder is not bit-identical to the original: the reviewed
`4_battle_win.ogg` differs by one 16-bit unit on 59 of 322,298 samples.

HoleDevice state/lifecycle probes match the original without the device attached.
HID/DirectInput hardware behavior remains unverified. Discovery failures and
resource cleanup are hardened; report reads use one outstanding request with
cancel-and-drain on stop. These are deliberate safety normalizations, not claims
of reproducing the original's error handling. Serialize device discovery,
frame updates, and lifecycle calls on the owning thread.

TJS has all 13 wrapper exports registered and a 40-line differential probe matching
the original in both builds. This is partial compatibility, not a full engine decompilation:
The native exposed engine does not register RegExp; our disabled setting matches
that path. Error VM register dumps differ. File serialization is unavailable in
the exposed original too: Dictionary load/save are no-ops, other stream paths
use null streams. No additional file support was enabled.
Native Dictionary.toStructString/getKeyArray are now exposed to scripts; a 34-line
probe matches both builds. See `docs/tjs-builtins-reconstruction.md` for limits.
Native sorting quirks, normal large-array paths and comparator counts are
reproduced: 660 small and 1,116 isolated large-array cases match both builds.
Reduced-memory fallback and native failure/reuse behavior remain unverified;
see `docs/tjs-array-sort-reconstruction.md`.
Object/closure/octet host payloads and live callback argument/result views are now
preserved, with a separate 96-line native differential probe. Release owned script
payloads before deleting the engine. Explicitly invalidate script-finalizer objects
while globals/callbacks are alive: native teardown has confirmed fatal cases.
Seventeen safe finalizer cases match both builds; retained host dispatches are
lifetime-guarded as deliberate hardening. Arbitrary teardown remains unsupported;
see `docs/tjs-finalizer-reconstruction.md`.
Array split/join and safe serialization preconditions match in a 128-case probe;
see `docs/tjs-array-text-reconstruction.md` for native quirks and scope.
Dictionary assignment hash-order differences are fixed;114 assignment cases
match native in both builds. See `docs/tjs-assign-reconstruction.md`.
The native `Scripts` class now provides exec/eval and storage methods using the
borrowed filesystem; its separate 83-line probe matches after pointer normalization.
See `docs/tjs-reconstruction.md` for evidence, supported behavior, and the next steps.

## Evidence

- `docs/evidence-ledger.md` maps claims to REA evidence IDs.
- `rea-evidence.json` contains the initial file-object evidence bundle.
- `rea-filesystem-evidence.json` contains the filesystem-core evidence bundle.
- `rea-csv-evidence.json` contains the CSV-wrapper, vtable, parser, and numeric
  conversion evidence bundle.
- `rea-menu-evidence.json` contains the menu exports, decoder, worker, and record
  layout evidence. `docs/menu-reconstruction.md` records the unresolved ABI work.
- `rea-anm-enum-evidence.json` contains the EnumData and AnmParse evidence.
- `rea-review-ogg-evidence.json` contains the native OGG open/provider dispatch
  evidence used to fix callback-backed playback during the collaboration review.
- `rea-review-hole-evidence.json` contains the HoleDevice discovery, reader,
  stop, creation/deletion, and DirectInput evidence used in the second review.
- `rea-tjs-wrapper-evidence.json` contains the TJS wrapper, callbacks, file loader,
  console, and exception evidence; `rea-tjs-messages-evidence.json` contains the
  native message-holder initializers and literal defaults.
- `rea-tjs-scripts-evidence.json` retains the Scripts class registration, four
  methods, filesystem getter, native-instance support, and confirming assembly.
- `rea-tjs-payload-evidence.json` retains variant reference/copy/release and
  conversion evidence; see `docs/tjs-payload-reconstruction.md` for ownership limits.
- `rea-tjs-builtins-evidence.json` retains builtin registration, compact Dictionary
  serialization, key enumeration, and native stream-path evidence.
- `rea-tjs-array-sort-evidence.json` retains sort dispatch, comparison failures,
  small-array insertion helpers and callback receiver/boolean-conversion evidence.
- `rea-tjs-large-sort-evidence.json` retains normal large-array sorting helpers.
- `rea-tjs-finalizer-evidence.json` retains lifetime/teardown and native callback
  map destruction evidence; see `docs/tjs-finalizer-reconstruction.md`.
- `rea-tjs-array-text-evidence.json` retains split/join, string append and boolean
  conversion evidence; the stream audit reuses `rea-tjs-builtins-evidence.json`.
- `COLLABORATION.md` is the shared handoff log and status board; read it first.
- `tools/export-inventory.ps1` regenerates the export catalog from that bundle.

The `.def` file preserves the observed names and ordinals for the reconstructed
slice, including the original `CreateileMemory`/`DeleteileMemory` spellings.

## Deployment
The reconstructed DLL is linked with the static C/C++ runtime so that, like the original, it needs only system DLLs (KERNEL32, BCRYPT, SETUPAPI, HID, DINPUT8). Check with `dumpbin /dependents cm3d2_reconstructed.dll` after changing dependencies.
