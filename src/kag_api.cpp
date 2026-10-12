#include "cm3d2/kag_api.h"

#include "kag_host.h"
#include "kag_script.h"
#include "tjs_variant_internal.h"

#include <Windows.h>

#include <atomic>

#include <cstdlib>
#include <cstring>

namespace {

std::wstring wide(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == 0) return {};
    const int length = static_cast<int>(std::strlen(utf8));
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8, length, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, utf8, length, out.data(), size);
    return out;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

// FUN_18006f7a0: UTF-8 copy truncated to capacity - 1, always terminated.
void copyOut(const std::wstring& text, char* destination, int capacity) {
    if (destination == nullptr || capacity <= 0) return;
    const std::string utf8 = narrow(text);
    if (utf8.empty()) {
        destination[0] = 0;
        return;
    }
    const std::size_t count = static_cast<std::uint64_t>(static_cast<long long>(static_cast<int>(utf8.size()))) < static_cast<std::uint64_t>(capacity)
        ? utf8.size() : static_cast<std::size_t>(capacity - 1);
    std::memcpy(destination, utf8.data(), count);
    destination[count] = 0;
}

CM3D2_KagScript* script(CM3D2_KagHandle* handle) { return handle == nullptr ? nullptr : static_cast<CM3D2_KagScript*>(*handle); }

std::atomic<int> g_serialize_count{0};  // _DAT_18021dc60

} // namespace

