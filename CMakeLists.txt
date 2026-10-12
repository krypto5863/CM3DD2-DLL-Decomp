cmake_minimum_required(VERSION 3.21)

project(cm3d2_reconstruction LANGUAGES C CXX)

# The original DLL imports only KERNEL32, USER32, DINPUT8, HID and SETUPAPI (static CRT). Link the C/C++ runtime statically
# too, so the replacement loads on a machine without the Visual C++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "This reconstruction currently targets the observed Windows x64 DLL only")
endif()

# Vendored TJS2 core. Keep upstream's legacy compiler settings separate from
# the stricter reconstructed wrappers. The native exposed engine does not register
# RegExp; keep that configuration instead of enabling vendor-only behavior.
file(GLOB TJS2_SOURCES CONFIGURE_DEPENDS third_party/tjs2/*.cpp)
add_library(tjs2_core STATIC ${TJS2_SOURCES} src/tjs_platform.cpp)
target_include_directories(tjs2_core SYSTEM PUBLIC third_party/tjs2)
target_compile_features(tjs2_core PUBLIC cxx_std_17)
target_compile_definitions(tjs2_core PUBLIC __WIN32__ TJS_NO_REGEXP NOMINMAX UNICODE _UNICODE)
target_compile_definitions(tjs2_core PRIVATE CM3D2_TJS_NATIVE_BUILTINS)
if(MSVC)
    target_compile_options(tjs2_core PRIVATE /W0 /utf-8)
    target_compile_definitions(tjs2_core PRIVATE _HAS_AUTO_PTR_ETC=1)
endif()

# Embedded Kirikiri KAGParser (open-source component, see third_party/kagparser) plus the host shims it needs.
add_library(kag_parser STATIC third_party/kagparser/KAGParser.cpp src/kag_host.cpp src/kag_serialize.cpp src/kag_script.cpp src/kag_api.cpp)
target_include_directories(kag_parser PUBLIC third_party/kagparser/shims include PRIVATE src third_party/kagparser)
target_link_libraries(kag_parser PUBLIC tjs2_core)
target_compile_definitions(kag_parser PRIVATE CM3D2_RECONSTRUCTION_BUILD WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_features(kag_parser PUBLIC cxx_std_17)
if(MSVC)
    set_source_files_properties(third_party/kagparser/KAGParser.cpp PROPERTIES COMPILE_OPTIONS "/W0;/utf-8;/FIkag_prefix.h")
    target_compile_definitions(kag_parser PRIVATE _HAS_AUTO_PTR_ETC=1)
    set_source_files_properties(src/kag_host.cpp src/kag_serialize.cpp PROPERTIES COMPILE_OPTIONS "/W4;/utf-8")
    set_source_files_properties(src/kag_script.cpp src/kag_api.cpp PROPERTIES COMPILE_OPTIONS "/W4;/utf-8;/FIkag_prefix.h")
endif()

add_library(cm3d2_reconstructed SHARED
    src/anm_api.cpp
    src/anm_format.cpp
    src/file_system_delegate_api.cpp
    src/file_system_wide_abi.cpp
    src/file_system_map_abi.cpp
    src/file_system_narrow_abi.cpp
    src/tjs_variant_api.cpp
    src/tjs_api.cpp
    src/tjs_messages.cpp
    src/csv_api.cpp
    src/enum_data_api.cpp
    src/file_api.cpp
    src/file_system_api.cpp
    src/warc_archive.cpp
    src/warp_archive.cpp
    src/file_system_windows.cpp
    src/file_system_archive.cpp
    src/menu_database.cpp
    src/menu_command_interpreter.cpp
    src/menu_api.cpp
    src/hole_device.cpp
    src/msvc_ordered_map.cpp
    src/menu_format.cpp
    src/ogg_api.cpp
    src/stb_vorbis_impl.c
    src/stb_image_impl.c
    exports/cm3d2_file.def
)

target_include_directories(cm3d2_reconstructed PUBLIC include)
target_link_libraries(cm3d2_reconstructed PRIVATE bcrypt setupapi hid dinput8 dxguid tjs2_core kag_parser)
target_compile_features(cm3d2_reconstructed PUBLIC cxx_std_17)
target_compile_definitions(cm3d2_reconstructed PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    CM3D2_RECONSTRUCTION_BUILD
)

if(MSVC)
    target_compile_options(cm3d2_reconstructed PRIVATE /W4 /permissive- /utf-8)
endif()

# Third-party decoder: keep its warnings out of the reconstruction build.
set_source_files_properties(src/stb_vorbis_impl.c src/stb_image_impl.c PROPERTIES COMPILE_OPTIONS "/W0")

include(CTest)
if(BUILD_TESTING)
    add_executable(warc_owned_stream_contract tests/warc_owned_stream_contract.cpp
        src/warc_archive.cpp src/msvc_ordered_map.cpp src/stb_image_impl.c)
    target_compile_features(warc_owned_stream_contract PRIVATE cxx_std_17)
    target_compile_definitions(warc_owned_stream_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(warc_owned_stream_contract PRIVATE src include)
    target_link_libraries(warc_owned_stream_contract PRIVATE bcrypt)
    add_test(NAME warc_owned_stream_contract COMMAND warc_owned_stream_contract)
    add_executable(archive_decode_probe tools/differential/archive_decode_probe.cpp)
    target_compile_features(archive_decode_probe PRIVATE cxx_std_17)
    target_compile_definitions(archive_decode_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(archive_decode_probe PRIVATE bcrypt)
    add_executable(file_system_wide_abi_contract tests/file_system_wide_abi_contract.cpp src/file_system_wide_abi.cpp)
    target_include_directories(file_system_wide_abi_contract PRIVATE src)
    target_compile_features(file_system_wide_abi_contract PRIVATE cxx_std_17)
    add_test(NAME file_system_wide_abi_contract COMMAND file_system_wide_abi_contract)
    add_executable(file_system_map_abi_contract tests/file_system_map_abi_contract.cpp
        src/file_system_map_abi.cpp src/msvc_ordered_map.cpp)
    target_include_directories(file_system_map_abi_contract PRIVATE src)
    target_compile_features(file_system_map_abi_contract PRIVATE cxx_std_17)
    target_compile_definitions(file_system_map_abi_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME file_system_map_abi_contract COMMAND file_system_map_abi_contract)
    add_executable(file_system_callable_contract tests/file_system_callable_contract.cpp)
    target_include_directories(file_system_callable_contract PRIVATE src)
    target_compile_features(file_system_callable_contract PRIVATE cxx_std_17)
    target_compile_definitions(file_system_callable_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(file_system_callable_contract PRIVATE cm3d2_reconstructed)
    add_test(NAME file_system_callable_contract COMMAND file_system_callable_contract)
    set_tests_properties(file_system_callable_contract PROPERTIES TIMEOUT 10)
    add_executable(file_system_conversion_api_contract tests/file_system_conversion_api_contract.cpp)
    target_include_directories(file_system_conversion_api_contract PRIVATE src)
    target_compile_features(file_system_conversion_api_contract PRIVATE cxx_std_17)
    target_compile_definitions(file_system_conversion_api_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(file_system_conversion_api_contract PRIVATE cm3d2_reconstructed)
    add_test(NAME file_system_conversion_api_contract COMMAND file_system_conversion_api_contract)
    set_tests_properties(file_system_conversion_api_contract PROPERTIES TIMEOUT 10)
    add_executable(file_system_narrow_abi_contract tests/file_system_narrow_abi_contract.cpp
        src/file_system_narrow_abi.cpp src/file_system_wide_abi.cpp)
    target_include_directories(file_system_narrow_abi_contract PRIVATE src)
    target_compile_features(file_system_narrow_abi_contract PRIVATE cxx_std_17)
    add_test(NAME file_system_narrow_abi_contract COMMAND file_system_narrow_abi_contract)
    add_executable(file_system_narrow_conversion_contract tests/file_system_narrow_conversion_contract.cpp)
    target_include_directories(file_system_narrow_conversion_contract PRIVATE src)
    target_compile_features(file_system_narrow_conversion_contract PRIVATE cxx_std_17)
    add_test(NAME file_system_narrow_conversion_contract COMMAND file_system_narrow_conversion_contract)
    add_executable(file_system_archive_hash_contract tests/file_system_archive_hash_contract.cpp
        src/file_system_archive.cpp src/file_system_windows.cpp src/msvc_ordered_map.cpp
        src/warc_archive.cpp src/stb_image_impl.c src/file_system_narrow_abi.cpp)
    target_include_directories(file_system_archive_hash_contract PRIVATE src include)
    target_compile_definitions(file_system_archive_hash_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_features(file_system_archive_hash_contract PRIVATE cxx_std_17)
    target_link_libraries(file_system_archive_hash_contract PRIVATE bcrypt)
    add_test(NAME file_system_archive_hash_contract COMMAND file_system_archive_hash_contract)
    add_executable(archive_autopath_conversion_contract tests/archive_autopath_conversion_contract.cpp
        src/file_system_archive.cpp src/file_system_windows.cpp src/msvc_ordered_map.cpp
        src/warc_archive.cpp src/stb_image_impl.c)
    target_include_directories(archive_autopath_conversion_contract PRIVATE src include)
    target_compile_definitions(archive_autopath_conversion_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_features(archive_autopath_conversion_contract PRIVATE cxx_std_17)
    target_link_libraries(archive_autopath_conversion_contract PRIVATE bcrypt)
    add_test(NAME archive_autopath_conversion_contract COMMAND archive_autopath_conversion_contract)
    if(MSVC)
        target_compile_options(file_system_narrow_abi_contract PRIVATE /W4 /utf-8)
        target_compile_options(file_system_narrow_conversion_contract PRIVATE /W4 /utf-8)
        target_compile_options(file_system_archive_hash_contract PRIVATE /W4 /utf-8)
    endif()
    add_executable(fs_archive_job_probe tools/differential/fs_archive_job_probe.cpp)
    target_compile_features(fs_archive_job_probe PRIVATE cxx_std_17)
    target_compile_definitions(fs_archive_job_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_executable(fs_archive_job_internal_probe tools/differential/fs_archive_job_probe.cpp
        src/file_system_archive.cpp src/file_system_windows.cpp src/msvc_ordered_map.cpp src/warc_archive.cpp src/stb_image_impl.c)
    target_compile_features(fs_archive_job_internal_probe PRIVATE cxx_std_17)
    target_compile_definitions(fs_archive_job_internal_probe PRIVATE ARCHIVE_JOB_INTERNAL WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(fs_archive_job_internal_probe PRIVATE src include)
    target_link_libraries(fs_archive_job_internal_probe PRIVATE bcrypt)
    add_executable(file_system_archive_job_contract tests/file_system_archive_job_contract.cpp
        src/file_system_archive.cpp src/file_system_windows.cpp src/msvc_ordered_map.cpp src/warc_archive.cpp src/stb_image_impl.c)
    target_compile_features(file_system_archive_job_contract PRIVATE cxx_std_17)
    target_compile_definitions(file_system_archive_job_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(file_system_archive_job_contract PRIVATE src include)
    target_link_libraries(file_system_archive_job_contract PRIVATE bcrypt)
    add_test(NAME file_system_archive_job_contract COMMAND file_system_archive_job_contract)
    set_tests_properties(file_system_archive_job_contract PROPERTIES TIMEOUT 10)
    add_executable(fs_job_probe tools/differential/fs_job_probe.cpp)
    target_compile_features(fs_job_probe PRIVATE cxx_std_17)
    target_compile_definitions(fs_job_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_executable(fs_job_internal_probe tools/differential/fs_job_probe.cpp
        src/file_system_windows.cpp src/msvc_ordered_map.cpp src/warc_archive.cpp src/stb_image_impl.c)
    target_compile_features(fs_job_internal_probe PRIVATE cxx_std_17)
    target_include_directories(fs_job_internal_probe PRIVATE src include)
    target_compile_definitions(fs_job_internal_probe PRIVATE FS_JOB_INTERNAL WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(fs_job_internal_probe PRIVATE bcrypt)
    add_executable(file_system_windows_job_contract tests/file_system_windows_job_contract.cpp
        src/file_system_windows.cpp src/msvc_ordered_map.cpp src/warc_archive.cpp src/stb_image_impl.c)
    target_compile_features(file_system_windows_job_contract PRIVATE cxx_std_17)
    target_include_directories(file_system_windows_job_contract PRIVATE src include)
    target_compile_definitions(file_system_windows_job_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(file_system_windows_job_contract PRIVATE bcrypt)
    add_test(NAME file_system_windows_job_contract COMMAND file_system_windows_job_contract)
    set_tests_properties(file_system_windows_job_contract PROPERTIES TIMEOUT 10)
    # Claude's internal hierarchy golden contract; no original DLL/game data.
    add_executable(file_system_tree_contract tools/differential/fs_tree_battery.cpp
        src/file_system_windows.cpp src/msvc_ordered_map.cpp src/warc_archive.cpp src/stb_image_impl.c)
    target_compile_definitions(file_system_tree_contract PRIVATE TREE_BATTERY_INTERNAL WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(file_system_tree_contract PRIVATE src include)
    target_link_libraries(file_system_tree_contract PRIVATE bcrypt)
    target_compile_features(file_system_tree_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(file_system_tree_contract PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME file_system_tree_contract COMMAND file_system_tree_contract
        ${CMAKE_BINARY_DIR}/tree-contract-work golden ${CMAKE_SOURCE_DIR}/tests/data/fs_tree_golden_transcript.txt)
    add_executable(file_system_archive_synthetic_contract tests/file_system_archive_synthetic_contract.cpp)
    target_link_libraries(file_system_archive_synthetic_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(file_system_archive_synthetic_contract PRIVATE cxx_std_17)
    target_compile_definitions(file_system_archive_synthetic_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(file_system_archive_synthetic_contract PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME file_system_archive_synthetic_contract COMMAND file_system_archive_synthetic_contract)
    add_executable(fs_archive_battery tools/differential/fs_archive_battery.cpp)
    target_compile_features(fs_archive_battery PRIVATE cxx_std_17)
    target_compile_definitions(fs_archive_battery PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(fs_archive_battery PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_executable(file_system_job_state_contract tests/file_system_job_state_contract.cpp)
    target_include_directories(file_system_job_state_contract PRIVATE src)
    target_compile_definitions(file_system_job_state_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_features(file_system_job_state_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(file_system_job_state_contract PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME file_system_job_state_contract COMMAND file_system_job_state_contract)
    add_executable(archive_member_file_contract tests/archive_member_file_contract.cpp src/file_api.cpp)
    target_include_directories(archive_member_file_contract PRIVATE src include)
    target_compile_features(archive_member_file_contract PRIVATE cxx_std_17)
    target_compile_definitions(archive_member_file_contract PRIVATE CM3D2_RECONSTRUCTION_BUILD WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(archive_member_file_contract PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME archive_member_file_contract COMMAND archive_member_file_contract)
    add_executable(file_system_last_folder_contract tests/file_system_last_folder_contract.cpp)
    target_link_libraries(file_system_last_folder_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(file_system_last_folder_contract PRIVATE cxx_std_17)
    add_test(NAME file_system_last_folder_contract COMMAND file_system_last_folder_contract)
    add_executable(file_system_folder_order_contract tests/file_system_folder_order_contract.cpp)
    target_link_libraries(file_system_folder_order_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(file_system_folder_order_contract PRIVATE cxx_std_17)
    add_test(NAME file_system_folder_order_contract COMMAND file_system_folder_order_contract)
    add_executable(anm_contract tests/anm_contract.cpp)
    target_link_libraries(anm_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(anm_contract PRIVATE cxx_std_17)
    add_test(NAME anm_contract COMMAND anm_contract)

    add_executable(file_system_delegate_contract tests/file_system_delegate_contract.cpp)
    target_link_libraries(file_system_delegate_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(file_system_delegate_contract PRIVATE cxx_std_17)
    target_include_directories(file_system_delegate_contract PRIVATE src)
    add_test(NAME file_system_delegate_contract COMMAND file_system_delegate_contract)

    add_executable(tjs_variant_contract tests/tjs_variant_contract.cpp)
    target_link_libraries(tjs_variant_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(tjs_variant_contract PRIVATE cxx_std_17)
    add_test(NAME tjs_variant_contract COMMAND tjs_variant_contract)

    add_executable(tjs_script_contract tests/tjs_script_contract.cpp)
    target_link_libraries(tjs_script_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(tjs_script_contract PRIVATE cxx_std_17)
    add_test(NAME tjs_script_contract COMMAND tjs_script_contract)

    add_executable(tjs_regexp_contract tests/tjs_regexp_contract.cpp)
    target_link_libraries(tjs_regexp_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(tjs_regexp_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(tjs_regexp_contract PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME tjs_regexp_contract COMMAND tjs_regexp_contract)

    add_executable(tjs_dictionary_contract tests/tjs_dictionary_contract.cpp)
    target_link_libraries(tjs_dictionary_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(tjs_dictionary_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(tjs_dictionary_contract PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME tjs_dictionary_contract COMMAND tjs_dictionary_contract)

    add_executable(tjs_array_sort_contract tests/tjs_array_sort_contract.cpp)
    target_link_libraries(tjs_array_sort_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(tjs_array_sort_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(tjs_array_sort_contract PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME tjs_array_sort_contract COMMAND tjs_array_sort_contract)

    # One VM copy: direct reconstructed heap/temporary ownership checks.
    add_executable(tjs_array_sort_structural_contract tests/tjs_array_sort_structural_contract.cpp)
    target_link_libraries(tjs_array_sort_structural_contract PRIVATE tjs2_core)
    target_compile_features(tjs_array_sort_structural_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(tjs_array_sort_structural_contract PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME tjs_array_sort_structural_contract COMMAND tjs_array_sort_structural_contract)

    # Structural ownership checks run against a single copy of the VM core.
    add_executable(tjs_payload_lifetime_contract tests/tjs_payload_lifetime_contract.cpp)
    target_link_libraries(tjs_payload_lifetime_contract PRIVATE tjs2_core)
    target_compile_features(tjs_payload_lifetime_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(tjs_payload_lifetime_contract PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME tjs_payload_lifetime_contract COMMAND tjs_payload_lifetime_contract)

    # API-owned variants stay in the DLL's VM. The static core supplies only the
    # inline accessor's error helper; this test never creates a second core VM.
    add_executable(tjs_finalizer_contract tests/tjs_finalizer_contract.cpp)
    target_link_libraries(tjs_finalizer_contract PRIVATE cm3d2_reconstructed tjs2_core)
    target_compile_features(tjs_finalizer_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(tjs_finalizer_contract PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME tjs_finalizer_contract COMMAND tjs_finalizer_contract)

    add_executable(tjs_array_text_contract tests/tjs_array_text_contract.cpp)
    target_link_libraries(tjs_array_text_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(tjs_array_text_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(tjs_array_text_contract PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME tjs_array_text_contract COMMAND tjs_array_text_contract)

    add_executable(ogg_contract tests/ogg_contract.cpp)
    target_link_libraries(ogg_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(ogg_contract PRIVATE cxx_std_17)
    add_test(NAME ogg_contract COMMAND ogg_contract)
    set_tests_properties(ogg_contract PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(file_system_archive_contract tests/file_system_archive_contract.cpp)
    target_link_libraries(file_system_archive_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(file_system_archive_contract PRIVATE cxx_std_17)
    add_test(NAME file_system_archive_contract COMMAND file_system_archive_contract)
    set_tests_properties(file_system_archive_contract PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(warp_archive_contract tests/warp_archive_contract.cpp)
    target_link_libraries(warp_archive_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(warp_archive_contract PRIVATE cxx_std_17)
    add_test(NAME warp_archive_contract COMMAND warp_archive_contract)
    set_tests_properties(warp_archive_contract PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(abi_handle_contract tests/abi_handle_contract.cpp)
    target_link_libraries(abi_handle_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(abi_handle_contract PRIVATE cxx_std_17)
    add_test(NAME abi_handle_contract COMMAND abi_handle_contract)

    add_executable(enum_data_contract tests/enum_data_contract.cpp)
    target_link_libraries(enum_data_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(enum_data_contract PRIVATE cxx_std_17)
    add_test(NAME enum_data_contract COMMAND enum_data_contract)

    add_executable(menu_format_contract tests/menu_format_contract.cpp src/menu_format.cpp)
    target_include_directories(menu_format_contract PRIVATE src)
    target_include_directories(menu_format_contract PRIVATE include)
    target_compile_features(menu_format_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(menu_format_contract PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME menu_format_contract COMMAND menu_format_contract)

    add_executable(file_memory_contract tests/file_memory_contract.cpp)
    target_link_libraries(file_memory_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(file_memory_contract PRIVATE cxx_std_17)
    add_test(NAME file_memory_contract COMMAND file_memory_contract)

    add_executable(file_system_contract tests/file_system_contract.cpp)
    target_link_libraries(file_system_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(file_system_contract PRIVATE cxx_std_17)
    add_test(NAME file_system_contract COMMAND file_system_contract)
    add_executable(menu_database_contract tests/menu_database_contract.cpp)
    target_link_libraries(menu_database_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(menu_database_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(menu_database_contract PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME menu_database_contract COMMAND menu_database_contract)
    add_executable(menu_command_error_contract tests/menu_command_error_contract.cpp src/menu_command_interpreter.cpp)
    target_include_directories(menu_command_error_contract PRIVATE src include)
    target_compile_features(menu_command_error_contract PRIVATE cxx_std_17)
    add_test(NAME menu_command_error_contract COMMAND menu_command_error_contract)
    if(MSVC)
        target_compile_options(menu_command_error_contract PRIVATE /W4 /utf-8)
    endif()
    set_tests_properties(menu_database_contract PROPERTIES ENVIRONMENT "CM3D2_NO_UI_DIALOGS=1" TIMEOUT 20)
    add_executable(kag_serialize_contract tests/kag_serialize_contract.cpp src/kag_serialize.cpp)
    target_include_directories(kag_serialize_contract PRIVATE src)
    target_compile_features(kag_serialize_contract PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(kag_serialize_contract PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME kag_serialize_contract COMMAND kag_serialize_contract)

    add_executable(kag_script_contract tests/kag_script_contract.cpp)
    target_link_libraries(kag_script_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(kag_script_contract PRIVATE cxx_std_17)
    add_test(NAME kag_script_contract COMMAND kag_script_contract)

    add_executable(hole_device_contract tests/hole_device_contract.cpp)
    target_link_libraries(hole_device_contract PRIVATE cm3d2_reconstructed)
    target_compile_features(hole_device_contract PRIVATE cxx_std_17)
    add_test(NAME hole_device_contract COMMAND hole_device_contract)

    add_executable(csv_contract tests/csv_contract.cpp)
    target_link_libraries(csv_contract PRIVATE cm3d2_reconstructed)
    target_link_libraries(csv_contract PRIVATE bcrypt)
    target_compile_features(csv_contract PRIVATE cxx_std_17)
    add_test(NAME csv_contract COMMAND csv_contract)
endif()
