# Differential checks against the original DLL

## Synthetic archive filesystem battery

`fs_archive_battery.cpp` / `fs_archive_battery.ps1`: four freshly generated plain
WARC fixtures only, three mount/auto-path order scenarios. CMake target
`fs_archive_battery`; the runner checks the original SHA and the probe has a
20-second watchdog. Review and command: `docs/archive-filesystem-order-review.md`.
2026-10-12: 841 transcript lines identical, all six runs exit 0. Wide opens are
checked through KAG/menu consumers; native vtable layout is not claimed.
`file_system_archive_synthetic_contract` retains focused regression expectations
without requiring original/game files.

## Enabled auto-path job probes

`fs_job_probe` / `fs_job_internal_probe`: Windows loose-tree jobs. Arguments:
`<original.dll.orig|internal> <async 0/1> <directories 0..16> <preset 0/1>`.
The original duplicate-slot worker branch can hang; a five-second watchdog
bounds the process. `fs_archive_job_probe` / `fs_archive_job_internal_probe`:
arguments `<original.dll.orig|internal> <async 0/1> <preset 0/1>`, three generated
plain WARCs only. The native backend flips the original virtual-base mode flag;
the internal backend uses the source-level setter, not a reconstructed vtable.
Archive results: 86 identical lines across four cases, all eight exits 0.
Evidence/limits: `docs/filesystem-delegate-job-review.md`. Never substitute an
unknown DLL for the verified original when using the native-layout setter.

`diff_all.cpp` loads a cm3d2 DLL by path and runs one named battery against its
exports, printing one deterministic line per observation. Run it against the
original game DLL and against the reconstruction, then diff the two outputs.

Build (Visual Studio x64 developer prompt):

    cl /nologo /EHsc /W3 diff_all.cpp /Fe:diff_all.exe

Run (replace paths as needed):

    diff_all.exe "E:\COM3D2Test 2.5\COM3D2_5\COM3D2x64_Data\Plugins\cm3d2.dll" tjs   > orig_tjs.txt
    diff_all.exe "<build>\cm3d2_reconstructed.dll"                           tjs   > ours_tjs.txt
    diff orig_tjs.txt ours_tjs.txt

Batteries: `tjs`, `enum`, `anm`, `csv <nei path>`, `fsd`, `fsdvalid`.

Known divergences, deliberately excluded from the batteries:
- AnmParse truncated carriers: the original reads past the buffer. It accepts some
  prefixes as empty and crashes on others.
- AnmParse negative key counts: the original crashes.

Results on 2026-10-11 (all batteries): tjs 0/87, enum 0/22, anm 0/1376,
csv 0/194 (on the real `neiFiles/edit_bg.nei`), fsd 0/13. `fsdvalid` returns 1 on
both, so the earlier claim that `IsValid` aborts was wrong.

OGG: `ogg_oracle.cpp` runs the original OGG reader on a file through a delegate file system. Result: values match the reconstruction to within 1 LSB (0.03% of samples at most).

## winfs_probe.cpp
Drives the original (or reconstructed) Windows file system on a scratch directory tree:
`winfs_probe.exe <dll> <scratch root> <names file>`. Prints IsValid/AddFolder/AddArchive/AddAutoPath return codes and, for each
name, existence and file content. Used to confirm the FileSystemWindows reading of the pseudocode (0 differences, 32 lines).

## arch_probe.cpp, winlist_probe.cpp, cache_probe.cpp
- `arch_probe.exe <dll> <archive copy> <names file> [second archive copy]`: FileSystemArchive on scratch copies: AddArchive twice, AddAutoPath variants, every name (bare and path forms), CreateList in every mode, CreateExtensionList, GetNameMapLastArchive (order-sensitive hash). Reconstruction vs original: 1,577 lines, 0 differences (system.arc + system_gp003.arc).
- `winlist_probe.exe <dll> <scratch root>`: Windows list exports and the MSVC-ordered name map on a 128-file tree: 1,025 lines, 0 differences.
- `cache_probe.exe <dll> ser|de <scratch root> <cache> [names]`: CasheSerialize / CasheDeserialize. All four serialize/deserialize pairings of the original and the reconstruction behave the same, and the two serialized files hold identical content (only the order and one pointer field differ).

## menu_probe.cpp, extract_menus.mjs, gen_menus.mjs
- `menu_probe.exe <dll> win <folder>` / `menu_probe.exe <dll> arc <archive copy>`: builds a MenuDataBase over a Windows folder or an archive (scratch copy only), waits for the worker, then dumps every record through all 20 `DLL_MenuParse_*` getters plus the file/parent name lookups, an unknown hash and Delete.
- `extract_menus.mjs <archive copy> <out folder>` writes the `.menu` members of an archive to a folder; `gen_menus.mjs <out folder>` writes synthetic menus covering every interpreter command, the `_zurashi`/`_mekure` skips, the `_z` parent rule, an icon-less menu and two undecodable files.
- Results: 300 real menus, Windows mode 160 records / 360 lines, 0 differences; `parts2` archive copy 208 records / 472 lines, 0 differences; synthetic folder 9 records / 15 lines, 0 differences. The synthetic run found the carry-over of the category text (FUN_18010d610 does not reset state +0xf8), now reproduced.

## hole_probe.cpp
`hole_probe.exe <dll>`: HoleDevice state logic without hardware (Create, SetSensitivity over 10 values incl. NaN/inf, all getters, StartThread twice, StopThred twice, restart, Delete). 15 lines, 0 differences.

