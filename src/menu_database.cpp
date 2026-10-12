#include "menu_database.h"
#include "msvc_utf8_facet.h"
#include "enum_data_internal.h"
#include "file_system_facade.h"
#include <Windows.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <vector>

namespace cm3d2::internal {
namespace {
std::string wideToUtf8(const std::wstring& wide) {
    return msvc_utf8::toUtf8(wide.substr(0,wide.find(L'\0')));
}
} // namespace

MenuDatabase::~MenuDatabase() {
    // FUN_18010d470: an unfinished analysis is cancelled and waited for.
    if (started_) {
        cancelled_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
    }
}

void MenuDatabase::start(const void* wide_interface) {
    if (finished_ || started_) {
        return;
    }
    started_ = true;
    worker_ = std::thread([this, wide_interface]() { run(wide_interface); });
}

void MenuDatabase::setIndex(std::int32_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size()) {
        return;  // the original reaches a nonreturning vector-subscript error
    }
    selected_ = records_[static_cast<std::size_t>(index)].get();
}

const std::string* MenuDatabase::fileName(std::uint64_t hash) const {
    const auto found = names_.find(hash);
    return found == names_.end() ? nullptr : &found->second.file_name;
}

const std::string* MenuDatabase::parentFileName(std::uint64_t hash) const {
    const auto found = names_.find(hash);
    return found == names_.end() ? nullptr : &found->second.parent;
}

// FUN_18010fde0.
void MenuDatabase::run(const void* wide_interface) {
    if (cancelled_) {
        finished_ = true;
        return;
    }
    const auto contains = [](const std::wstring& name, const wchar_t* part) {
        return name.find(part) != std::wstring::npos;
    };
    const auto skipped = [&](const std::wstring& name) {
        return contains(name, L"/man") || contains(name, L"_zurashi") || contains(name, L"_mekure");
    };
    const auto menus = fileSystemMenuNames(wide_interface);
    if (menus.empty()) {
        for (const auto& name : fileSystemListWide(wide_interface, L"", 3)) {
            if (cancelled_) {
                finished_ = true;
                return;
            }
            const bool menu_file = name.size() >= 5 && name.compare(name.size() - 5, 5, L".menu") == 0;
            if (menu_file && !skipped(name)) {
                load(wide_interface, name);
            }
        }
    } else {
        for (const auto& name : menus) {
            if (cancelled_) {
                finished_ = true;
                return;
            }
            if (!skipped(name)) {
                load(wide_interface, name);
            }
        }
    }
    finished_ = true;
}

// FUN_18010ed30.
void MenuDatabase::load(const void* wide_interface, const std::wstring& name) {
    const auto slash = name.rfind(L'\\');
    const std::wstring base = slash == std::wstring::npos ? name : name.substr(slash + 1);
    std::unique_ptr<IFile> file(fileSystemOpenWide(wide_interface, base));
    if (!file) {
        return;
    }
    const std::uint64_t hash = menuNativeHash(std::u16string_view(reinterpret_cast<const char16_t*>(base.data()), base.size()));
    std::vector<std::uint8_t> bytes;
    const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(file->data());
    const std::uint64_t size = file->size();
    if (data == nullptr) {
        bytes.resize(static_cast<std::size_t>(size));
        file->readAt(bytes.data(), 0, size);
        data = bytes.data();
    }
    MenuDocument document;
    try {
        if (!decodeMenu(data, static_cast<std::size_t>(size), document) ||
            !interpretMenuCommands(document, *categories_, *colors_, interpreter_state_) || interpreter_state_.icon_s.empty()) {
            return;
        }
    } catch (const std::exception& error) {
        // The catch funclet at 0x1801b18f0: a modal MessageBoxW titled "pre menu parse error !" with the file
        // name, then a MessageBoxA titled "error" with what(); the menu is skipped and the scan continues.
        // CM3D2_NO_UI_DIALOGS=1 (not in the original) suppresses the dialogs for automated runs.
        if (GetEnvironmentVariableA("CM3D2_NO_UI_DIALOGS", nullptr, 0) == 0) {
            MessageBoxW(nullptr, base.c_str(), L"pre menu parse error !", MB_ICONERROR);
            MessageBoxA(nullptr, error.what(), "error", MB_ICONERROR);
        }
        return;
    }

    const MenuFields& fields = interpreter_state_;
    auto record = std::make_unique<MenuRecord>();
    record->hash = hash;
    record->header_check = fields.header_check ? 1 : 0;
    record->command_bytes = fields.command_bytes;
    record->version = fields.version;
    record->category_mpn = fields.category_mpn;
    record->color_set_mpn = fields.color_set_mpn;
    record->multi_color_id = fields.multi_color_id;
    record->bo_del_only = fields.bo_del_only ? 1 : 0;
    record->is_man = fields.is_man ? 1 : 0;
    record->is_collabo = fields.is_collabo ? 1 : 0;
    const std::string* slots[11] = {&fields.source_file, &fields.item_name, &fields.category_name,
        &fields.info_text, &fields.menu_name, &fields.item_info_text, &fields.category_mpn_text,
        &fields.menu_name_in_color_set, &fields.icon_s, &fields.save_item, &fields.priority_text};
    for (int i = 0; i < 11; ++i) {
        record->offsets[i] = static_cast<std::uint16_t>(record->pool.size());
        record->pool += *slots[i];
        record->pool.push_back('\0');
    }

    // The parent file name drops an "_z..." part: name_zpart_rest.menu -> name_rest.menu.
    const std::string file_name = wideToUtf8(base);
    std::string parent;
    if (file_name.size() > 1) {
        std::size_t at = file_name.find('_');
        while (at != std::string::npos && at < file_name.size() - 1) {
            if (file_name[at + 1] == 'z') {
                std::size_t next = std::string::npos;
                if (file_name.size() > at + 1) {
                    next = file_name.find('_', at + 1);
                    if (next == std::string::npos) {
                        next = file_name.find('.', at + 1);
                    }
                }
                if (next != std::string::npos && at < next && next != file_name.size()) {
                    parent = file_name.substr(0, at) + file_name.substr(next);
                }
                break;
            }
            at = file_name.find('_', at + 1);
            if (at != std::string::npos && at >= file_name.size() - 1) {
                break;
            }
        }
    }
    names_.try_emplace(hash, Names{std::move(parent), file_name});
    records_.push_back(std::move(record));
}

} // namespace cm3d2::internal
