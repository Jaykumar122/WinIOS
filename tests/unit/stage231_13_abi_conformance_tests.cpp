#include "winios_sdk/winios_sdk.h"
#include "kernel/user_syscall_dispatcher.h"
#include "settings/settings_types.h"

#include <cassert>
#include <cstddef>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

// 1. Static Layout & Offset Assertions for Category C ABI v2.4 (Syscalls 581..593)
static void test_abi_layout_and_offsets() {
    std::cout << "[Test 1] Category C ABI v2.4 layout and field offset assertions...\n";

    // Syscall 581 & 582: winios_settings_snapshot_t (48 bytes)
    static_assert(sizeof(winios_settings_snapshot_t) == 48, "winios_settings_snapshot_t size must be 48");
    static_assert(offsetof(winios_settings_snapshot_t, theme_mode) == 0);
    static_assert(offsetof(winios_settings_snapshot_t, accent_color) == 4);
    static_assert(offsetof(winios_settings_snapshot_t, display_width) == 8);
    static_assert(offsetof(winios_settings_snapshot_t, display_height) == 12);
    static_assert(offsetof(winios_settings_snapshot_t, display_scale) == 16);
    static_assert(offsetof(winios_settings_snapshot_t, display_orientation) == 20);
    static_assert(offsetof(winios_settings_snapshot_t, master_volume) == 24);
    static_assert(offsetof(winios_settings_snapshot_t, is_muted) == 28);
    static_assert(offsetof(winios_settings_snapshot_t, low_power_mode) == 32);
    static_assert(offsetof(winios_settings_snapshot_t, text_scale_percent) == 36);
    static_assert(offsetof(winios_settings_snapshot_t, high_contrast) == 40);
    static_assert(offsetof(winios_settings_snapshot_t, network_enabled) == 44);

    // Syscall 586: winios_task_entry_t (120 bytes)
    static_assert(sizeof(winios_task_entry_t) == 120, "winios_task_entry_t size must be 120");
    static_assert(offsetof(winios_task_entry_t, pid) == 0);
    static_assert(offsetof(winios_task_entry_t, state) == 4);
    static_assert(offsetof(winios_task_entry_t, is_foreground) == 8);
    static_assert(offsetof(winios_task_entry_t, pad) == 12);
    static_assert(offsetof(winios_task_entry_t, primary_window) == 16);
    static_assert(offsetof(winios_task_entry_t, name) == 24);
    static_assert(offsetof(winios_task_entry_t, bundle_id) == 56);

    // Syscall 589 & 590: winios_accessibility_info_t (32 bytes)
    static_assert(sizeof(winios_accessibility_info_t) == 32, "winios_accessibility_info_t size must be 32");
    static_assert(offsetof(winios_accessibility_info_t, text_scale_percent) == 0);
    static_assert(offsetof(winios_accessibility_info_t, high_contrast) == 4);
    static_assert(offsetof(winios_accessibility_info_t, reduce_motion) == 8);
    static_assert(offsetof(winios_accessibility_info_t, focus_indicator_visible) == 12);
    static_assert(offsetof(winios_accessibility_info_t, screen_reader_active) == 16);
    static_assert(offsetof(winios_accessibility_info_t, pad) == 20);

    // Syscall 591: winios_diag_event_record_t (48 bytes) & winios_diag_query_req_t (32 bytes)
    static_assert(sizeof(winios_diag_event_record_t) == 48, "winios_diag_event_record_t size must be 48");
    static_assert(offsetof(winios_diag_event_record_t, sequence) == 0);
    static_assert(offsetof(winios_diag_event_record_t, timestamp_us) == 8);
    static_assert(offsetof(winios_diag_event_record_t, pid) == 16);
    static_assert(offsetof(winios_diag_event_record_t, level) == 20);
    static_assert(offsetof(winios_diag_event_record_t, category) == 24);
    static_assert(offsetof(winios_diag_event_record_t, error_code) == 28);
    static_assert(offsetof(winios_diag_event_record_t, tag) == 32);

    static_assert(sizeof(winios_diag_query_req_t) == 32, "winios_diag_query_req_t size must be 32");
    static_assert(offsetof(winios_diag_query_req_t, after_sequence) == 0);
    static_assert(offsetof(winios_diag_query_req_t, category_filter) == 8);
    static_assert(offsetof(winios_diag_query_req_t, min_level) == 12);
    static_assert(offsetof(winios_diag_query_req_t, max_entries) == 16);
    static_assert(offsetof(winios_diag_query_req_t, returned_entries) == 20);
    static_assert(offsetof(winios_diag_query_req_t, buffer_va) == 24);

    // Syscall 592 & 593: winios_app_info_t (128 bytes) & winios_app_list_req_t (24 bytes)
    static_assert(sizeof(winios_app_info_t) == 128, "winios_app_info_t size must be 128");
    static_assert(offsetof(winios_app_info_t, bundle_id) == 0);
    static_assert(offsetof(winios_app_info_t, display_name) == 64);
    static_assert(offsetof(winios_app_info_t, version) == 96);
    static_assert(offsetof(winios_app_info_t, min_sdk_version) == 112);
    static_assert(offsetof(winios_app_info_t, state) == 116);
    static_assert(offsetof(winios_app_info_t, storage_quota_bytes) == 120);

    static_assert(sizeof(winios_app_list_req_t) == 24, "winios_app_list_req_t size must be 24");
    static_assert(offsetof(winios_app_list_req_t, max_entries) == 0);
    static_assert(offsetof(winios_app_list_req_t, returned_entries) == 4);
    static_assert(offsetof(winios_app_list_req_t, active_only) == 8);
    static_assert(offsetof(winios_app_list_req_t, reserved) == 12);
    static_assert(offsetof(winios_app_list_req_t, buffer_va) == 16);

    std::cout << "  -> All Category C ABI v2.4 struct layouts and offsets verified bit-for-bit.\n";
}

