#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "winios/winios.h"

// Forward declare freestanding implementations from winios_runtime.c
extern "C" {
    void*  winios_memcpy(void* dest, const void* src, size_t n);
    void*  winios_memmove(void* dest, const void* src, size_t n);
    void*  winios_memset(void* s, int c, size_t n);
    int    winios_memcmp(const void* s1, const void* s2, size_t n);
    size_t winios_strlen(const char* s);
    int    winios_strcmp(const char* s1, const char* s2);
    int    winios_strncmp(const char* s1, const char* s2, size_t n);
    char*  winios_strcpy(char* dest, const char* src);
    char*  winios_strncpy(char* dest, const char* src, size_t n);
    void   winios_itoa(int32_t value, char* str, int base);
    int32_t winios_atoi(const char* str);
    void   winios_u64_to_hex(uint64_t val, char* out_buf);
    void   winios_runtime_init(const winios_app_arg_block_t* args);
    const char* winios_getenv(const char* name);
}

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_freestanding_memory_operations() {
    std::cout << "[Test 1] Freestanding Memory Operations (memcpy, memmove, memset, memcmp)...\n";

    // 1. Memset
    uint8_t buffer[64];
    winios_memset(buffer, 0xAA, sizeof(buffer));
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        TEST_ASSERT(buffer[i] == 0xAA);
    }

    // 2. Memcpy
    uint8_t dest[64];
    winios_memset(dest, 0, sizeof(dest));
    winios_memcpy(dest, buffer, sizeof(buffer));
    TEST_ASSERT(winios_memcmp(dest, buffer, sizeof(buffer)) == 0);

    // 3. Memmove with overlapping buffers
    char text[32] = "abcdefghijklmnop";
    // Forward overlap: copy "abcdef" to offset 2 -> "ababcdef..."
    winios_memmove(text + 2, text, 6);
    TEST_ASSERT(std::memcmp(text, "ababcdef", 8) == 0);

    // Backward overlap: copy offset 2 to 0 -> "abcdef..."
    char text2[32] = "0123456789";
    winios_memmove(text2, text2 + 2, 6);
    TEST_ASSERT(std::memcmp(text2, "234567", 6) == 0);

    std::cout << "[PASS] Memory operations verified.\n";
}

void test_freestanding_string_operations() {
    std::cout << "[Test 2] Freestanding String Operations...\n";

    const char* hello = "Hello WinIOS!";
    TEST_ASSERT(winios_strlen(hello) == 13);
    TEST_ASSERT(winios_strlen("") == 0);
    TEST_ASSERT(winios_strlen(nullptr) == 0);

    TEST_ASSERT(winios_strcmp("apple", "apple") == 0);
    TEST_ASSERT(winios_strcmp("apple", "banana") < 0);
    TEST_ASSERT(winios_strcmp("zebra", "apple") > 0);

    TEST_ASSERT(winios_strncmp("win_ios", "win_linux", 4) == 0);
    TEST_ASSERT(winios_strncmp("win_ios", "win_linux", 5) != 0);

    char buf[32];
    winios_strcpy(buf, "CopyText");
    TEST_ASSERT(winios_strcmp(buf, "CopyText") == 0);

    char buf2[16];
    winios_strncpy(buf2, "1234567890ABCDEF", 5);
    buf2[5] = '\0';
    TEST_ASSERT(winios_strcmp(buf2, "12345") == 0);

    std::cout << "[PASS] String operations verified.\n";
}

void test_freestanding_formatting_and_conversions() {
    std::cout << "[Test 3] Formatting & Numeric Conversions...\n";

    char out[32];
    winios_itoa(12345, out, 10);
    TEST_ASSERT(winios_strcmp(out, "12345") == 0);

    winios_itoa(-42, out, 10);
    TEST_ASSERT(winios_strcmp(out, "-42") == 0);

    winios_itoa(0, out, 10);
    TEST_ASSERT(winios_strcmp(out, "0") == 0);

    winios_itoa(255, out, 16);
    TEST_ASSERT(winios_strcmp(out, "ff") == 0);

    TEST_ASSERT(winios_atoi("42") == 42);
    TEST_ASSERT(winios_atoi("-100") == -100);
    TEST_ASSERT(winios_atoi("  0500") == 500);

    char hex[32];
    winios_u64_to_hex(0x123456789ABCDEF0ULL, hex);
    TEST_ASSERT(winios_strcmp(hex, "0x123456789abcdef0") == 0);

    std::cout << "[PASS] Conversions verified.\n";
}

void test_freestanding_env_parsing() {
    std::cout << "[Test 4] Environment Variable Resolution...\n";

    const char* env_ptrs[] = {
        "PATH=/apps/bin",
        "APP_ID=com.winios.test",
        "LOG_LEVEL=debug",
        nullptr
    };

    winios_app_arg_block_t b{};
    b.contract_version = WINIOS_SDK_VERSION;
    b.process_id = 42;
    b.envc = 3;
    b.envp_ptrs = reinterpret_cast<uint64_t>(env_ptrs);

    winios_runtime_init(&b);

    const char* path_val = winios_getenv("PATH");
    TEST_ASSERT(path_val != nullptr);
    TEST_ASSERT(winios_strcmp(path_val, "/apps/bin") == 0);

    const char* app_id = winios_getenv("APP_ID");
    TEST_ASSERT(app_id != nullptr);
    TEST_ASSERT(winios_strcmp(app_id, "com.winios.test") == 0);

    TEST_ASSERT(winios_getenv("NONEXISTENT") == nullptr);

    std::cout << "[PASS] Environment variable parsing verified.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 221.4: Minimal Freestanding C Runtime Tests ===\n";
    test_freestanding_memory_operations();
    test_freestanding_string_operations();
    test_freestanding_formatting_and_conversions();
    test_freestanding_env_parsing();
    std::cout << "[ALL PASS] Stage 221.4 tests passed successfully.\n";
    return 0;
}