## kag_serialize_probe.cpp
`kag_serialize_probe.exe <dll>`: KAG record/serialization state (AddRecordLabel/GetRecordLabel, AddFinishFile/GetFinishFile, tag enable flags, CreateSerializeData, Deserialize, junk input) without any scenario. Hand-built payloads carry no parser state text because the original crashes restoring one without a scenario file system. Original output is the source of the golden bytes in tests/kag_serialize_contract.cpp.

## kag_exec_probe.cpp, kag_flow_probe.cpp, kag_misc_probe.cpp
- `kag_exec_probe.exe <dll>`: in-memory scenarios (text, [r], custom tag with attribute list, [eval] failure, macro, label jump, unknown tag). Original and rebuilt transcripts are identical.
- `kag_flow_probe.exe <dll> <scratch folder>`: file scenarios through the Windows file system: call/jump/return, r_call/r_return and its file-name rule, the replace callback and all six other callbacks (arguments logged), record labels, page labels, disabled tags. 181 lines, identical. Do not add `[r_call file=""]` (it crashes the original).
- `kag_misc_probe.exe <dll> <scratch folder>`: AddTag/RemoveTag semantics, call/return enable flags, unique ids, save and restore of a file scenario into a second object. Identical except the final Exec after a restore, where the original returns an uninitialized value.

## anm_real_probe.cpp
`anm_real_probe.exe <dll> <folder of .anm files> [max]`: opens each real animation file with the 8 combinations of the three flags and prints per curve the path, com id, frame count and an FNV hash of all key frame floats. Run on 350 files extracted (read-only) from motion.arc, motion2.arc and motion_gp003.arc: identical output from the original and the reconstruction (1.44M curves, 39.5M key frames). The extracted files stay in the scratch folder and are not part of the repository.

## kag_real_probe.cpp
`kag_real_probe.exe <dll> <folder of .ks files> <tag names file> [-v] [max]`: loads every scenario file through KAG with a logging handler for each tag name in the list (return/call/jump/eval/s/if/else/elsif/endif are left to the engine), runs Exec until it stops (max 300 steps) and prints a one-line summary per file (state, text hash, FNV of the callback/tag log); `-v` also prints the full log. Run on 400 real scenario files extracted (read-only) from script.arc: output identical between the original and the reconstruction for all 386 files that finished within the time limit (the sample's tail was not run). Probing it led to three fixes: tag-name lowercasing in AddTag/RemoveTag, a missing file/label inside call/jump/label no longer ends Exec early, `[jump]/[call]` storages without an extension get ".ks", and an unmatched `[R_return]` loads the empty storage.

## kag_console_probe.cpp
`kag_console_probe.exe <dll> <scratch folder>`: runs ~30 one-file scenarios (unknown tag, parser errors, missing file/label in call/jump, if/else/iscript/eval success and failure, macros, jump/call/return flows, and a string scenario) with a TJS console callback registered, and prints every console message with its exception flag. Addresses in VM dumps differ between runs; mask `0x...` before diffing. The reconstruction's console output equals the original's for every case.

## warp_probe.cpp, warp_diff.mjs, warp_cases.mjs
Warp (DLC patch) archives. `warp_probe.exe <dll> <op>... [-- members...]` runs AddArchive / SetBaseDirectory / preferred-directory operations on a FileSystemArchive and prints the results; ALWAYS on scratch copies, because the original rewrites the patch file on disk. `node warp_diff.mjs [pairs] [maxbytes]` copies real patch+base pairs to scratch, runs both DLLs and compares results and the md5 of the converted files (40 pairs identical). `node warp_cases.mjs` runs 21 layout/corruption cases through both DLLs (all identical). Paths to the game data are set at the top of the scripts.

## misc_exports_probe.cpp
`misc_exports_probe.exe <dll> <scratch folder>`: the nine exports no other probe named (TestFlag, AddAutoPathForAllFolder / IsFinishedAddAutoPathJob / ReleaseAddAutoPathJob on both file system kinds, File_ReleaseInstanceCount, HoleDevice CheckHidDevice / UpdateFrame without hardware, KAG GetKey / ClearScnearioCache). Output identical between the original and the reconstruction. With it, every one of the 215 exports is named by at least one test or differential probe.

## tjs_bridge_probe.cpp
`tjs_bridge_probe.exe <dll> <folder with the game's startup.tjs, utils.tjs, game_utils.tjs>`: the script shapes the game's ScriptManager generates (CameraTargetValue/TransformValue creation with .NET "G9" numbers, `global.tf = new Dictionary();`, GetRandomScenarioName) and about 90 number/string/Math/Dictionary expressions through EvalScript/ExecScript with the game's startup scripts loaded. 214 output lines, identical between the original and the reconstruction. The startup scripts come from script.arc (`node extract_ext.mjs ... .tjs`).

## kag_real_probe.cpp environment switches (TJS mode)
CM3D2_PROBE_HANG_MS (per-file watchdog), CM3D2_PROBE_START (resume at a file), CM3D2_PROBE_TJS=1 (console log, f/sf/tf/kag dumps, TjsScript with the file system as host context), CM3D2_PROBE_STARTUP=<folder> (runs the game's startup.tjs), CM3D2_PROBE_CALLBACKS=<names file> (registers each name through DLL_TJS_AddMemberFunction like ScriptManager.Initialize does; calls are logged as `native name(type:value, ...)`), CM3D2_PROBE_SHOWKEYS=1 (prints registration and call keys).

## abi_canary_probe.cpp and the managed audit scripts
`abi_canary_probe.exe <dll>` shows which bytes of a canary-filled slot each Create/New/Delete export writes. tools/list-managed-imports.ps1, list-managed-callers.ps1, find-managed-callers.ps1 and list-managed-strings.ps1 read the game's managed assemblies (read-only reflection) for the P/Invoke signatures, struct layouts, delegate types, callers and string literals.
