#include "cm3d2/anm_api.h"
#include "cm3d2/file_api.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void putString(Bytes& out, const std::string& text) {
    // Native length prefix: first + max(second - 1, 0) * 128 when first > 0x7f.
    if (text.size() <= 0x7f) {
        out.push_back(static_cast<std::uint8_t>(text.size()));
    } else if (text.size() <= 0xff) {
        out.push_back(static_cast<std::uint8_t>(text.size()));
        out.push_back(1);
    } else {
        throw std::logic_error("test helper only encodes short strings");
    }
    out.insert(out.end(), text.begin(), text.end());
}

void putWord(Bytes& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void putFloat(Bytes& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    putWord(out, bits);
}

Bytes header() {
    Bytes out;
    putString(out, "CM3D2_ANIM");
    putWord(out, 1001);
    return out;
}

void addPath(Bytes& out, const std::string& path) {
    out.push_back(1);
    putString(out, path);
}

void addCurve(Bytes& out, std::uint8_t tag, int frames) {
    out.push_back(tag);
    putWord(out, static_cast<std::uint32_t>(frames));
    for (int i = 0; i < frames; ++i) {
        putFloat(out, static_cast<float>(i));         // time
        putFloat(out, static_cast<float>(i) * 2.0f);  // value
        putFloat(out, 0.5f);                          // in tangent
        putFloat(out, -0.5f);                         // out tangent
    }
}

// Parses `data` with the given flags and returns the number of surviving curves,
// or -1 when the native-equivalent parse fails.
int curvesFor(const Bytes& data, bool mune_l = false, bool mune_r = false, bool bones = false) {
    CM3D2_AnmParseHandle parser = nullptr;
    DLL_AnmParse_New(&parser);
    require(parser != nullptr, "New returned null");
    const auto ok = DLL_AnmParse_OpenByte(&parser, data.data(),
        static_cast<std::int32_t>(data.size()), mune_l, mune_r, bones);
    const int count = ok ? static_cast<int>(DLL_AnmParse_AnimationCurveDataList_Size(
                               DLL_AnmParse_GetAnimationCurveDataList(&parser)))
                         : -1;
    DLL_AnmParse_Delete(&parser);
    require(parser == nullptr, "Delete must clear the handle");
    return count;
}

Bytes oneCurve(std::uint8_t tag, const std::string& path) {
    Bytes data = header();
    addPath(data, path);
    addCurve(data, tag, 1);
    data.push_back(0);
    return data;
}

} // namespace

