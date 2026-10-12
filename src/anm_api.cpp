#include "cm3d2/anm_api.h"

#include "anm_format.h"
#include "file_interface.h"

#include <cstddef>
#include <vector>

namespace {

using cm3d2::internal::AnmCurve;
using cm3d2::internal::AnmKeyFrame;
using cm3d2::internal::AnmOptions;

// Native handle: pointer to a 0x18-byte vector of 0x40-byte curve records
// (string path, u32 id at +0x20, key-frame vector at +0x28). This is a
// semantic reconstruction: callers only see these records through the API.
using CurveList = std::vector<AnmCurve>;
using KeyFrames = std::vector<AnmKeyFrame>;

static_assert(sizeof(AnmKeyFrame) == sizeof(CM3D2_AnmKeyFrame), "key frame ABI mismatch");

CurveList* list(CM3D2_AnmParseHandle* parser) {
    return parser == nullptr ? nullptr : static_cast<CurveList*>(*parser);
}

AnmOptions options(uint8_t mune_l, uint8_t mune_r, uint8_t all_bones) {
    return AnmOptions{mune_l != 0, mune_r != 0, all_bones != 0};
}

uint8_t parse(CM3D2_AnmParseHandle* parser, const uint8_t* data, std::size_t size,
    const AnmOptions& opts) {
    CurveList* curves = list(parser);
    if (curves == nullptr) return 0;
    try {
        return cm3d2::internal::parseAnm(data, size, opts, *curves) ? 1 : 0;
    } catch (...) {
        curves->clear();
        return 0;
    }
}

} // namespace

extern "C" {

void CM3D2_CALL DLL_AnmParse_New(CM3D2_AnmParseHandle* out_parser) {
    if (out_parser == nullptr) return;
    *out_parser = new CurveList;
}

void CM3D2_CALL DLL_AnmParse_Delete(CM3D2_AnmParseHandle* parser) {
    if (parser == nullptr) return;
    delete static_cast<CurveList*>(*parser);
    *parser = nullptr;
}

uint32_t CM3D2_CALL DLL_AnmParse_Open(CM3D2_AnmParseHandle* parser, void* interface_file,
    uint8_t keep_mune_l, uint8_t keep_mune_r, uint8_t keep_all_bones) {
    if (list(parser) == nullptr || interface_file == nullptr) return 0;
    auto* file = static_cast<cm3d2::internal::IFile*>(interface_file);
    try {
        // Native: size from vtable slot 8, zeroed buffer, slot 4 readAt(buf, 0, size).
        const std::uint64_t size = file->size();
        std::vector<uint8_t> buffer(static_cast<std::size_t>(size), 0);
        file->readAt(buffer.data(), 0, size);
        return parse(parser, buffer.data(), buffer.size(),
            options(keep_mune_l, keep_mune_r, keep_all_bones));
    } catch (...) {
        return 0;
    }
}

uint32_t CM3D2_CALL DLL_AnmParse_OpenByte(CM3D2_AnmParseHandle* parser, const uint8_t* data,
    int32_t byte_count, uint8_t keep_mune_l, uint8_t keep_mune_r, uint8_t keep_all_bones) {
    // The native sign-extends the count to 64 bits; a negative count is rejected.
    if (byte_count < 0) return 0;
    return parse(parser, data, static_cast<std::size_t>(byte_count),
        options(keep_mune_l, keep_mune_r, keep_all_bones));
}

void CM3D2_CALL DLL_AnmParse_Clear(CM3D2_AnmParseHandle* parser) {
    if (CurveList* curves = list(parser)) curves->clear();
}

uint32_t CM3D2_CALL DLL_AnmParse_IsValid(CM3D2_AnmParseHandle* parser) {
    const CurveList* curves = list(parser);
    return curves != nullptr && !curves->empty() ? 1 : 0;
}

CM3D2_AnmCurveListHandle CM3D2_CALL DLL_AnmParse_GetAnimationCurveDataList(
    CM3D2_AnmParseHandle* parser) {
    return parser == nullptr ? nullptr : *parser;
}

int64_t CM3D2_CALL DLL_AnmParse_AnimationCurveDataList_Size(CM3D2_AnmCurveListHandle handle) {
    const auto* curves = static_cast<const CurveList*>(handle);
    return curves == nullptr ? 0 : static_cast<int64_t>(curves->size());
}

// Native: an out-of-range index reaches a nonreturning "invalid vector subscript"
// error. Returning null here is a deliberate normalization.
CM3D2_AnmCurveHandle CM3D2_CALL DLL_AnmParse_AnimationCurveDataList_At(
    CM3D2_AnmCurveListHandle handle, int32_t index) {
    auto* curves = static_cast<CurveList*>(handle);
    if (curves == nullptr || index < 0 || static_cast<std::size_t>(index) >= curves->size()) {
        return nullptr;
    }
    return &(*curves)[static_cast<std::size_t>(index)];
}

const char* CM3D2_CALL DLL_AnmParse_AnimationCurveData_GetPath(CM3D2_AnmCurveHandle handle) {
    const auto* curve = static_cast<const AnmCurve*>(handle);
    return curve == nullptr ? nullptr : curve->path.c_str();
}

uint32_t CM3D2_CALL DLL_AnmParse_AnimationCurveData_ComID(CM3D2_AnmCurveHandle handle) {
    const auto* curve = static_cast<const AnmCurve*>(handle);
    return curve == nullptr ? 0 : curve->com_id;
}

CM3D2_AnmKeyFrameListHandle CM3D2_CALL DLL_AnmParse_AnimationCurveData_KeyFrameList(
    CM3D2_AnmCurveHandle handle) {
    auto* curve = static_cast<AnmCurve*>(handle);
    return curve == nullptr ? nullptr : &curve->key_frames;
}

int64_t CM3D2_CALL DLL_AnmParse_KeyFrameList_Size(CM3D2_AnmKeyFrameListHandle handle) {
    const auto* frames = static_cast<const KeyFrames*>(handle);
    return frames == nullptr ? 0 : static_cast<int64_t>(frames->size());
}

// Native: out-of-range index reaches the same nonreturning subscript error; here
// the output is left untouched.
void CM3D2_CALL DLL_AnmParse_KeyFrameList_At(
    CM3D2_AnmKeyFrameListHandle handle, int32_t index, CM3D2_AnmKeyFrame* out_key_frame) {
    const auto* frames = static_cast<const KeyFrames*>(handle);
    if (frames == nullptr || out_key_frame == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= frames->size()) {
        return;
    }
    const AnmKeyFrame& frame = (*frames)[static_cast<std::size_t>(index)];
    out_key_frame->time = frame.time;
    out_key_frame->value = frame.value;
    out_key_frame->in_tangent = frame.in_tangent;
    out_key_frame->out_tangent = frame.out_tangent;
}

} // extern "C"
