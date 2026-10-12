#pragma once

// Owned valid WARC fixtures for contracts/oracle tools. No game data, WARP,
// native pointers, or decoder error cases are used here.
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace synthetic_warc {
using Bytes = std::vector<unsigned char>;
inline void u32(Bytes& b, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
inline void u64(Bytes& b, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
inline std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("invalid fixture UTF-8");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
inline std::uint64_t key(const std::string& name, bool utf8) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    if (utf8) {
        for (unsigned char c : name) h = (h ^ c) * 0x100000001b3ULL;
    } else {
        for (wchar_t c : wide(name)) {
            h = (h ^ (c & 255)) * 0x100000001b3ULL;
            h = (h ^ ((c >> 8) & 255)) * 0x100000001b3ULL;
        }
    }
    return h ^ (h >> 32);
}
inline Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }
inline Bytes plain(const Bytes& data) {
    Bytes out;
    u32(out, 0); u32(out, 0);
    u32(out, static_cast<std::uint32_t>(data.size()));
    u32(out, static_cast<std::uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}
inline void menuString(Bytes& b, const std::string& text) {
    if (text.size() >= 128) throw std::runtime_error("fixture menu string too long");
    b.push_back(static_cast<unsigned char>(text.size()));
    b.insert(b.end(), text.begin(), text.end());
}
inline Bytes menu(const std::string& label) {
    Bytes body;
    body.push_back(2); menuString(body, "name"); menuString(body, label);
    body.push_back(2); menuString(body, "icon"); menuString(body, "fixture.tex");
    body.push_back(0);
    Bytes out;
    menuString(out, "CM3D2_MENU"); u32(out, 1000);
    menuString(out, "fixture"); menuString(out, label);
    menuString(out, "wear"); menuString(out, "fixture-info");
    u32(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
// Valid zlib stream made from uncompressed DEFLATE blocks; no third-party writer.
inline Bytes zlibStored(const Bytes& data) {
    Bytes out{0x78, 0x01};
    std::size_t at = 0;
    do {
        const auto count = std::min<std::size_t>(65535, data.size() - at);
        out.push_back(at + count == data.size() ? 1 : 0);
        const auto n = static_cast<std::uint16_t>(count);
        const auto inverse = static_cast<std::uint16_t>(~n);
        out.push_back(static_cast<unsigned char>(n)); out.push_back(static_cast<unsigned char>(n >> 8));
        out.push_back(static_cast<unsigned char>(inverse)); out.push_back(static_cast<unsigned char>(inverse >> 8));
        out.insert(out.end(), data.begin()+at, data.begin()+at+count);
        at += count;
    } while (at < data.size());
    std::uint32_t a = 1, b = 0;
    for (unsigned char c : data) { a = (a+c)%65521; b = (b+a)%65521; }
    const auto adler = (b << 16) | a;
    for (int i=3; i>=0; --i) out.push_back(static_cast<unsigned char>(adler >> (8*i)));
    return out;
}
struct Member {
    std::string path; Bytes data; bool compressed = false; bool xor_prefix = false;
    Bytes encoded_override; std::uint32_t crypt_override = 0;
};
struct Node {
    std::map<std::string, Node> directories;
    std::map<std::string, std::uint64_t> files;
};
inline Bytes table(const Node& node, bool utf8, std::uint64_t id = 0) {
    using Pair = std::pair<std::uint64_t, std::string>;
    std::vector<Pair> dirs, files;
    for (const auto& d : node.directories) dirs.emplace_back(key(d.first, utf8), d.first);
    for (const auto& f : node.files) files.emplace_back(key(f.first, utf8), f.first);
    std::sort(dirs.begin(), dirs.end()); std::sort(files.begin(), files.end());
    Bytes records, children;
    const std::size_t head = 24 + 16 * (dirs.size() + files.size());
    for (const auto& d : dirs) {
        u64(records, d.first); u64(records, head + children.size());
        const Bytes child = table(node.directories.at(d.second), utf8, d.first);
        children.insert(children.end(), child.begin(), child.end());
    }
    for (const auto& f : files) { u64(records, f.first); u64(records, node.files.at(f.second)); }
    Bytes out;
    u32(out, 24); u32(out, 16); u64(out, id);
    u32(out, static_cast<std::uint32_t>(dirs.size()));
    u32(out, static_cast<std::uint32_t>(files.size()));
    out.insert(out.end(), records.begin(), records.end());
    out.insert(out.end(), children.begin(), children.end());
    return out;
}
inline Bytes archive(const std::vector<Member>& members) {
    Node root;
    Bytes data, names;
    std::vector<std::string> seen;
    for (const auto& member : members) {
        Node* node = &root;
        std::size_t at = 0;
        while (true) {
            const auto slash = member.path.find('/', at);
            const std::string component = member.path.substr(at, slash == std::string::npos ? slash : slash - at);
            if (component.empty()) throw std::runtime_error("empty fixture component");
            if (std::find(seen.begin(), seen.end(), component) == seen.end()) {
                seen.push_back(component);
                u64(names, key(component, false));
                const auto w = wide(component);
                u32(names, static_cast<std::uint32_t>(w.size()));
                for (wchar_t c : w) { names.push_back(static_cast<unsigned char>(c)); names.push_back(static_cast<unsigned char>(c >> 8)); }
            }
            if (slash == std::string::npos) { node->files.emplace(component, data.size()); break; }
            node = &node->directories[component]; at = slash + 1;
        }
        Bytes encoded = member.compressed ? zlibStored(member.data) : member.data;
        if (!member.encoded_override.empty()) encoded = member.encoded_override;
        if (member.xor_prefix)
            for (std::size_t i=0; i<std::min<std::size_t>(16,encoded.size()); ++i) encoded[i] ^= 0xcf;
        Bytes payload;
        u32(payload, member.compressed ? 1 : 0);
        u32(payload, member.crypt_override ? member.crypt_override : (member.xor_prefix ? 1 : 0));
        u32(payload, static_cast<std::uint32_t>(member.data.size()));
        u32(payload, static_cast<std::uint32_t>(encoded.size()));
        payload.insert(payload.end(), encoded.begin(), encoded.end());
        data.insert(data.end(), payload.begin(), payload.end());
    }
    Bytes out{0x77, 0x61, 0x72, 0x63, 0xff, 0xaa, 0x45, 0xf1};
    u32(out, 1000); u32(out, 4);
    const Bytes sections[] = {table(root, false), table(root, true), data, plain(names)};
    for (std::uint32_t tag = 0; tag < 4; ++tag) {
        u32(out, tag); u64(out, sections[tag].size());
        out.insert(out.end(), sections[tag].begin(), sections[tag].end());
    }
    return out;
}
inline void write(const std::filesystem::path& path, const Bytes& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f) throw std::runtime_error("fixture write failed");
}
inline void fixtures(const std::filesystem::path& dir) {
    write(dir / "a.arc", archive({
        {"same.txt", text("A-root")}, {"same.ks", text("A-root\n")},
        {"only-a.txt", text("A-only")}, {"noext", text("A-noext")},
        {"root.menu", menu("A-menu")}, {"sub/same.txt", text("A-sub")},
        {"sub/same.ks", text("A-sub\n")}, {"sub/choose.txt", text("A-sub-choice")},
        {"sub/inner/choose.txt", text("A-inner-choice")}, {"sub/inner/inner.ks", text("A-inner\n")},
        {"sub/nested.menu", menu("A-nested-menu")}, {"other/choose.txt", text("A-other-choice")},
        {u8"\u65e5\u672c.txt", text("A-unicode")}, {u8"\u65e5\u672c.ks", text("A-unicode\n")}}));
    write(dir / "b.arc", archive({
        {"same.txt", text("B-root")}, {"same.ks", text("B-root\n")},
        {"only-b.txt", text("B-only")}, {"root.menu", menu("B-menu")},
        {"sub/same.txt", text("B-sub")}, {"sub/same.ks", text("B-sub\n")},
        {"sub/choose.txt", text("B-sub-choice")}, {"other/choose.txt", text("B-other-choice")},
        {"other/other.ks", text("B-other\n")}, {"other/other.menu", menu("B-other-menu")}}));
    write(dir / "c.arc", archive({{"only-c.txt", text("C-only")}, {"sub/c.ks", text("C-sub\n")}}));
    write(dir / "empty.arc", archive({}));
    write(dir / "plain.txt", text("plain text, not an archive"));
}
} // namespace synthetic_warc
