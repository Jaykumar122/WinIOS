#include "graphics/window_manager.h"

#include <algorithm>

namespace winios::graphics {

std::uint32_t WindowManager::create_window(
    std::uint32_t owner_pid,
    const std::string& title,
    const Rect& frame,
    std::shared_ptr<DisplaySurface> surface) {

    std::lock_guard<std::mutex> guard(lock_);
    std::uint32_t wid = next_window_id_++;

    auto win = std::make_shared<Window>();
    win->id = wid;
    win->owner_pid = owner_pid;
    win->title = title;
    win->frame = frame;
    win->surface = std::move(surface);
    win->state = WindowState::Normal;

    // Find highest z-order to place new window on top by default
    std::int32_t max_z = 0;
    for (const auto& [_, w] : windows_) {
        if (w->z_order > max_z) {
            max_z = w->z_order;
        }
    }
    win->z_order = max_z + 1;

    std::uint32_t gen = 1;
    auto gen_it = generations_.find(wid);
    if (gen_it != generations_.end()) {
        gen = gen_it->second;
    } else {
        generations_[wid] = 1;
    }
    win->generation = gen;

    // Update surface position to match window frame if surface exists
    if (win->surface) {
        win->surface->set_position(static_cast<std::int32_t>(frame.x), static_cast<std::int32_t>(frame.y));
        win->surface->set_z_order(win->z_order);
    }

    windows_[wid] = win;

    // New window gets focus by default
    if (focused_window_id_ != 0) {
        auto prev_it = windows_.find(focused_window_id_);
        if (prev_it != windows_.end()) {
            prev_it->second->is_focused = false;
        }
    }
    win->is_focused = true;
    focused_window_id_ = wid;

    return wid;
}

std::uint64_t WindowManager::create_window_v2(
    std::uint32_t owner_pid,
    const std::string& title,
    const Rect& frame,
    std::shared_ptr<DisplaySurface> surface) {

    std::uint32_t wid = create_window(owner_pid, title, frame, std::move(surface));
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(wid);
    if (it != windows_.end()) {
        return it->second->handle();
    }
    return 0;
}

bool WindowManager::destroy_window(std::uint32_t caller_pid, std::uint32_t window_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end()) {
        return false;
    }

    if (!it->second->can_access(caller_pid)) {
        return false;
    }

    it->second->state = WindowState::Closed;
    std::uint32_t next_gen = it->second->generation + 1;
    if (next_gen == 0) next_gen = 1;
    generations_[window_id] = next_gen;
    windows_.erase(it);

    if (focused_window_id_ == window_id) {
        focused_window_id_ = 0;
        reassign_focus_locked();
    }

    return true;
}

bool WindowManager::destroy_window_by_handle(std::uint32_t caller_pid, std::uint64_t handle) {
    const std::uint32_t wid = static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL);
    const std::uint32_t gen = static_cast<std::uint32_t>((handle >> 32) & 0xFFFFFFFFULL);

    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(wid);
    if (it == windows_.end() || it->second->generation != gen) {
        return false;
    }

    if (!it->second->can_access(caller_pid)) {
        return false;
    }

    it->second->state = WindowState::Closed;
    std::uint32_t next_gen = gen + 1;
    if (next_gen == 0) next_gen = 1;
    generations_[wid] = next_gen;
    windows_.erase(it);

    if (focused_window_id_ == wid) {
        focused_window_id_ = 0;
        reassign_focus_locked();
    }

    return true;
}

std::shared_ptr<Window> WindowManager::get_window_by_handle(std::uint64_t handle) const {
    const std::uint32_t wid = static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL);
    const std::uint32_t gen = static_cast<std::uint32_t>((handle >> 32) & 0xFFFFFFFFULL);

    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(wid);
    if (it != windows_.end() && it->second->generation == gen) {
        return it->second;
    }
    return nullptr;
}

bool WindowManager::is_handle_valid(std::uint64_t handle) const noexcept {
    const std::uint32_t wid = static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL);
    const std::uint32_t gen = static_cast<std::uint32_t>((handle >> 32) & 0xFFFFFFFFULL);

    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(wid);
    return it != windows_.end() && it->second->generation == gen && it->second->state != WindowState::Closed;
}

