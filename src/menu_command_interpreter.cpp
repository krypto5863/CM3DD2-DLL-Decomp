#include <stdexcept>
#include <cstdlib>
#include "menu_database.h"
#include "msvc_utf8_facet.h"

#include "enum_data_internal.h"


#include <algorithm>
#include <cstring>
#include <vector>

namespace cm3d2::internal {
namespace {

std::string lowerAscii(std::string value) {
    for (auto& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch + ('a' - 'A'));
        }
    }
    return value;
}

std::int32_t lookupNumber(const EnumData& data, const std::string& text) {
    const auto found = data.by_text.find(text);
    return found == data.by_text.end() ? 0 : found->second;
}

// FUN_18006c3f0: the native converts wide text back with the recovered facet (3 bytes per unit >= 0x800, surrogate halves individually).
std::string wideToUtf8(const std::wstring& wide) {
    // These menu callers scan a terminated wide buffer, rather than converting
    // the complete std::wstring range. A decoded supplementary unit can be NUL.
    return msvc_utf8::toUtf8(wide.substr(0,wide.find(L'\0')));
}

// FUN_18010fa40 with one separator, on the WIDE string the native works on: true when the text contains it, and the first piece is what
// the caller keeps. An empty text splits into no pieces (the native then indexes an empty vector).
bool firstPiece(const std::wstring& text, const std::wstring& separator, std::wstring& piece, bool& empty_text) {
    empty_text = text.empty();
    if (empty_text) {
        return true;
    }
    const auto at = text.find(separator);
    if (at == std::wstring::npos) {
        return false;
    }
    piece = text.substr(0, at);
    return true;
}

} // namespace

bool interpretMenuCommands(const MenuDocument& document, const EnumData& categories,
    const EnumData& colors, MenuFields& fields) {
    // FUN_18010d610 resets every field of the shared interpreter state before a menu except
    // categoryMpnText (state +0xf8), which therefore carries over from the previous menu, even
    // when that menu was rejected. `fields` is the persistent state: only that text is kept.
    MenuFields out;
    out.category_mpn_text = fields.category_mpn_text;
    struct CarryBack {
        const MenuFields& out;
        MenuFields& fields;
        ~CarryBack() { fields.category_mpn_text = out.category_mpn_text; }
    } carry_back{out, fields};
    out.header_check = true;
    out.command_bytes = document.declared_command_bytes;
    out.version = document.version;
    out.source_file = document.source_file;
    out.item_name = document.item_name;
    out.category_name = document.category_name;
    out.info_text = document.info_text;
    for (const auto& command : document.commands) {
        // Native dispatch compares the decoded wide command, not its raw bytes.
        // Canonical per-unit encoding preserves those comparisons without
        // introducing a Win32 converter. Category's argument stays narrow.
        if (command.empty()) throw std::runtime_error("invalid vector subscript");
        const std::string name = msvc_utf8::toUtf8(msvc_utf8::toWide(command[0].c_str()));
        const auto need_argument = [&]() {
            if (command.size() < 2) throw std::runtime_error("invalid vector subscript");
        };
        if (name == "name") {
            need_argument();
            // FUN_18010d790: from_bytes (throws "bad conversion"), split on L" " then L"　" on the WIDE string, to_bytes of the first piece.
            const std::wstring text = msvc_utf8::toWide(command[1].c_str());
            bool empty = false;
            std::wstring piece;
            if (!firstPiece(text, L" ", piece, empty)) {
                if (!firstPiece(text, L"　", piece, empty)) {
                    piece = text;
                }
            }
            if (empty) throw std::runtime_error("invalid vector subscript");
            out.menu_name = wideToUtf8(piece);
        } else if (name == "setumei") {
            need_argument();
            // from_bytes, replace every L"《改行》" by L"\n" on the wide string, to_bytes.
            std::wstring text = msvc_utf8::toWide(command[1].c_str());
            const std::wstring marker = L"《改行》";
            for (auto at = text.find(marker); at != std::wstring::npos; at = text.find(marker)) {
                text.replace(at, marker.size(), L"\n");
            }
            out.item_info_text = wideToUtf8(text);
        } else if (name == "category") {   // narrow copy + tolower only: the native does not convert this argument
            need_argument();
            out.category_mpn_text = lowerAscii(command[1]);
            out.category_mpn = lookupNumber(categories, out.category_mpn_text);
        } else if (name == "color_set") {
            need_argument();
            out.color_set_mpn = lookupNumber(categories, lowerAscii(command[1]));
            if (command.size() > 2) {
                out.menu_name_in_color_set = lowerAscii(command[2]);
            }
        } else if (name == "tex" || name == u8"テクスチャ変更") {
            if (command.size() == 6) {
                out.multi_color_id = lookupNumber(colors, command[5]);
            }
        } else if (name == "icon" || name == "icons") {
            need_argument();
            out.icon_s = command[1];
        } else if (name == "saveitem") {
            need_argument();
            out.save_item = command[1];
        } else if (name == "unsetitem") {
            out.bo_del_only = true;
        } else if (name == "priority") {
            need_argument();
            out.priority_text = command[1];
        } else if (name == u8"メニューフォルダ") {
            need_argument();
            if (lowerAscii(command[1]) == "man") {
                out.is_man = true;
            }
        } else if (name == "collabo") {
            out.is_collabo = true;
        }
    }
    fields = out;
    return true;
}

} // namespace cm3d2::internal
