#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "ui/display_window.h"

using namespace winios::ui;

#pragma pack(push, 1)
struct BmpFileHeader {
    std::uint16_t type;
    std::uint32_t size;
    std::uint16_t reserved1;
    std::uint16_t reserved2;
    std::uint32_t off_bits;
};

struct BmpInfoHeader {
    std::uint32_t size;
    std::int32_t  width;
    std::int32_t  height;
    std::uint16_t planes;
    std::uint16_t bit_count;
    std::uint32_t compression;
    std::uint32_t size_image;
    std::int32_t  x_pels_per_meter;
    std::int32_t  y_pels_per_meter;
    std::uint32_t clr_used;
    std::uint32_t clr_important;
};
#pragma pack(pop)

void test_bmp_capture_and_dimensions() {
    DisplayWindowConfig cfg;
    cfg.headless = true;
    cfg.native_width = 800;
    cfg.native_height = 1200;
    cfg.scale = 0.5f; // window_width = 400, window_height = 600

    DisplayWindow win(cfg);
    assert(win.create());

    // 1. Invalid path capture returns false
    const std::string invalid_path = "Z:\\non_existent_dir_215\\test_empty.bmp";
    assert(!win.save_screenshot_bmp(invalid_path));
    assert(!win.capture_client_area_bmp(invalid_path));

    // 2. Present distinctive frame (BGRA pattern)
    std::vector<std::uint8_t> frame(800 * 1200 * 4, 0);
    for (std::size_t i = 0; i < frame.size(); i += 4) {
        frame[i + 0] = 0xAA; // B
        frame[i + 1] = 0xBB; // G
        frame[i + 2] = 0xCC; // R
        frame[i + 3] = 0xFF; // A
    }
    assert(win.present_frame(frame.data(), 800, 1200));

    // 3. Capture framebuffer BMP
    const std::string fb_path = "test_fb.bmp";
    assert(win.save_screenshot_bmp(fb_path));

    // Read and verify framebuffer BMP header
    std::ifstream in_fb(fb_path, std::ios::binary);
    assert(in_fb.is_open());
    BmpFileHeader fh_fb{};
    BmpInfoHeader ih_fb{};
    in_fb.read(reinterpret_cast<char*>(&fh_fb), sizeof(fh_fb));
    in_fb.read(reinterpret_cast<char*>(&ih_fb), sizeof(ih_fb));
    in_fb.close();

    assert(fh_fb.type == 0x4D42); // "BM"
    assert(ih_fb.width == 800);
    assert(ih_fb.height == 1200);
    assert(ih_fb.bit_count == 32);

    // 4. Capture client-area BMP
    const std::string ca_path = "test_ca.bmp";
    assert(win.capture_client_area_bmp(ca_path));

    // Read and verify client-area BMP header
    std::ifstream in_ca(ca_path, std::ios::binary);
    assert(in_ca.is_open());
    BmpFileHeader fh_ca{};
    BmpInfoHeader ih_ca{};
    in_ca.read(reinterpret_cast<char*>(&fh_ca), sizeof(fh_ca));
    in_ca.read(reinterpret_cast<char*>(&ih_ca), sizeof(ih_ca));
    in_ca.close();

    assert(fh_ca.type == 0x4D42); // "BM"
    assert(ih_ca.width == static_cast<std::int32_t>(win.window_width()));
    assert(ih_ca.height == static_cast<std::int32_t>(win.window_height()));
    assert(ih_ca.bit_count == 32);

    // Clean up temporary files
    std::remove(fb_path.c_str());
    std::remove(ca_path.c_str());

    std::cout << "[PASS] test_bmp_capture_and_dimensions passed\n";
}

int main() {
    std::cout << "Starting Stage 215.5 Real Windows Desktop Capture Tests...\n";
    test_bmp_capture_and_dimensions();
    std::cout << "All Stage 215.5 Desktop Capture tests PASSED!\n";
    return 0;
}
