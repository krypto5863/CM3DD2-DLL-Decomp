# Embedded KAGParser notes

`KAGParser.cpp` / `KAGParser.h` are the open-source Kirikiri KAGParser (original TVP2 code by W.Dee and contributors,
license text in LICENSE), taken from zeas2/Kirikiroid2 commit d1c2b1259423542c893e0b65eaeb46c848848f2b (2024-06-05).
It is NOT recovered cm3d2 source. cm3d2.dll contains this component (RTTI `tTJSNC_KAGParser`, `tTJSNI_KAGParser`)
with local modifications; the DLL's own KagScript/BasicKagScript wrapper is rebuilt separately (src/kag_script.cpp) from
pseudocode. The local patches below make the vendored parser behave like the DLL's parser; each was confirmed by running the
same scenarios through the original DLL (tools/differential/kag_*_probe.cpp).

Local patches (search for "cm3d2" in the sources):
- The English message definitions at the top of KAGParser.cpp are commented out; the DLL's Japanese default messages
  (read from cm3d2.dll at 0x1801e9638..0x1801e9c68) are defined in src/kag_host.cpp instead.
- Special-tag attribute names: the DLL parser reads `file` and `label` for the jump / call / return tags (upstream: `storage`,
  `target`). The call and return tags are only processed while `TagCallEnabled` / `TagReturnEnabled` are true (DLL parser
  +0x131 / +0x132, default true); a disabled tag is skipped like an unknown tag.
- Hooks fired for the DLL wrapper (DLL parser std::function slots): `OnLoadHook` (start of LoadScenario, +0x180),
  `OnLabelHook` (GoToLabel found the label, +0x1c0), `OnJumpHook` / `OnCallHook` (before the onJump / onCall events, +0x200 /
  +0x240; both may rewrite the storage), `OnReturnHook` (before onReturn with the call stack top, only when the stack is not
  empty, +0x280), `OnLabelPassHook` (the parser passes a label line: label, has page, page; +0x140).
- `LoadScenarioString(text)`: loads a scenario from text; the storage is named "string scenario : no<N>" (global counter from 0).
- State dictionary: extra key `scenarioStr` (the text of a scenario loaded by LoadScenarioString, otherwise empty); Restore reloads
  such a scenario through LoadScenarioString (so the restored scenario gets a new name).
- The constructor initializes CurLine, CurPos, LineBufferUsing, LineCount, TagLine, ExcludeLevel (0), IfLevel (0). Upstream leaves
  them uninitialized; the original DLL then shows heap garbage in a fresh object's saved state (not reproduced).
- Accessors added: GetStorageShortName, GetOwner, ClearCallStackExternal, GetCallStackDepthExternal, FromObject.
- Compiled with a forced include of shims/kag_prefix.h (the vendored TJS headers need <string> and tjsCommHead.h first).
- shims/*.h replace the Kirikiri host headers (ScriptMgnIntf, StorageImpl, TextStream, MsgIntf, EventIntf, ...); the host functions
  are implemented in src/kag_host.cpp and routed to the DLL file system and the TJS engine.

Known remaining differences from the DLL parser are listed in docs/kag-reconstruction.md.

## Console / error reporting patches (2026-10-11)
LoadScenario logs "Scenario loaded : <name>" after every load (no separator line); LoadScenarioString logs it too; GoToLabel reports a missing scenario/label through TVPKagReport instead of throwing and logs "<short> => jumped to : <label>"; GoToStorageAndLabel catches eTJSError from the load and reports it as "[eTJSError]...". See docs/kag-reconstruction.md "Console output".
