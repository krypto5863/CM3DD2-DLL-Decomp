#include "kag_serialize.h"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

// Golden bytes come from the ORIGINAL DLL (tools/differential/kag_serialize_probe.cpp on 2026-10-11) and the layout from
// FUN_180102ce0 / FUN_180105e50 / FUN_180106010 / FUN_180104540.
namespace {

using namespace cm3d2::kag;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string hex(const std::vector<std::uint8_t>& bytes, std::size_t from = 0, std::size_t count = std::string::npos) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = from; i < bytes.size() && out.size() < count * 2; ++i) {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 15]);
    }
    return out;
}

// The state text the original produced for a fresh parser (Dictionary.toStructString).
const wchar_t* kEmptyState =
    L"(const)%[\"ExcludeLevelStack\",\"\",\"IfLevel\",0,\"macros\",(const)%[],\"macroArgStackBase\",0,\"IfLevelExecutedStack\",\"\","
    L"\"curPos\",0,\"storageShortName\",\"\",\"curLine\",0,\"storageName\",\"\",\"scenarioStr\",\"\",\"lineBufferUsing\",0,"
    L"\"callStack\",(const)[],\"macroArgs\",(const)[],\"macroArgStackDepth\",0,\"curLabel\",\"\",\"ExcludeLevel\",0,\"lineBuffer\",\"\"]";

} // namespace

int main() try {
    // Hash: FNV-1a 64 over the UTF-16 bytes; the three keys seen in the original's serialized records.
    require(hashUtf16(L"a.ks") == 0x621a9dc88163'2a3aull, "hash a.ks");
    require(hashUtf16(L"c.ks") == 0xaed9f330395b3a98ull, "hash c.ks");
    require(hashUtf16(L"b.ks") == 0xe6957be04f5f7ad9ull, "hash b.ks");
    require(hashUtf16(L"*x") == 0x8ca5fdc8345ffbf7ull, "hash *x");

    // Records: same operations as the probe; the original's 88-byte block.
    RecordDb records;
    require(records.addLabel(L"a.ks", L"*x"), "new label");
    require(records.addLabel(L"a.ks", L"*y"), "second label");
    require(!records.addLabel(L"a.ks", L"*x"), "duplicate label");
    require(records.addLabel(L"b.ks", L"*x"), "other storage");
    require(records.addFinish(L"c.ks"), "finish c");
    require(records.addFinish(L"a.ks"), "finish a");
    require(records.hasLabel(L"a.ks", L"*x") && !records.hasLabel(L"a.ks", L"*z") && records.hasLabel(L"b.ks", L"*x") &&
        !records.hasLabel(L"b.ks", L"*y") && !records.hasLabel(L"c.ks", L"*x"), "label lookups");
    require(records.hasFinish(L"c.ks") && records.hasFinish(L"a.ks") && !records.hasFinish(L"d.ks"), "finish lookups");
    const auto block = records.serialize();
    require(hex(block) ==
        "54000000726c6461030000003a2a6381c89d1a6203000000365df2f4bee17536ce185d34c897a28cf7fb5f34c8fda58c"
        "983a5b3930f3d9ae01000000365df2f4bee17536d97a5f4fe07b95e601000000f7fb5f34c8fda58c", "record block bytes");

    // Empty save data: 4 + 8 + 4 + 652 + 1 + 4 = 673 bytes in the original.
    const auto empty = serializeSaveData(RecordDb{}, std::wstring(kEmptyState), false, CallRecords{});
    require(empty.size() == 673, "empty save size");
    require(hex(empty, 0, 16) == "08000000726c646100000000" "8c020000", "empty save header");
    require(empty[empty.size() - 5] == 0 && empty[empty.size() - 4] == 0, "empty flag and count");

    // Record-call list round trip, with the tail the original printed after re-serializing the same payload.
    CallRecords calls;
    calls.push(L"yy.ks", L"", -1);   // stored front to back: the first push ends up last
    calls.push(L"x.ks", L"*lab", 7);
    const auto saved = serializeSaveData(records, std::nullopt, true, calls);
    require(saved.size() == 155, "saved size");
    require(hex(saved, saved.size() - 53) == "002e006b00730000000a0000002a006c00610062000000070000000c000000790079002e006b0073000000020000000000ffffffff", "record-call tail");

    RecordDb restored;
    CallRecords restored_calls;
    const auto parsed = parseSaveData(saved.data(), saved.size(), restored, restored_calls);
    require(parsed.consumed == saved.size(), "consumed everything");
    require(!parsed.state_text.has_value() && parsed.flag, "flag and no state text");
    require(restored.hasLabel(L"a.ks", L"*x") && restored.hasFinish(L"c.ks") && restored.storageCount() == 3, "restored records");
    require(restored_calls.size() == 2 && restored_calls.entries()[0].storage == L"x.ks" && restored_calls.entries()[1].id == -1,
        "restored call order");
    require(serializeSaveData(restored, std::nullopt, true, restored_calls) == saved, "round trip identical");

    // State text round trip.
    const auto with_state = serializeSaveData(records, std::wstring(kEmptyState), false, CallRecords{});
    RecordDb r2;
    CallRecords c2;
    const auto parsed_state = parseSaveData(with_state.data(), with_state.size(), r2, c2);
    require(parsed_state.state_text && *parsed_state.state_text == kEmptyState, "state text");

    // r_return label: "*RET_" + label, plus "_<id>" when the id is not negative.
    CallRecords stack;
    stack.push(L"a.ks", L"*go", 3);
    stack.push(L"b.ks", L"*go", -1);
    std::wstring storage, label;
    require(stack.pop(storage, label) && storage == L"b.ks" && label == L"*RET_*go", "pop without id");
    require(stack.pop(storage, label) && storage == L"a.ks" && label == L"*RET_*go_3", "pop with id");
    require(!stack.pop(storage, label), "empty pop");

    // Bad magic clears the records but the call list and the following fields are still read (21 bytes consumed).
    std::vector<std::uint8_t> junk(64, 0);
    junk[0] = 8;
    for (int i = 4; i < 12; ++i) junk[i] = 0x11;
    RecordDb r3 = records;
    CallRecords c3 = calls;
    const auto junk_parsed = parseSaveData(junk.data(), junk.size(), r3, c3);
    require(junk_parsed.consumed == 21 && r3.storageCount() == 0 && c3.size() == 0, "junk input");

    // Truncated input is rejected instead of read past the end.
    RecordDb r4;
    CallRecords c4;
    const auto truncated = parseSaveData(saved.data(), 10, r4, c4);
    require(truncated.consumed == 0, "truncated input");

    std::puts("kag_serialize_contract ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAILED: %s\n", error.what());
    return 1;
}
