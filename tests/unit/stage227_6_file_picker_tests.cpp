#include "documents/document_registry.h"
#include "documents/file_picker_service.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

int main() {
    using namespace winios;
    using namespace winios::documents;

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    vfs->mkdir("/Documents");
    const std::string dummy = "Hello File Picker\n";
    vfs->write_file("/Documents/file1.txt", dummy.data(), dummy.size());
    vfs->write_file("/Documents/file2.md", dummy.data(), dummy.size());

    auto reg = std::make_shared<DocumentRegistry>();
    FilePickerService picker(reg, vfs);

    // 1. Candidate enumeration with filter
    auto txt_files = picker.list_candidates("/Documents", ".txt");
    TEST_ASSERT(txt_files.size() == 1);
    TEST_ASSERT(txt_files[0] == "/Documents/file1.txt");

    auto all_files = picker.list_candidates("/Documents", "*");
    TEST_ASSERT(all_files.size() == 2);

    // 2. Request open flow
    std::uint32_t req_id = 0;
    TEST_ASSERT(picker.request_open(101, 0x100000001ULL, ".txt", req_id));
    TEST_ASSERT(req_id == 1);

    // Still active -> polling returns nullopt
    TEST_ASSERT(!picker.poll_result(req_id, 101).has_value());

    // 3. Accept selection with invalid extension fails
    TEST_ASSERT(!picker.accept_selection(req_id, "/Documents/file2.md"));

    // Accept valid selection
    TEST_ASSERT(picker.accept_selection(req_id, "/Documents/file1.txt", "file1.txt"));

    auto res = picker.poll_result(req_id, 101);
    TEST_ASSERT(res.has_value());
    TEST_ASSERT(res->state == FilePickerState::Accepted);
    TEST_ASSERT(res->selected_path == "/Documents/file1.txt");
    TEST_ASSERT(res->display_name == "file1.txt");

    // Second poll returns nullopt (consumed)
    TEST_ASSERT(!picker.poll_result(req_id, 101).has_value());

    // 4. Request save and cancellation flow
    std::uint32_t save_req_id = 0;
    TEST_ASSERT(picker.request_save(101, 0x100000001ULL, "newdoc.txt", ".txt", save_req_id));
    TEST_ASSERT(save_req_id == 2);

    // Unauthorized cancel by different PID fails
    TEST_ASSERT(!picker.cancel_request(save_req_id, 999));

    // Caller cancel succeeds
    TEST_ASSERT(picker.cancel_request(save_req_id, 101));
    auto cancel_res = picker.poll_result(save_req_id, 101);
    TEST_ASSERT(cancel_res.has_value());
    TEST_ASSERT(cancel_res->state == FilePickerState::Cancelled);

    std::cout << "stage227_6_file_picker_tests: ALL PASSED" << std::endl;
    return 0;
}
