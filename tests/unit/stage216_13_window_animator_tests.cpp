#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "graphics/window_animator.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios::graphics;

void test_open_animation_deterministic_progress() {
    WindowAnimator animator;

    Rect start_f{100, 100, 200, 150};
    Rect target_f{50, 50, 400, 300};

    // 200ms duration
    animator.start_animation(1, WindowAnimationType::Open, start_f, target_f, 200);
    TEST_ASSERT(animator.is_animating(1));
    TEST_ASSERT(animator.has_active_animations());
    TEST_ASSERT(animator.get_current_alpha(1) == 0.0f);

    // Step 100ms (50% elapsed)
    animator.step(100);
    TEST_ASSERT(animator.is_animating(1));
    float alpha_50 = animator.get_current_alpha(1);
    TEST_ASSERT(alpha_50 > 0.0f && alpha_50 < 1.0f);

    Rect frame_50 = animator.get_current_frame(1, start_f);
    TEST_ASSERT(frame_50.width > start_f.width && frame_50.width < target_f.width);

    // Step another 100ms -> 100% complete
    animator.step(100);
    TEST_ASSERT(!animator.is_animating(1));
    TEST_ASSERT(!animator.has_active_animations());
    TEST_ASSERT(animator.get_current_alpha(1) == 1.0f);

    std::cout << "[PASS] test_open_animation_deterministic_progress passed\n";
}

void test_minimize_and_restore_animation() {
    WindowAnimator animator;
    Rect normal_f{100, 100, 400, 300};
    Rect icon_f{20, 500, 50, 50};

    // Start minimize animation
    animator.start_animation(2, WindowAnimationType::Minimize, normal_f, icon_f, 100);
    TEST_ASSERT(animator.is_animating(2));

    animator.step(100);
    TEST_ASSERT(!animator.is_animating(2));

    // Start restore animation
    animator.start_animation(2, WindowAnimationType::Restore, icon_f, normal_f, 100);
    TEST_ASSERT(animator.is_animating(2));

    animator.step(100);
    TEST_ASSERT(!animator.is_animating(2));
    TEST_ASSERT(animator.get_current_alpha(2) == 1.0f);

    std::cout << "[PASS] test_minimize_and_restore_animation passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.13 Window Animator Tests ===" << std::endl;
    test_open_animation_deterministic_progress();
    test_minimize_and_restore_animation();
    std::cout << "=== All Stage 216.13 Window Animator Tests PASSED ===" << std::endl;
    return 0;
}