bool WindowManager::minimize_window(std::uint32_t caller_pid, std::uint64_t handle) {
    auto win = get_window_by_handle(handle);
    if (!win || !win->can_access(caller_pid)) return false;
    return set_window_state(caller_pid, win->id, WindowState::Minimized);
}

bool WindowManager::maximize_window(std::uint32_t caller_pid, std::uint64_t handle, const Rect& screen_bounds) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint32_t wid = static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL);
    const std::uint32_t gen = static_cast<std::uint32_t>((handle >> 32) & 0xFFFFFFFFULL);

    auto it = windows_.find(wid);
    if (it == windows_.end() || it->second->generation != gen || !it->second->can_access(caller_pid)) {
        return false;
    }

    auto& win = it->second;
    if (win->state != WindowState::Maximized) {
        win->restore_frame = win->frame;
        win->state = WindowState::Maximized;
        // Apply maximized bounds
        Rect old_frame = win->frame;
        win->frame = screen_bounds;
        if (win->surface) {
            auto cf = win->client_frame();
            if (cf.width != win->surface->width() || cf.height != win->surface->height()) {
                win->surface->resize(cf.width, cf.height);
            }
            win->surface->set_position(static_cast<std::int32_t>(cf.x), static_cast<std::int32_t>(cf.y));
        }
        win->is_dirty = true;
        win->dirty_rect = old_frame.union_with(win->frame);
    }
    return true;
}

bool WindowManager::restore_window(std::uint32_t caller_pid, std::uint64_t handle) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint32_t wid = static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL);
    const std::uint32_t gen = static_cast<std::uint32_t>((handle >> 32) & 0xFFFFFFFFULL);

    auto it = windows_.find(wid);
    if (it == windows_.end() || it->second->generation != gen || !it->second->can_access(caller_pid)) {
        return false;
    }

    auto& win = it->second;
    if (win->state == WindowState::Maximized) {
        win->state = WindowState::Normal;
        Rect old_frame = win->frame;
        win->frame = (win->restore_frame.width > 0 && win->restore_frame.height > 0) ? win->restore_frame : Rect{50, 50, 400, 300};
        if (win->surface) {
            auto cf = win->client_frame();
            if (cf.width != win->surface->width() || cf.height != win->surface->height()) {
                win->surface->resize(cf.width, cf.height);
            }
            win->surface->set_position(static_cast<std::int32_t>(cf.x), static_cast<std::int32_t>(cf.y));
        }
        win->is_dirty = true;
        win->dirty_rect = old_frame.union_with(win->frame);
    } else if (win->state == WindowState::Minimized || win->state == WindowState::Hidden) {
        win->state = WindowState::Normal;
        if (win->surface) {
            win->surface->set_visible(true);
        }
    }
    return true;
}

bool WindowManager::post_event(std::uint64_t handle, const DesktopEvent& ev) {
    auto win = get_window_by_handle(handle);
    if (!win) return false;
    return win->event_queue.push(ev);
}

bool WindowManager::post_event_to_focused(const DesktopEvent& ev) {
    std::lock_guard<std::mutex> guard(lock_);
    if (focused_window_id_ == 0) return false;
    auto it = windows_.find(focused_window_id_);
    if (it == windows_.end()) return false;
    DesktopEvent routed = ev;
    routed.window_handle = it->second->handle();
    return it->second->event_queue.push(routed);
}

bool WindowManager::poll_event(std::uint32_t caller_pid, std::uint64_t handle, DesktopEvent& out_ev) {
    auto win = get_window_by_handle(handle);
    if (!win || !win->can_access(caller_pid)) return false;
    return win->event_queue.pop(out_ev);
}

bool WindowManager::invalidate_window_rect(std::uint32_t caller_pid, std::uint64_t handle, const Rect& damage) {
    auto win = get_window_by_handle(handle);
    if (!win || !win->can_access(caller_pid)) return false;
    mark_window_dirty(win->id, damage);
    return true;
}

