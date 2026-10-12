#pragma once

#include "cm3d2/kag_api.h"
#include "kag_host_context.h"
#include "kag_serialize.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TJS {
class tTJS;
class iTJSDispatch2;
}
class tTJSNI_KAGParser;

// The handler argument (KagTagSupport): the tag dictionary produced by the parser plus the script that runs it.
struct CM3D2_KagTagSupport {
    TJS::iTJSDispatch2* dict = nullptr;  // +0 in the original
    std::uint64_t key = 0;               // +0x20: hash of the tag name
    CM3D2_KagScript* owner = nullptr;    // +0x28
    std::vector<std::pair<std::string, std::string>> list;  // filled by DLL_KAG_TAG_CreateTagList
};

struct CM3D2_KagSerializeData {
    std::uint8_t* data = nullptr;
    std::int64_t size = 0;
};

// KagScript over BasicKagScript (vtables 0x1801ea240 / 0x1801ea1b0), reconstructed from pseudocode;
// the parser is the embedded Kirikiri KAGParser with the local additions listed in
// third_party/kagparser/RECONSTRUCTION-NOTES.md.
struct CM3D2_KagScript {
    explicit CM3D2_KagScript(void* tjs_script_pointer);
    ~CM3D2_KagScript();
    CM3D2_KagScript(const CM3D2_KagScript&) = delete;
    CM3D2_KagScript& operator=(const CM3D2_KagScript&) = delete;

    bool valid() const { return parser_ != nullptr; }

    void setFileSystem(const void* file_system) { host_.file_system = file_system; }
    void loadScenario(const std::wstring& storage);
    void loadScenarioString(const std::wstring& text);
    void goToLabel(const std::wstring& label);
    int exec();
    bool running() const { return running_; }
    void textClear() { text_.clear(); }
    void clearCallStack();
    int callStackDepth() const;
    int rcallStackDepth() const { return static_cast<int>(calls_.size()); }
    std::wstring storageName() const;
    std::wstring currentLabel() const;
    int currentLine() const;
    const std::wstring& text() const { return text_; }
    std::uint64_t uniqueId() const { return unique_id_; }
    void setTagCallEnabled(bool enabled);
    bool tagCallEnabled() const;
    void setTagReturnEnabled(bool enabled);
    bool tagReturnEnabled() const;

    cm3d2::kag::RecordDb& records() { return records_; }
    CM3D2_KagSerializeData* createSerializeData();
    std::int64_t deserialize(const void* data);

    void addTag(const std::wstring& name, std::function<bool(CM3D2_KagTagSupport*)> function);
    std::uint64_t tagKey(const std::wstring& name) const;  // the key AddTag returns (hash of the lower-cased name)
    bool removeTag(const std::wstring& name);
    void createTagList(CM3D2_KagTagSupport& support);

    CM3D2_KagReplaceFunction replace_function = nullptr;
    CM3D2_KagTextFunction on_scenario_load_function = nullptr;
    CM3D2_KagTextFunction on_call_function = nullptr;
    CM3D2_KagTextFunction on_rcall_function = nullptr;
    CM3D2_KagReturnFunction on_return_function = nullptr;
    CM3D2_KagTextFunction on_rreturn_function = nullptr;
    CM3D2_KagLabelFunction label_function = nullptr;

private:
    struct TagEntry {
        std::wstring name;
        std::function<bool(CM3D2_KagTagSupport*)> function;
    };

    cm3d2::kag::HostContext hostContext() const { return host_; }
    void installHooks();
    void registerDefaultTags();
    std::wstring applyReplace(const std::wstring& text) const;
    std::wstring normalizeJumpStorage(const std::wstring& storage) const;
    void report(const std::wstring& message) const;

    // Tag handlers (FUN_180103240 ch, FUN_1801032d0 eval, FUN_180106170 re-emit, FUN_180106490 r_call, FUN_180106fb0 r_return).
    bool tagCh(CM3D2_KagTagSupport* support);
    bool tagEval(CM3D2_KagTagSupport* support);
    bool tagReEmit(CM3D2_KagTagSupport* support);
    bool tagRCall(CM3D2_KagTagSupport* support);
    bool tagRReturn(CM3D2_KagTagSupport* support);
    bool tagStop(CM3D2_KagTagSupport* support);

    void* tjs_script_ = nullptr;
    cm3d2::kag::HostContext host_;
    TJS::iTJSDispatch2* parser_object_ = nullptr;
    tTJSNI_KAGParser* parser_ = nullptr;
    std::wstring text_;                  // +0x20
    bool running_ = false;               // +0xd0
    bool paused_ = false;                // +0x2b0 (set by the [s] tag, cleared by the parser hooks)
    std::uint64_t unique_id_ = 0;        // +0x118
    cm3d2::kag::RecordDb records_;       // +0xc0
    cm3d2::kag::CallRecords calls_;      // +0x2a0
    std::unordered_map<std::uint64_t, TagEntry> tags_;           // +0xd8
    std::unordered_map<std::uint64_t, std::wstring> tag_names_;  // +0x80
};