extern "C" {

void CM3D2_CALL DLL_KAG_Create(CM3D2_TjsScriptHandle* tjs, CM3D2_KagHandle* out_kag) {
    auto* created = new CM3D2_KagScript(tjs == nullptr ? nullptr : *tjs);
    if (!created->valid()) {
        delete created;
        created = nullptr;
    }
    *out_kag = created;
}

void CM3D2_CALL DLL_KAG_Delete(CM3D2_KagHandle* kag) {
    if (kag == nullptr) return;
    delete script(kag);
    *kag = nullptr;
}

void CM3D2_CALL DLL_KAG_SetFileSystem(CM3D2_KagHandle* kag, void* file_system) { script(kag)->setFileSystem(file_system); }
void CM3D2_CALL DLL_KAG_LoadScenario(CM3D2_KagHandle* kag, const char* storage) { script(kag)->loadScenario(wide(storage)); }
void CM3D2_CALL DLL_KAG_LoadScenarioString(CM3D2_KagHandle* kag, const char* text) { script(kag)->loadScenarioString(wide(text)); }
void CM3D2_CALL DLL_KAG_GoToLabel(CM3D2_KagHandle* kag, const char* label) { script(kag)->goToLabel(wide(label)); }
int32_t CM3D2_CALL DLL_KAG_Exec(CM3D2_KagHandle* kag) { return script(kag)->exec(); }
uint32_t CM3D2_CALL DLL_KAG_IsExecScenario(CM3D2_KagHandle* kag) { return script(kag)->running() ? 1 : 0; }
void CM3D2_CALL DLL_KAG_TextClear(CM3D2_KagHandle* kag) { script(kag)->textClear(); }
void CM3D2_CALL DLL_KAG_ClearCallStack(CM3D2_KagHandle* kag) { script(kag)->clearCallStack(); }
void CM3D2_CALL DLL_KAG_ClearScnearioCache(CM3D2_KagHandle*) { TVPClearScnearioCache(); }

void CM3D2_CALL DLL_KAG_GetText(CM3D2_KagHandle* kag, char* destination, int32_t capacity) { copyOut(script(kag)->text(), destination, capacity); }
void CM3D2_CALL DLL_KAG_GetStoragename(CM3D2_KagHandle* kag, char* destination, int32_t capacity) { copyOut(script(kag)->storageName(), destination, capacity); }
void CM3D2_CALL DLL_KAG_GetCurrentLabel(CM3D2_KagHandle* kag, char* destination, int32_t capacity) { copyOut(script(kag)->currentLabel(), destination, capacity); }
int32_t CM3D2_CALL DLL_KAG_GetCurrentLine(CM3D2_KagHandle* kag) { return script(kag)->currentLine(); }
int32_t CM3D2_CALL DLL_KAG_GetCallStackDepth(CM3D2_KagHandle* kag) { return script(kag)->callStackDepth(); }
int32_t CM3D2_CALL DLL_KAG_GetRCallStackDepth(CM3D2_KagHandle* kag) { return script(kag)->rcallStackDepth(); }
uint64_t CM3D2_CALL DLL_KAG_GetUniqueID(CM3D2_KagHandle* kag) { return script(kag)->uniqueId(); }
uint64_t CM3D2_CALL DLL_KAG_GetUniqueIDFromBasicKagScriptPtr(const CM3D2_KagScript* s) { return s->uniqueId(); }
void CM3D2_CALL DLL_KAG_SetTagCallEnabled(CM3D2_KagHandle* kag, uint8_t enabled) { script(kag)->setTagCallEnabled(enabled != 0); }
uint32_t CM3D2_CALL DLL_KAG_GetTagCallEnabled(CM3D2_KagHandle* kag) { return script(kag)->tagCallEnabled() ? 1 : 0; }
void CM3D2_CALL DLL_KAG_SetTagReturnEnabled(CM3D2_KagHandle* kag, uint8_t enabled) { script(kag)->setTagReturnEnabled(enabled != 0); }
uint32_t CM3D2_CALL DLL_KAG_GetTagReturnEnabled(CM3D2_KagHandle* kag) { return script(kag)->tagReturnEnabled() ? 1 : 0; }

void CM3D2_CALL DLL_KAG_AddFinishFile(CM3D2_KagHandle* kag, const char* storage) { script(kag)->records().addFinish(wide(storage)); }
uint32_t CM3D2_CALL DLL_KAG_GetFinishFile(CM3D2_KagHandle* kag, const char* storage) { return script(kag)->records().hasFinish(wide(storage)) ? 1 : 0; }
void CM3D2_CALL DLL_KAG_AddRecordLabel(CM3D2_KagHandle* kag, const char* storage, const char* label) {
    script(kag)->records().addLabel(wide(storage), wide(label));
}
uint32_t CM3D2_CALL DLL_KAG_GetRecordLabel(CM3D2_KagHandle* kag, const char* storage, const char* label) {
    return script(kag)->records().hasLabel(wide(storage), wide(label)) ? 1 : 0;
}

CM3D2_KagSerializeData* CM3D2_CALL DLL_KAG_CreateSerializeData(CM3D2_KagHandle* kag) {
    ++g_serialize_count;
    return script(kag)->createSerializeData();
}
int32_t CM3D2_CALL DLL_KAG_GetSerializeDataSize(const CM3D2_KagSerializeData* data) { return static_cast<int32_t>(data->size); }
void CM3D2_CALL DLL_KAG_WriteSerializeData(void* destination, const CM3D2_KagSerializeData* data) {
    std::memcpy(destination, data->data, static_cast<std::size_t>(data->size));
}
void CM3D2_CALL DLL_KAG_ReleaseSerializeData(CM3D2_KagSerializeData* data) {
    if (data == nullptr) return;
    std::free(data->data);
    delete data;
    --g_serialize_count;
}
int64_t CM3D2_CALL DLL_KAG_Deserialize(CM3D2_KagHandle* kag, const void* data) { return script(kag)->deserialize(data); }

uint64_t CM3D2_CALL DLL_KAG_GetKey(void*, const char* name) { return cm3d2::kag::hashUtf16(wide(name)); }

// Returns the tag key (hash of the lower-cased name, the value DLL_KAG_GetKey gives for it and DLL_KAG_TAG_GetTagKey reports
// inside the handler); the original returns it even when the tag already exists (checked with ret_probe).
uint64_t CM3D2_CALL DLL_KAG_AddTag(CM3D2_KagHandle* kag, const char* name, CM3D2_KagTagFunction function) {
    const std::wstring tag_name = wide(name);
    if (function != nullptr) {
        script(kag)->addTag(tag_name, [function](CM3D2_KagTagSupport* tag) { return function(tag) != 0; });
    }
    return script(kag)->tagKey(tag_name);
}
uint32_t CM3D2_CALL DLL_KAG_RemoveTag(CM3D2_KagHandle* kag, const char* name) { return script(kag)->removeTag(wide(name)) ? 1 : 0; }

uint32_t CM3D2_CALL DLL_KAG_TAG_IsValid(CM3D2_KagTagSupport* tag, const char* name) {
    return tag->dict->IsValid(0, wide(name).c_str(), nullptr, tag->dict) == TJS_S_TRUE ? 1 : 0;
}
void CM3D2_CALL DLL_KAG_TAG_GetTagProperty(CM3D2_KagTagSupport* tag, const char* name, void** variant_handle) {
    auto* value = static_cast<cm3d2::internal::TjsVariant*>(*variant_handle);
    tag->dict->PropGet(0, wide(name).c_str(), nullptr, &value->native(), tag->dict);
}
uint64_t CM3D2_CALL DLL_KAG_TAG_GetTagKey(const CM3D2_KagTagSupport* tag) { return tag->key; }
uint64_t CM3D2_CALL DLL_KAG_TAG_GetClassUniqueid(const CM3D2_KagTagSupport* tag) { return tag->owner->uniqueId(); }
void CM3D2_CALL DLL_KAG_TAG_CreateTagList(CM3D2_KagTagSupport* tag) { tag->owner->createTagList(*tag); }
int64_t CM3D2_CALL DLL_KAG_TAG_GetListCount(const CM3D2_KagTagSupport* tag) { return static_cast<int64_t>(tag->list.size()); }
void CM3D2_CALL DLL_KAG_TAG_GetListNameAt(const CM3D2_KagTagSupport* tag, int32_t index, char* destination, int32_t capacity) {
    copyOut(wide(tag->list.at(static_cast<std::size_t>(index)).first.c_str()), destination, capacity);
}
void CM3D2_CALL DLL_KAG_TAG_GetListElmAt(const CM3D2_KagTagSupport* tag, int32_t index, char* destination, int32_t capacity) {
    copyOut(wide(tag->list.at(static_cast<std::size_t>(index)).second.c_str()), destination, capacity);
}

void CM3D2_CALL DLL_KAG_SetReplaceFunction(CM3D2_KagHandle* kag, CM3D2_KagReplaceFunction function) { script(kag)->replace_function = function; }
void CM3D2_CALL DLL_KAG_SetOnScenarioLoadFunction(CM3D2_KagHandle* kag, CM3D2_KagTextFunction function) { script(kag)->on_scenario_load_function = function; }
void CM3D2_CALL DLL_KAG_SetOnCallFunction(CM3D2_KagHandle* kag, CM3D2_KagTextFunction function) { script(kag)->on_call_function = function; }
void CM3D2_CALL DLL_KAG_SetOnRCallFunction(CM3D2_KagHandle* kag, CM3D2_KagTextFunction function) { script(kag)->on_rcall_function = function; }
void CM3D2_CALL DLL_KAG_SetOnReturnFunction(CM3D2_KagHandle* kag, CM3D2_KagReturnFunction function) { script(kag)->on_return_function = function; }
void CM3D2_CALL DLL_KAG_SetOnRReturnFunction(CM3D2_KagHandle* kag, CM3D2_KagTextFunction function) { script(kag)->on_rreturn_function = function; }
void CM3D2_CALL DLL_KAG_SetOnLabelCallBackFunction(CM3D2_KagHandle* kag, CM3D2_KagLabelFunction function) { script(kag)->label_function = function; }

} // extern "C"