bool WindowManager::set_focus(std::uint32_t window_id) {
    std::lock_guard<std::mutex> guard(lock_);
    if (window_id == 0) {
        if (focused_window_id_ != 0) {
            auto it = windows_.find(focused_window_id_);
            if (it != windows_.end()) {
                it->second->is_focused = false;
            }
            focused_window_id_ = 0;
        }
        return true;
    }

    auto it = windows_.find(window_id);
    if (it == windows_.end() || it->second->state != WindowState::Normal) {
        return false;
    }

    if (focused_window_id_ != 0) {
        auto prev_it = windows_.find(focused_window_id_);
        if (prev_it != windows_.end()) {
            prev_it->second->is_focused = false;
        }
    }

    it->second->is_focused = true;
    focused_window_id_ = window_id;
    return true;
}

std::uint32_t WindowManager::focused_window_id() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return focused_window_id_;
}

std::uint32_t WindowManager::focused_pid() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    if (focused_window_id_ == 0) return 0;
    auto it = windows_.find(focused_window_id_);
    if (it != windows_.end()) {
        return it->second->owner_pid;
    }
    return 0;
}

bool WindowManager::set_window_state(std::uint32_t caller_pid, std::uint32_t window_id, WindowState state) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end() || !it->second->can_access(caller_pid)) {
        return false;
    }

    it->second->state = state;
    if (it->second->surface) {
        it->second->surface->set_visible(state == WindowState::Normal);
    }

    if (state != WindowState::Normal && focused_window_id_ == window_id) {
        it->second->is_focused = false;
        focused_window_id_ = 0;
        reassign_focus_locked();
    }

    return true;
}

bool WindowManager::set_window_frame(std::uint32_t caller_pid, std::uint32_t window_id, const Rect& frame) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end() || !it->second->can_access(caller_pid)) {
        return false;
    }

    Rect old_frame = it->second->frame;
    it->second->frame = frame;
    if (it->second->surface) {
        auto cf = it->second->client_frame();
        if (cf.width != it->second->surface->width() || cf.height != it->second->surface->height()) {
            it->second->surface->resize(cf.width, cf.height);
        }
        it->second->surface->set_position(static_cast<std::int32_t>(cf.x), static_cast<std::int32_t>(cf.y));
    }
    it->second->is_dirty = true;
    Rect damage = old_frame.union_with(it->second->frame);
    if (it->second->dirty_rect.width == 0 || it->second->dirty_rect.height == 0) {
        it->second->dirty_rect = damage;
    } else {
        it->second->dirty_rect = it->second->dirty_rect.union_with(damage);
    }
    return true;
}

bool WindowManager::set_window_z_order(std::uint32_t caller_pid, std::uint32_t window_id, std::int32_t z) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end() || !it->second->can_access(caller_pid)) {
        return false;
    }

    it->second->z_order = z;
    if (it->second->surface) {
        it->second->surface->set_z_order(z);
    }
    return true;
}

std::shared_ptr<Window> WindowManager::get_window(std::uint32_t window_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it != windows_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Window>> WindowManager::get_windows_for_process(std::uint32_t pid) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::shared_ptr<Window>> res;
    for (const auto& [_, w] : windows_) {
        if (w->owner_pid == pid) {
            res.push_back(w);
        }
    }
    return res;
}

std::vector<std::shared_ptr<Window>> WindowManager::get_visible_windows_z_ordered() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::shared_ptr<Window>> res;
    for (const auto& [_, w] : windows_) {
        if (w->is_visible()) {
            res.push_back(w);
        }
    }

    std::sort(res.begin(), res.end(), [](const auto& a, const auto& b) {
        if (a->always_on_top != b->always_on_top) {
            return !a->always_on_top && b->always_on_top;
        }
        return a->z_order < b->z_order;
    });

    return res;
}

bool WindowManager::raise_to_top(std::uint32_t window_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end() || it->second->state != WindowState::Normal) {
        return false;
    }

    std::int32_t max_z = 0;
    for (const auto& [_, w] : windows_) {
        if (w->id != window_id) {
            if (it->second->always_on_top || !w->always_on_top) {
                if (w->z_order > max_z) max_z = w->z_order;
            }
        }
    }
    it->second->z_order = max_z + 1;
    if (it->second->surface) {
        it->second->surface->set_z_order(it->second->z_order);
    }

    if (focused_window_id_ != window_id) {
        if (focused_window_id_ != 0) {
            auto prev_it = windows_.find(focused_window_id_);
            if (prev_it != windows_.end()) {
                prev_it->second->is_focused = false;
            }
        }
        it->second->is_focused = true;
        focused_window_id_ = window_id;
    }

    it->second->is_dirty = true;
    return true;
}

