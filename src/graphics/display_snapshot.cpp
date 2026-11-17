#include "graphics/display_snapshot.h"

#include <cstring>

namespace winios::graphics {

void DisplaySnapshot::serialize(
    const VirtualDisplay& display,
    const SurfaceResourceManager& surface_mgr,
    const WindowManager& window_mgr,
    std::vector<std::uint8_t>& dst) {

    auto append_val = [&](const auto& val) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
        dst.insert(dst.end(), p, p + sizeof(val));
    };

    // 1. Header
    append_val(kDisplaySnapshotMagic);
    append_val(kDisplaySnapshotVersion);

    // 2. Display Configuration
    const auto& cfg = display.config();
    append_val(cfg.width);
    append_val(cfg.height);
    append_val(static_cast<std::uint8_t>(cfg.orientation));
    append_val(static_cast<std::uint8_t>(cfg.pixel_format));
    append_val(cfg.bytes_per_pixel);
    append_val(cfg.row_stride);
    append_val(cfg.refresh_interval_us);
    append_val(cfg.display_scale);
    append_val(cfg.guest_physical_address);

    // 3. Display Framebuffer Data
    std::uint32_t fb_size = static_cast<std::uint32_t>(display.framebuffer_size());
    append_val(fb_size);
    if (fb_size > 0 && display.raw_data()) {
        dst.insert(dst.end(), display.raw_data(), display.raw_data() + fb_size);
    }

    // 4. Windows State
    auto visible_windows = window_mgr.get_visible_windows_z_ordered();
    std::uint32_t win_count = static_cast<std::uint32_t>(visible_windows.size());
    append_val(win_count);

    for (const auto& w : visible_windows) {
        append_val(w->id);
        append_val(w->owner_pid);
        append_val(w->frame.x);
        append_val(w->frame.y);
        append_val(w->frame.width);
        append_val(w->frame.height);
        append_val(w->z_order);
        append_val(static_cast<std::uint8_t>(w->state));
        append_val(static_cast<std::uint8_t>(w->is_focused ? 1 : 0));

        std::uint32_t title_len = static_cast<std::uint32_t>(w->title.size());
        append_val(title_len);
        if (title_len > 0) {
            dst.insert(dst.end(), w->title.begin(), w->title.end());
        }
    }

    // 5. Surfaces State
    auto surfaces = surface_mgr.get_all_visible_surfaces();
    std::uint32_t surf_count = static_cast<std::uint32_t>(surfaces.size());
    append_val(surf_count);

    for (const auto& s : surfaces) {
        append_val(s->id());
        append_val(s->owner_pid());
        append_val(s->width());
        append_val(s->height());
        append_val(static_cast<std::uint8_t>(s->format()));
        append_val(s->pos_x());
        append_val(s->pos_y());
        append_val(s->z_order());
        append_val(s->opacity());
        append_val(static_cast<std::uint8_t>(s->is_visible() ? 1 : 0));

        std::uint32_t s_bytes = static_cast<std::uint32_t>(s->size_bytes());
        append_val(s_bytes);
        if (s_bytes > 0 && s->raw_data()) {
            dst.insert(dst.end(), s->raw_data(), s->raw_data() + s_bytes);
        }
    }
}