// 2. Verify Syscall Number Definitions Match Host Kernel Dispatcher
static void test_syscall_number_mapping() {
    std::cout << "[Test 2] Syscall number mapping (581..593)...\n";

    TEST_ASSERT(SYS_WINIOS_SETTINGS_GET == winios::kernel::kSysWiniosSettingsGet);
    TEST_ASSERT(SYS_WINIOS_SETTINGS_GET == 581);

    TEST_ASSERT(SYS_WINIOS_SETTINGS_SET == winios::kernel::kSysWiniosSettingsSet);
    TEST_ASSERT(SYS_WINIOS_SETTINGS_SET == 582);

    TEST_ASSERT(SYS_WINIOS_THEME_GET == winios::kernel::kSysWiniosThemeGet);
    TEST_ASSERT(SYS_WINIOS_THEME_GET == 583);

    TEST_ASSERT(SYS_WINIOS_THEME_SET == winios::kernel::kSysWiniosThemeSet);
    TEST_ASSERT(SYS_WINIOS_THEME_SET == 584);

    TEST_ASSERT(SYS_WINIOS_CONTROL_CENTER_TOGGLE == winios::kernel::kSysWiniosControlCenterToggle);
    TEST_ASSERT(SYS_WINIOS_CONTROL_CENTER_TOGGLE == 585);

    TEST_ASSERT(SYS_WINIOS_TASK_ENUMERATE == winios::kernel::kSysWiniosTaskEnumerate);
    TEST_ASSERT(SYS_WINIOS_TASK_ENUMERATE == 586);

    TEST_ASSERT(SYS_WINIOS_TASK_SWITCH == winios::kernel::kSysWiniosTaskSwitch);
    TEST_ASSERT(SYS_WINIOS_TASK_SWITCH == 587);

    TEST_ASSERT(SYS_WINIOS_TASK_TERMINATE == winios::kernel::kSysWiniosTaskTerminate);
    TEST_ASSERT(SYS_WINIOS_TASK_TERMINATE == 588);

    TEST_ASSERT(SYS_WINIOS_ACCESSIBILITY_GET == winios::kernel::kSysWiniosAccessibilityGet);
    TEST_ASSERT(SYS_WINIOS_ACCESSIBILITY_GET == 589);

    TEST_ASSERT(SYS_WINIOS_ACCESSIBILITY_SET == winios::kernel::kSysWiniosAccessibilitySet);
    TEST_ASSERT(SYS_WINIOS_ACCESSIBILITY_SET == 590);

    TEST_ASSERT(SYS_WINIOS_DIAG_EVENT_QUERY == winios::kernel::kSysWiniosDiagEventQuery);
    TEST_ASSERT(SYS_WINIOS_DIAG_EVENT_QUERY == 591);

    TEST_ASSERT(SYS_WINIOS_APP_INFO == winios::kernel::kSysWiniosAppInfo);
    TEST_ASSERT(SYS_WINIOS_APP_INFO == 592);

    TEST_ASSERT(SYS_WINIOS_APP_LIST == winios::kernel::kSysWiniosAppList);
    TEST_ASSERT(SYS_WINIOS_APP_LIST == 593);

    TEST_ASSERT(winios::kernel::kSysWiniosMaxCategoryC == 593);
    TEST_ASSERT(winios::kernel::is_category_c_extension(581));
    TEST_ASSERT(winios::kernel::is_category_c_extension(593));
    TEST_ASSERT(!winios::kernel::is_category_c_extension(594));

    std::cout << "  -> Syscall numbering 581..593 exactly matches host kernel definitions.\n";
}

// 3. Verify Version Constants and Conformance Macros
static void test_abi_versions() {
    std::cout << "[Test 3] Category C ABI version constants...\n";

    TEST_ASSERT(WINIOS_CATEGORY_C_ABI_VERSION_2_4 == 0x00020400);
    TEST_ASSERT(WINIOS_CATEGORY_C_ABI_VERSION_2_3 == 0x00020300);
    TEST_ASSERT(WINIOS_CATEGORY_C_ABI_VERSION_2_2 == 0x00020200);
    TEST_ASSERT(WINIOS_SDK_VERSION_2_4 == 0x00020400);

    std::cout << "  -> Version constants verified.\n";
}

int main() {
    std::cout << "=== Stage 231.13: Category C ABI v2.4 Conformance Audit Tests ===\n";
    test_abi_layout_and_offsets();
    test_syscall_number_mapping();
    test_abi_versions();
    std::cout << "=== All Stage 231.13 Tests PASSED ===\n";
    return 0;
}
