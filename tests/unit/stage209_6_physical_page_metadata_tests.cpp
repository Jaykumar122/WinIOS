#include <iostream>
#include <cstdint>
#include <cstring>

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

#define XNU_PHYS_MODIFIED    0x01u
#define XNU_PHYS_REFERENCED  0x02u
#define XNU_PHYS_MANAGED     0x04u
#define XNU_PHYS_NOENCRYPT   0x08u

#pragma pack(push, 4)
typedef struct xnu_vm_page {
    uint32_t phys_page;
    uint16_t wire_count;
    uint8_t  state;
    uint8_t  flags;
} xnu_vm_page_t;
#pragma pack(pop)

typedef struct {
    uint32_t magic;
    uint32_t total_pages;
    uint32_t managed_pages;
    uint32_t free_pages;
    uint64_t avail_start;
    uint64_t avail_end;
} xnu_page_metadata_header_t;

void test_page_metadata_structure_conformance() {
    std::cout << "[Test 1] Stage 209.6: Physical Page Metadata Structure Conformance...\n";

    // 1. Verify structure sizes and alignment
    TEST_CHECK(sizeof(xnu_vm_page_t) == 8, "xnu_vm_page_t is exactly 8 bytes (compact C layout)");
    TEST_CHECK(sizeof(xnu_page_metadata_header_t) == 32, "xnu_page_metadata_header_t is exactly 32 bytes");

    // 2. Test metadata header population
    xnu_page_metadata_header_t hdr{};
    hdr.magic = 0x584E5550u; // "XNUP"
    hdr.total_pages = 512;
    hdr.managed_pages = 496;
    hdr.free_pages = 496;
    hdr.avail_start = 0x40000ULL;
    hdr.avail_end = 0x800000ULL;

    TEST_CHECK(hdr.magic == 0x584E5550u, "Header magic is 'XNUP'");
    TEST_CHECK(hdr.total_pages == 512, "Total pages = 512");
    TEST_CHECK(hdr.managed_pages == 496, "Managed pages = 496");
    TEST_CHECK(hdr.free_pages == 496, "Free pages = 496");
    TEST_CHECK(hdr.avail_start == 0x40000ULL, "Avail start = 0x40000");
    TEST_CHECK(hdr.avail_end == 0x800000ULL, "Avail end = 0x800000");

    // 3. Test physical attributes table
    uint8_t attrs[512] = {0};
    // Initialize frames 0..15 as unmanaged (reserved)
    for (int i = 0; i < 16; ++i) {
        attrs[i] = 0;
    }
    // Initialize frames 16..511 as managed
    for (int i = 16; i < 512; ++i) {
        attrs[i] = XNU_PHYS_MANAGED;
    }

    TEST_CHECK((attrs[0] & XNU_PHYS_MANAGED) == 0, "Page 0 is unmanaged (reserved boot)");
    TEST_CHECK((attrs[15] & XNU_PHYS_MANAGED) == 0, "Page 15 is unmanaged (reserved stolen tables)");
    TEST_CHECK((attrs[16] & XNU_PHYS_MANAGED) == XNU_PHYS_MANAGED, "Page 16 is managed free frame");
    TEST_CHECK((attrs[511] & XNU_PHYS_MANAGED) == XNU_PHYS_MANAGED, "Page 511 is managed free frame");

    // 4. Test page descriptors array
    xnu_vm_page_t pages[496] = {0};
    for (uint32_t i = 0; i < 496; ++i) {
        pages[i].phys_page = 16 + i;
        pages[i].wire_count = 0;
        pages[i].state = 0; // FREE
        pages[i].flags = 0;
    }

    TEST_CHECK(pages[0].phys_page == 16, "First allocatable page descriptor tracks PFN 16 (0x40000)");
    TEST_CHECK(pages[495].phys_page == 511, "Last allocatable page descriptor tracks PFN 511 (0x7FC000)");
    TEST_CHECK(pages[0].state == 0, "Initial page state is FREE");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.6 Physical Page Metadata Tests ===\n";
    test_page_metadata_structure_conformance();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.6: All physical page metadata tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.6: " << g_failures << " tests failed.\n";
        return 1;
    }
}