bool WindowManager::lower_to_bottom(std::uint32_t window_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end() || it->second->state != WindowState::Normal) {
        return false;
    }

    std::int32_t min_z = 0;
    for (const auto& [_, w] : windows_) {
        if (w->id != window_id) {
            if (w->z_order < min_z) min_z = w->z_order;
        }
    }
    it->second->z_order = min_z - 1;
    if (it->second->surface) {
        it->second->surface->set_z_order(it->second->z_order);
    }
    it->second->is_dirty = true;
    return true;
}

bool WindowManager::set_always_on_top(std::uint32_t caller_pid, std::uint32_t window_id, bool on_top) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end() || !it->second->can_access(caller_pid)) {
        return false;
    }
    it->second->always_on_top = on_top;
    it->second->is_dirty = true;
    return true;
}

bool WindowManager::move_window(std::uint32_t caller_pid, std::uint32_t window_id, std::int32_t dx, std::int32_t dy) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end() || !it->second->can_access(caller_pid)) {
        return false;
    }

    Rect old_frame = it->second->frame;
    std::int64_t new_x = static_cast<std::int64_t>(it->second->frame.x) + dx;
    std::int64_t new_y = static_cast<std::int64_t>(it->second->frame.y) + dy;
    it->second->frame.x = static_cast<std::uint32_t>(std::max<std::int64_t>(0, new_x));
    it->second->frame.y = static_cast<std::uint32_t>(std::max<std::int64_t>(0, new_y));

    if (it->second->surface) {
        auto cf = it->second->client_frame();
        it->second->surface->set_position(static_cast<std::int32_t>(cf.x), static_cast<std::int32_t>(cf.y));
    }
    it->second->is_dirty = true;
    Rect move_damage = old_frame.union_with(it->second->frame);
    if (it->second->dirty_rect.width == 0 || it->second->dirty_rect.height == 0) {
        it->second->dirty_rect = move_damage;
    } else {
        it->second->dirty_rect = it->second->dirty_rect.union_with(move_damage);
    }
    return true;
}

bool WindowManager::resize_window(std::uint32_t caller_pid, std::uint32_t window_id, std::uint32_t new_width, std::uint32_t new_height) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end() || !it->second->can_access(caller_pid)) {
        return false;
    }

    Rect old_frame = it->second->frame;
    std::uint32_t w = std::clamp(new_width, it->second->min_width, it->second->max_width);
    std::uint32_t h = std::clamp(new_height, it->second->min_height, it->second->max_height);
    it->second->frame.width = w;
    it->second->frame.height = h;

    if (it->second->surface) {
        auto cf = it->second->client_frame();
        it->second->surface->resize(cf.width, cf.height);
        it->second->surface->set_position(static_cast<std::int32_t>(cf.x), static_cast<std::int32_t>(cf.y));
    }
    it->second->is_dirty = true;
    Rect resize_damage = old_frame.union_with(it->second->frame);
    if (it->second->dirty_rect.width == 0 || it->second->dirty_rect.height == 0) {
        it->second->dirty_rect = resize_damage;
    } else {
        it->second->dirty_rect = it->second->dirty_rect.union_with(resize_damage);
    }
    return true;
}