int main() {
    // Basic structure: path, ComID, key frames, accessors.
    {
        Bytes data = header();
        addPath(data, "Bip01/Bip01 Pelvis");
        addCurve(data, 100, 2);
        data.push_back(7); // tags 2..99 carry no payload
        addCurve(data, 101, 0);
        addPath(data, "Other");
        addCurve(data, 102, 1);
        data.push_back(0);

        CM3D2_AnmParseHandle parser = nullptr;
        DLL_AnmParse_New(&parser);
        require(DLL_AnmParse_IsValid(&parser) == 0, "fresh parser must be invalid");
        require(DLL_AnmParse_OpenByte(&parser, data.data(),
                    static_cast<std::int32_t>(data.size()), 0, 0, 0) == 1,
            "valid carrier rejected");
        require(DLL_AnmParse_IsValid(&parser) == 1, "parsed parser must be valid");

        auto list = DLL_AnmParse_GetAnimationCurveDataList(&parser);
        require(DLL_AnmParse_AnimationCurveDataList_Size(list) == 3, "curve count mismatch");
        auto first = DLL_AnmParse_AnimationCurveDataList_At(list, 0);
        require(std::string(DLL_AnmParse_AnimationCurveData_GetPath(first)) == "Bip01/Bip01 Pelvis",
            "first path mismatch");
        require(DLL_AnmParse_AnimationCurveData_ComID(first) == 100, "first ComID mismatch");
        auto frames = DLL_AnmParse_AnimationCurveData_KeyFrameList(first);
        require(DLL_AnmParse_KeyFrameList_Size(frames) == 2, "key frame count mismatch");
        CM3D2_AnmKeyFrame frame{};
        DLL_AnmParse_KeyFrameList_At(frames, 1, &frame);
        require(frame.time == 1.0f && frame.value == 2.0f && frame.in_tangent == 0.5f &&
                frame.out_tangent == -0.5f,
            "key frame payload mismatch");

        auto second = DLL_AnmParse_AnimationCurveDataList_At(list, 1);
        require(std::string(DLL_AnmParse_AnimationCurveData_GetPath(second)) == "Bip01/Bip01 Pelvis",
            "path must persist until replaced");
        require(DLL_AnmParse_KeyFrameList_Size(
                    DLL_AnmParse_AnimationCurveData_KeyFrameList(second)) == 0,
            "empty key frame list mismatch");
        auto third = DLL_AnmParse_AnimationCurveDataList_At(list, 2);
        require(std::string(DLL_AnmParse_AnimationCurveData_GetPath(third)) == "Other",
            "replaced path mismatch");

        // Native raises a fatal subscript error here; the reconstruction is lenient.
        require(DLL_AnmParse_AnimationCurveDataList_At(list, 3) == nullptr, "At past end");
        require(DLL_AnmParse_AnimationCurveDataList_At(list, -1) == nullptr, "At negative");
        CM3D2_AnmKeyFrame untouched{9, 9, 9, 9};
        DLL_AnmParse_KeyFrameList_At(frames, 2, &untouched);
        require(untouched.time == 9.0f, "out-of-range key frame must not write");

        // Null/empty input returns 0 without clearing (the native checks first).
        require(DLL_AnmParse_OpenByte(&parser, nullptr, 0, 0, 0, 0) == 0, "null input accepted");
        require(DLL_AnmParse_AnimationCurveDataList_Size(list) == 3, "null input must not clear");
        // A bad carrier clears the previous result before failing.
        const Bytes junk{1, 2, 3, 4};
        require(DLL_AnmParse_OpenByte(&parser, junk.data(), 4, 0, 0, 0) == 0, "junk accepted");
        require(DLL_AnmParse_IsValid(&parser) == 0, "failed parse must leave no curves");

        DLL_AnmParse_OpenByte(&parser, data.data(), static_cast<std::int32_t>(data.size()), 0, 0, 0);
        DLL_AnmParse_Clear(&parser);
        require(DLL_AnmParse_IsValid(&parser) == 0, "Clear must empty the parser");
        DLL_AnmParse_Delete(&parser);
    }

    // Bone filter (tags 104..106 only, unless keep_all_bones).
    require(curvesFor(oneCurve(104, "Foo/Bar")) == 0, "unmatched bone curve kept");
    require(curvesFor(oneCurve(104, "Foo/Bar"), false, false, true) == 1, "keep_all_bones ignored");
    require(curvesFor(oneCurve(100, "Foo/Bar")) == 1, "tag 100 must bypass the bone filter");
    require(curvesFor(oneCurve(107, "Foo/Bar")) == 1, "tag 107 must bypass the bone filter");
    require(curvesFor(oneCurve(103, "Foo/Bar")) == 1, "tag 103 must bypass the bone filter");
    require(curvesFor(oneCurve(105, "Root/Bip01")) == 1, "path ending Bip01 rejected");
    require(curvesFor(oneCurve(105, "Root/Bip01/Spine")) == 0, "Bip01 mid-path must not match");
    require(curvesFor(oneCurve(106, "A/_IK_hand")) == 1, "_IK_ path rejected");
    require(curvesFor(oneCurve(104, "A/ManBip")) == 1, "path ending ManBip rejected");
    require(curvesFor(oneCurve(104, "A/ManBip/B")) == 0, "ManBip mid-path must not match");
    require(curvesFor(oneCurve(104, "A/Bone_Face/B")) == 1, "Bone_Face path rejected");
    require(curvesFor(oneCurve(104, "")) == 0, "empty path passes the bone filter");

    // Mune filter: dropped by default unless the path has _IK_.
    require(curvesFor(oneCurve(100, "Bip01/Mune_L")) == 0, "Mune_L kept by default");
    require(curvesFor(oneCurve(100, "Bip01/Mune_L"), true) == 1, "keep_mune_l ignored");
    require(curvesFor(oneCurve(100, "Bip01/Mune_L"), false, true) == 0, "Mune_L ruled by Mune_R flag");
    require(curvesFor(oneCurve(100, "Bip01/Mune_R")) == 0, "Mune_R kept by default");
    require(curvesFor(oneCurve(100, "Bip01/Mune_R"), false, true) == 1, "keep_mune_r ignored");
    require(curvesFor(oneCurve(100, "Bip01/_IK_Mune_L")) == 1, "_IK_ Mune_L dropped");
    // keep_all_bones does not rescue a Mune curve on tags 104..106.
    require(curvesFor(oneCurve(104, "Bip01/Mune_L"), false, false, true) == 0,
        "Mune filter must run after the bone filter");

    // Two-byte string length prefix: 130 characters encoded as 0x82, 0x01.
    {
        const std::string longPath = "Bip01/" + std::string(124, 'x');
        require(longPath.size() == 130, "test setup");
        require(curvesFor(oneCurve(100, longPath)) == 1, "two-byte length path rejected");
    }

    // Hardening: every strict prefix of a valid stream is rejected.
    {
        const Bytes full = oneCurve(100, "Bip01/Spine");
        for (std::size_t length = 1; length < full.size(); ++length) {
            const Bytes prefix(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(length));
            require(curvesFor(prefix) == -1, "truncated carrier accepted");
        }
        require(curvesFor(full) == 1, "full carrier rejected");
    }
    {
        Bytes negative = header();
        negative.push_back(100);
        putWord(negative, 0xffffffffu);
        negative.push_back(0);
        require(curvesFor(negative) == -1, "negative key frame count accepted");
    }
    {
        Bytes wrong;
        putString(wrong, "CM3D2_MENU");
        putWord(wrong, 1);
        wrong.push_back(0);
        require(curvesFor(wrong) == -1, "wrong magic accepted");
    }

    // Open through a file interface: size via slot 8, readAt via slot 4.
    {
        const Bytes data = oneCurve(100, "Bip01/Spine");
        CM3D2_FileHandle file = nullptr;
        DLL_FileMemory_CreateileMemory(&file);
        DLL_FileMemory_SetData(&file, data.data(), static_cast<std::int32_t>(data.size()));

        CM3D2_AnmParseHandle parser = nullptr;
        DLL_AnmParse_New(&parser);
        require(DLL_AnmParse_Open(&parser, DLL_FileMemory_GetInterfaceFilePointer(&file), 0, 0, 0) == 1,
            "Open through file interface failed");
        require(DLL_AnmParse_AnimationCurveDataList_Size(
                    DLL_AnmParse_GetAnimationCurveDataList(&parser)) == 1,
            "Open curve count mismatch");
        require(DLL_AnmParse_Open(&parser, nullptr, 0, 0, 0) == 0, "null file accepted");
        DLL_AnmParse_Delete(&parser);
        DLL_FileMemory_DeleteileMemory(&file);
    }

    return 0;
}
