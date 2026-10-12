# Vendored TJS2 build notes

This tree arrived in Claude's unfinished TJS draft. Headers identify Kirikiri Z
TJS2 version 2.4.28; the existing LICENSE and per-file notices are retained.
The source URL and exact upstream commit were not recorded and remain unknown.
Do not label this tree recovered cm3d2 source or assert an exact native version match.

Build adaptations include guarded Windows/version macros, an Array stream-write
typo correction, and guarded RegExp cleanup. CMake keeps TJS_NO_REGEXP because
the native exposed engine does not register RegExp (confirmed statically and by
a bounded runtime probe). Legacy std::binary_function compatibility is enabled with
_HAS_AUTO_PTR_ETC=1. Vendor warnings are suppressed; wrappers retain /W4.

src/tjs_platform.cpp supplies Windows CP932 hooks in place of missing upstream
conversion tables. Mapping equivalence outside tested inputs is unverified.
src/tjs_messages.cpp initializes 58 recovered native defaults, once per process.
Eleven newer-engine message fields have no matching named native holder and
remain unresolved; no replacement strings were invented.

The native Scripts class is now reconstructed in src/tjs_api.cpp (exec/eval and
storage methods) separately from this vendor tree. Text/binary stream factories
are not installed, so Array/Dictionary file serialization is unsupported. The host
facade now preserves object closures (object and receiver) and octets using this
core's variant ownership, with borrowed live callback views. This is a semantic
facade, not the original packed variant ABI. See docs/tjs-payload-reconstruction.md
for native checks and engine/finalizer lifetime limits.
CM3D2_TJS_NATIVE_BUILTINS disables newer vendor-only Array/Dictionary loadStruct
registrations. tjsCm3d2Dictionary.cpp reconstructs native class-only toStructString
and getKeyArray, with registration in tjsDictionary.cpp; these are additions, not
upstream source. Native compact text uses comma key/value pairs, raw control
characters, plain hex reals and native octet spacing. Null/cycle/depth handling
is deliberately hardened. KAG's independent serializer is unchanged. Details and
the builtin follow-up: docs/tjs-builtins-reconstruction.md.
Native sorting comparator conversion failures and copy-based insertion for <=32
elements are reconstructed in tjsArray.cpp under CM3D2_TJS_NATIVE_BUILTINS.
This avoids Debug-only extra script comparator calls. Larger arrays now use
reconstructed introsort/heap and sufficient-buffer stable merge paths in
tjsCm3d2ArraySort.h / tjsCm3d2ArrayStableSort.h. Temporary payloads use safe RAII;
the native allocator's reduced-memory fallback and post-failure engine reuse are
not reproduced. See docs/tjs-array-sort-reconstruction.md.
Native debugger integration is disabled in the vendor configuration, but the
upstream debugger statement can still invoke __debugbreak. Run only controlled
scripts until engine compatibility and integration are reviewed further.

Lifetime follow-up: tjs.cpp initializes Global before allocation-failure Cleanup.
The wrapper separately guards retained host dispatch/Scripts owners and releases
its engine on Scripts-install failure. These are deliberate hardening, not full
OOM recovery or native fatal teardown reproduction. Explicitly invalidate script
finalizers before globals/callbacks disappear; native teardown has confirmed
callback-map and global-name hazards. See docs/tjs-finalizer-reconstruction.md.

Array split/join were audited against recovered native bodies and128 differential
cases; existing vendor behavior matches, including retained Array methods after
invalidation. Dictionary.load/save are native no-op stubs, not missing file I/O.
Array stream methods/Dictionary.saveStruct use null streams in the original
exposed routes too. No additional stream factories enabled. Regression test and
scope: docs/tjs-array-text-reconstruction.md.

Dictionary Assign/AssignStructure native bodies have no hash reserve/count
prepass. Three newer vendor reserve blocks are disabled in the native-compatible
configuration to preserve observable key order.114 assignment cases match native
in both builds. Details and limits: docs/tjs-assign-reconstruction.md.
