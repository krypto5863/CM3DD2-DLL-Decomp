# cm3d2.dll source reconstruction

> **Authorship:** this repository was written entirely by AI coding agents, **Claude** (Anthropic; Claude Sonnet 5.5 in the final sessions) and **Codex** (OpenAI), working together under the direction of [krypto5863](https://github.com/krypto5863). Treat everything here as machine-generated and unreviewed by a human maintainer.

An evidence-led C++ reconstruction of the native x64 `cm3d2.dll` plugin used by COM3D2. It is inferred source code written from analysis of the binary, not recovered original source, and it is not affiliated with the game's developer. No game files or original binaries are included.

## Status

- All 215 exported names are declared at their observed ordinals (`exports/cm3d2_file.def`).
- Source areas (`src/`, public headers in `include/cm3d2/`): file objects, file systems (Windows folders, archives, auto-path jobs, delegate), CSV/WCSV, EnumData, AnmParse, menu parsing and database, HoleDevice, and OGG, KAG and TJS wrappers.
- Behavior was compared against the original on synthetic inputs only. It has not been validated in a game run and is not claimed to be fully equivalent. Several behaviors are deliberate hardening rather than reproductions of original faults.
- Warp-patch archives are converted in place when mounted, as the original does. Do not point it at game archives you have not backed up.
- Comments in the sources mention analysis notes and evidence that are not part of this repository.

## Layout

- `src/`, `include/` - the reconstruction.
- `tests/` - contract tests, registered with CTest by `CMakeLists.txt`.
- `tools/differential/` - probes and batteries that compare a build against an original DLL. They need a copy of the original DLL and fixtures that you supply; none are provided.
- `exports/` - the export definition file.
- `third_party/` - vendored code (below).

## Third-party code

- `third_party/stb` - `stb_vorbis.c` (Sean Barrett, v1.22), MIT or public domain; the notice is at the end of the file.
- `third_party/tjs2` - a vendored Kirikiri Z TJS2 core with its license retained (`third_party/tjs2/LICENSE`, `RECONSTRUCTION-NOTES.md`). Its download URL and commit are not recorded.
- `third_party/kagparser` - the KAG parser (`third_party/kagparser/LICENSE`, `RECONSTRUCTION-NOTES.md`).

## Build

Visual Studio x64 developer prompt, CMake 3.21 or newer:

```powershell
cmake -S . -B build -A x64
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

The DLL links the C/C++ runtime statically and needs only system DLLs (KERNEL32, USER32, BCRYPT, SETUPAPI, HID, DINPUT8). A full build and test run was last checked during development; it has not been re-run from this exact tree.

## License

The code written for this project is released into the public domain under [The Unlicense](LICENSE). The Unlicense does not cover `third_party/`, which keeps its own licenses and notices.
