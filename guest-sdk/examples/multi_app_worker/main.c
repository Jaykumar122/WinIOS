#include "winios/winios.h"

// Multi-Application Background Worker Workload
// Performs computation, data processing, and terminates with code 88.
int main(int argc, const char* const* argv) {
    (void)argc;
    (void)argv;

    uint32_t accum = 0;
    for (uint32_t i = 1; i <= 20; ++i) {
        accum += (i * i);
    }

    // Sum of squares from 1 to 20 = 2870
    if (accum != 2870) {
        return 1;
    }

    // Expected success status: 88
    return 88;
}