NonClientZone WindowManager::hit_test_non_client(std::uint32_t window_id, std::uint32_t px, std::uint32_t py) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it == windows_.end() || !it->second->is_visible() || !it->second->frame.contains(px, py)) {
        return NonClientZone::None;
    }

    const auto& win = it->second;
    const auto& f = win->frame;

    // Check buttons inside title bar
    if (f.width >= Window::kButtonWidth + Window::kButtonMargin) {
        std::uint32_t close_x = f.x + f.width - Window::kButtonMargin - Window::kButtonWidth;
        std::uint32_t btn_y = f.y + (Window::kTitleBarHeight - Window::kButtonHeight) / 2;
        Rect close_rect{close_x, btn_y, Window::kButtonWidth, Window::kButtonHeight};
        if (close_rect.contains(px, py)) return NonClientZone::CloseButton;

        if (f.width >= (Window::kButtonWidth * 2 + Window::kButtonMargin * 2)) {
            std::uint32_t max_x = close_x - Window::kButtonMargin - Window::kButtonWidth;
            Rect max_rect{max_x, btn_y, Window::kButtonWidth, Window::kButtonHeight};
            if (max_rect.contains(px, py)) return NonClientZone::MaximizeButton;

            if (f.width >= (Window::kButtonWidth * 3 + Window::kButtonMargin * 3)) {
                std::uint32_t min_x = max_x - Window::kButtonMargin - Window::kButtonWidth;
                Rect min_rect{min_x, btn_y, Window::kButtonWidth, Window::kButtonHeight};
                if (min_rect.contains(px, py)) return NonClientZone::MinimizeButton;
            }
        }
    }

    // Check title bar
    Rect title_rect{f.x, f.y, f.width, Window::kTitleBarHeight};
    if (title_rect.contains(px, py)) {
        return NonClientZone::TitleBar;
    }

    // Check borders (within 4px of edges)
    constexpr std::uint32_t kResizeBorder = 4;
    if (px < f.x + kResizeBorder || px >= f.x + f.width - kResizeBorder ||
        py >= f.y + f.height - kResizeBorder) {
        return NonClientZone::Border;
    }

    return NonClientZone::Client;
}

void WindowManager::mark_window_dirty(std::uint32_t window_id, const Rect& damage) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it != windows_.end()) {
        it->second->is_dirty = true;
        if (it->second->dirty_rect.width == 0 || it->second->dirty_rect.height == 0) {
            it->second->dirty_rect = damage;
        } else {
            it->second->dirty_rect = it->second->dirty_rect.union_with(damage);
        }
    }
}

void WindowManager::clear_window_dirty(std::uint32_t window_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it != windows_.end()) {
        it->second->is_dirty = false;
        it->second->dirty_rect = Rect{0, 0, 0, 0};
    }
}

bool WindowManager::is_window_dirty(std::uint32_t window_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it != windows_.end()) {
        return it->second->is_dirty;
    }
    return false;
}

Rect WindowManager::get_window_dirty_rect(std::uint32_t window_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = windows_.find(window_id);
    if (it != windows_.end()) {
        return it->second->dirty_rect;
    }
    return Rect{0, 0, 0, 0};
}

std::uint32_t WindowManager::window_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return static_cast<std::uint32_t>(windows_.size());
}

std::shared_ptr<Window> WindowManager::hit_test(std::uint32_t px, std::uint32_t py) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::shared_ptr<Window>> visible;
    for (const auto& [_, w] : windows_) {
        if (w->is_visible() && w->frame.contains(px, py)) {
            visible.push_back(w);
        }
    }

    if (visible.empty()) {
        return nullptr;
    }

    // Frontmost window has highest z-order
    auto max_it = std::max_element(visible.begin(), visible.end(), [](const auto& a, const auto& b) {
        return a->z_order < b->z_order;
    });

    return *max_it;
}

void WindowManager::cleanup_process_windows(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::uint32_t> to_remove;
    for (const auto& [wid, w] : windows_) {
        if (w->owner_pid == pid) {
            to_remove.push_back(wid);
        }
    }

    for (std::uint32_t wid : to_remove) {
        auto it = windows_.find(wid);
        if (it != windows_.end()) {
            it->second->state = WindowState::Closed;
            std::uint32_t next_gen = it->second->generation + 1;
            if (next_gen == 0) next_gen = 1;
            generations_[wid] = next_gen;
            windows_.erase(it);
        }
    }

    if (focused_window_id_ != 0 && windows_.find(focused_window_id_) == windows_.end()) {
        focused_window_id_ = 0;
        reassign_focus_locked();
    }
}

void WindowManager::reassign_focus_locked() {
    std::int32_t max_z = std::numeric_limits<std::int32_t>::min();
    std::shared_ptr<Window> top_win = nullptr;

    for (const auto& [_, w] : windows_) {
        if (w->is_visible() && w->z_order > max_z) {
            max_z = w->z_order;
            top_win = w;
        }
    }

    if (top_win) {
        top_win->is_focused = true;
        focused_window_id_ = top_win->id;
    }
}

} // namespace winios::graphics