bool DisplaySnapshot::deserialize(
    const std::uint8_t*& src,
    std::size_t& remaining,
    VirtualDisplay& display,
    SurfaceResourceManager& surface_mgr,
    WindowManager& window_mgr) {

    auto read_val = [&](auto& val) -> bool {
        if (remaining < sizeof(val)) return false;
        std::memcpy(&val, src, sizeof(val));
        src += sizeof(val);
        remaining -= sizeof(val);
        return true;
    };

    // 1. Header
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!read_val(magic) || magic != kDisplaySnapshotMagic) return false;
    if (!read_val(version) || version != kDisplaySnapshotVersion) return false;

    // 2. Display Configuration
    DisplayConfiguration cfg{};
    std::uint8_t orient = 0;
    std::uint8_t fmt = 0;
    if (!read_val(cfg.width)) return false;
    if (!read_val(cfg.height)) return false;
    if (!read_val(orient)) return false;
    cfg.orientation = static_cast<DisplayOrientation>(orient);
    if (!read_val(fmt)) return false;
    cfg.pixel_format = static_cast<PixelFormat>(fmt);
    if (!read_val(cfg.bytes_per_pixel)) return false;
    if (!read_val(cfg.row_stride)) return false;
    if (!read_val(cfg.refresh_interval_us)) return false;
    if (!read_val(cfg.display_scale)) return false;
    if (!read_val(cfg.guest_physical_address)) return false;

    if (!display.reconfigure(cfg)) return false;

    // 3. Display Framebuffer Data
    std::uint32_t fb_size = 0;
    if (!read_val(fb_size)) return false;
    if (remaining < fb_size) return false;
    if (fb_size > 0 && display.raw_data()) {
        std::memcpy(display.raw_data(), src, std::min<std::size_t>(fb_size, display.framebuffer_size()));
    }
    src += fb_size;
    remaining -= fb_size;

    // 4. Windows State
    std::uint32_t win_count = 0;
    if (!read_val(win_count)) return false;

    for (std::uint32_t i = 0; i < win_count; ++i) {
        std::uint32_t wid = 0;
        std::uint32_t owner_pid = 0;
        Rect frame{};
        std::int32_t z = 0;
        std::uint8_t state = 0;
        std::uint8_t focused = 0;

        if (!read_val(wid)) return false;
        if (!read_val(owner_pid)) return false;
        if (!read_val(frame.x)) return false;
        if (!read_val(frame.y)) return false;
        if (!read_val(frame.width)) return false;
        if (!read_val(frame.height)) return false;
        if (!read_val(z)) return false;
        if (!read_val(state)) return false;
        if (!read_val(focused)) return false;

        std::uint32_t t_len = 0;
        if (!read_val(t_len)) return false;
        if (remaining < t_len) return false;
        std::string title(reinterpret_cast<const char*>(src), t_len);
        src += t_len;
        remaining -= t_len;

        std::shared_ptr<DisplaySurface> surf = nullptr;
        surface_mgr.get_surface(owner_pid, wid, surf);
        std::uint32_t created_wid = window_mgr.create_window(owner_pid, title, frame, surf);
        window_mgr.set_window_z_order(owner_pid, created_wid, z);
        window_mgr.set_window_state(owner_pid, created_wid, static_cast<WindowState>(state));
        if (focused) {
            window_mgr.set_focus(created_wid);
        }
    }

    // 5. Surfaces State
    std::uint32_t surf_count = 0;
    if (!read_val(surf_count)) return false;

    for (std::uint32_t i = 0; i < surf_count; ++i) {
        std::uint32_t sid = 0;
        std::uint32_t owner_pid = 0;
        std::uint32_t w = 0, h = 0;
        std::uint8_t s_fmt = 0;
        std::int32_t px = 0, py = 0, sz = 0;
        float opacity = 1.0f;
        std::uint8_t vis = 1;

        if (!read_val(sid)) return false;
        if (!read_val(owner_pid)) return false;
        if (!read_val(w)) return false;
        if (!read_val(h)) return false;
        if (!read_val(s_fmt)) return false;
        if (!read_val(px)) return false;
        if (!read_val(py)) return false;
        if (!read_val(sz)) return false;
        if (!read_val(opacity)) return false;
        if (!read_val(vis)) return false;

        std::uint32_t s_bytes = 0;
        if (!read_val(s_bytes)) return false;
        if (remaining < s_bytes) return false;

        std::shared_ptr<DisplaySurface> surf;
        if (surface_mgr.create_surface(owner_pid, w, h, static_cast<PixelFormat>(s_fmt), surf) == SurfaceResultCode::Success && surf) {
            surf->set_position(px, py);
            surf->set_z_order(sz);
            surf->set_opacity(opacity);
            surf->set_visible(vis != 0);
            if (s_bytes <= surf->size_bytes() && surf->raw_data()) {
                std::memcpy(surf->raw_data(), src, s_bytes);
            }
        }
        src += s_bytes;
        remaining -= s_bytes;
    }

    return true;
}

} // namespace winios::graphics
