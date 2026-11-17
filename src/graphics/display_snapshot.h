#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"

namespace winios::graphics {

inline constexpr std::uint32_t kDisplaySnapshotMagic = 0x57445350u; // 'WDSP'
inline constexpr std::uint32_t kDisplaySnapshotVersion = 1u;

class DisplaySnapshot {
public:
    static void serialize(
        const VirtualDisplay& display,
        const SurfaceResourceManager& surface_mgr,
        const WindowManager& window_mgr,
        std::vector<std::uint8_t>& dst);

    static bool deserialize(
        const std::uint8_t*& src,
        std::size_t& remaining,
        VirtualDisplay& display,
        SurfaceResourceManager& surface_mgr,
        WindowManager& window_mgr);
};

} // namespace winios::graphics
