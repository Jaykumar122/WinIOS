#include "cpu/gicv3.h"
#include "sndl/sndl_source_hooks.h"

#include <algorithm>
#include <cstring>

namespace winios {

GICv3::GICv3() noexcept {
    reset();
}

void GICv3::set_core_count(std::size_t count) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    num_cores_ = std::max(std::size_t{1}, std::min(count, kMaxCores));
}

void GICv3::reset() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // GICD reset (shared)
    gicd_ctlr_ = 0x50u; // ARE_NS | DS
    gicd_typer_ = 0x03780003u; // A3V | No1N | LPIS | IDbits=0xF | ITLinesNumber=3
    std::fill(std::begin(gicd_igroupr_), std::end(gicd_igroupr_), 0u);
    std::fill(std::begin(gicd_igrpmodr_), std::end(gicd_igrpmodr_), 0u);
    std::fill(std::begin(gicd_isenabler_), std::end(gicd_isenabler_), 0u);
    std::fill(std::begin(gicd_ispendr_), std::end(gicd_ispendr_), 0u);
    std::fill(std::begin(gicd_isactiver_), std::end(gicd_isactiver_), 0u);
    std::fill(std::begin(gicd_ipriorityr_), std::end(gicd_ipriorityr_), std::uint8_t{0});
    std::fill(std::begin(gicd_icfgr_), std::end(gicd_icfgr_), 0u);
    std::fill(std::begin(gicd_level_), std::end(gicd_level_), 0u);
    std::fill(std::begin(gicd_irouter_), std::end(gicd_irouter_), 0ULL);

    // Per-core GICR and ICC/ICH reset
    for (std::size_t i = 0; i < kMaxCores; ++i) {
        auto& r = redistributors_[i];
        r.ctlr = 0u;
        r.waker = 0u;
        r.igroupr0 = 0u;
        r.igrpmodr0 = 0u;
        r.isenabler0 = 0u;
        r.ispendr0 = 0u;
        r.isactiver0 = 0u;
        std::fill(std::begin(r.ipriorityr), std::end(r.ipriorityr), std::uint8_t{0});
        r.icfgr0 = 0xAAAAAAAAu; // SGIs edge-triggered
        r.icfgr1 = 0u;
        r.level0 = 0u;

        auto& c = icc_[i];
        c.icc_pmr_el1 = 0u;
        c.icc_bpr0_el1 = 2u;
        c.icc_bpr1_el1 = 3u;
        c.icc_ctlr_el1 = 0u;
        c.icc_igrpen0_el1 = 0u;
        c.icc_igrpen1_el1 = 0u;
        c.icc_sre_el1 = 0x7u;
        c.icc_sre_el2 = 0xFu;
        c.active_priorities.clear();

        c.ich_hcr_el2 = 0u;
        c.ich_vtr_el2 = 0x60000000ULL | (3ULL << 29) | (1ULL << 26) | (1ULL << 23) | (6ULL << 0) | (5ULL << 19);
        c.ich_vmcr_el2 = 0x0000000000480000ULL | (3ULL << 21) | (4ULL << 18);
        std::fill(std::begin(c.ich_lr_el2), std::end(c.ich_lr_el2), 0ULL);
        c.active_virt_priorities.clear();
        c.gic_enabled = false;
    }
}

void GICv3::set_ppi_level(std::uint32_t intid, bool level, std::size_t core_id) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (intid < 16u || intid >= 32u) return;
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const std::uint32_t mask = 1u << intid;
    if (level) {
        redistributors_[c].level0 |= mask;
    } else {
        redistributors_[c].level0 &= ~mask;
    }
}

void GICv3::set_spi_level(std::uint32_t intid, bool level) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (intid < 32u || intid >= kTotalIrqs) return;
    const std::size_t word = intid / 32u;
    const std::uint32_t bit  = intid % 32u;
    if (level) {
        winios::sndl::hook_interrupt_inject(0, intid, 0, 0, 0);
        gicd_level_[word] |= (1u << bit);
    } else {
        gicd_level_[word] &= ~(1u << bit);
    }
}

std::uint64_t GICv3::read_mmio(std::uint64_t pa, unsigned width) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (is_gicd_address(pa)) {
        return read_gicd(pa - kGicdBase, width);
    }
    if (is_gicr_address(pa)) {
        return read_gicr(pa - kGicrBase, width);
    }
    return 0ULL;
}

void GICv3::write_mmio(std::uint64_t pa, std::uint64_t value, unsigned width) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (is_gicd_address(pa)) {
        write_gicd(pa - kGicdBase, value, width);
        return;
    }
    if (is_gicr_address(pa)) {
        write_gicr(pa - kGicrBase, value, width);
        return;
    }
}

GICv3::DistributorState GICv3::get_distributor_state() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    DistributorState s{};
    s.gicd_ctlr = gicd_ctlr_;
    s.gicd_typer = gicd_typer_;
    std::memcpy(s.gicd_igroupr, gicd_igroupr_, sizeof(s.gicd_igroupr));
    std::memcpy(s.gicd_igrpmodr, gicd_igrpmodr_, sizeof(s.gicd_igrpmodr));
    std::memcpy(s.gicd_isenabler, gicd_isenabler_, sizeof(s.gicd_isenabler));
    std::memcpy(s.gicd_ispendr, gicd_ispendr_, sizeof(s.gicd_ispendr));
    std::memcpy(s.gicd_isactiver, gicd_isactiver_, sizeof(s.gicd_isactiver));
    std::memcpy(s.gicd_ipriorityr, gicd_ipriorityr_, sizeof(s.gicd_ipriorityr));
    std::memcpy(s.gicd_icfgr, gicd_icfgr_, sizeof(s.gicd_icfgr));
    std::memcpy(s.gicd_level, gicd_level_, sizeof(s.gicd_level));
    std::memcpy(s.gicd_irouter, gicd_irouter_, sizeof(s.gicd_irouter));
    return s;
}

void GICv3::set_distributor_state(const DistributorState& s) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    gicd_ctlr_ = s.gicd_ctlr;
    gicd_typer_ = s.gicd_typer;
    std::memcpy(gicd_igroupr_, s.gicd_igroupr, sizeof(gicd_igroupr_));
    std::memcpy(gicd_igrpmodr_, s.gicd_igrpmodr, sizeof(gicd_igrpmodr_));
    std::memcpy(gicd_isenabler_, s.gicd_isenabler, sizeof(gicd_isenabler_));
    std::memcpy(gicd_ispendr_, s.gicd_ispendr, sizeof(gicd_ispendr_));
    std::memcpy(gicd_isactiver_, s.gicd_isactiver, sizeof(gicd_isactiver_));
    std::memcpy(gicd_ipriorityr_, s.gicd_ipriorityr, sizeof(gicd_ipriorityr_));
    std::memcpy(gicd_icfgr_, s.gicd_icfgr, sizeof(gicd_icfgr_));
    std::memcpy(gicd_level_, s.gicd_level, sizeof(gicd_level_));
    std::memcpy(gicd_irouter_, s.gicd_irouter, sizeof(gicd_irouter_));
}

std::uint64_t GICv3::read_gicd(std::uint64_t offset, unsigned width) const noexcept {
    if (offset == 0x0000) {
        return gicd_ctlr_;
    }
    if (offset == 0x0004) {
        return gicd_typer_;
    }
    if (offset == 0x0008) {
        return 0x0000043Bu; // GICD_IIDR
    }
    if (offset == 0x0010) {
        return 0u; // GICD_STATUSR
    }
    // GICD_IGROUPR (0x0080..0x008C)
    if (offset >= 0x0080 && offset < 0x0090) {
        const std::size_t idx = (offset - 0x0080) / 4u;
        return gicd_igroupr_[idx];
    }
    // GICD_ISENABLER (0x0100..0x010C)
    if (offset >= 0x0100 && offset < 0x0110) {
        const std::size_t idx = (offset - 0x0100) / 4u;
        return gicd_isenabler_[idx];
    }
    // GICD_ICENABLER (0x0180..0x018C)
    if (offset >= 0x0180 && offset < 0x0190) {
        const std::size_t idx = (offset - 0x0180) / 4u;
        return gicd_isenabler_[idx];
    }
    // GICD_ISPENDR (0x0200..0x020C)
    if (offset >= 0x0200 && offset < 0x0210) {
        const std::size_t idx = (offset - 0x0200) / 4u;
        return gicd_ispendr_[idx] | gicd_level_[idx];
    }
    // GICD_ICPENDR (0x0280..0x028C)
    if (offset >= 0x0280 && offset < 0x0290) {
        const std::size_t idx = (offset - 0x0280) / 4u;
        return gicd_ispendr_[idx] | gicd_level_[idx];
    }
    // GICD_ISACTIVER (0x0300..0x030C)
    if (offset >= 0x0300 && offset < 0x0310) {
        const std::size_t idx = (offset - 0x0300) / 4u;
        return gicd_isactiver_[idx];
    }
    // GICD_ICACTIVER (0x0380..0x038C)
    if (offset >= 0x0380 && offset < 0x0390) {
        const std::size_t idx = (offset - 0x0380) / 4u;
        return gicd_isactiver_[idx];
    }
    // GICD_IPRIORITYR (0x0400..0x047F)
    if (offset >= 0x0400 && offset < 0x0480) {
        const std::size_t intid = offset - 0x0400;
        if (width == 1u) {
            return gicd_ipriorityr_[intid];
        }
        if (width == 2u && intid + 1 < 128) {
            return static_cast<std::uint64_t>(gicd_ipriorityr_[intid]) |
                   (static_cast<std::uint64_t>(gicd_ipriorityr_[intid + 1]) << 8u);
        }
        if (width == 4u && intid + 3 < 128) {
            std::uint32_t val = 0;
            for (int i = 3; i >= 0; --i) {
                val = (val << 8u) | gicd_ipriorityr_[intid + i];
            }
            return val;
        }
    }
    // GICD_ICFGR (0x0C00..0x0C20)
    if (offset >= 0x0C00 && offset < 0x0C20) {
        const std::size_t idx = (offset - 0x0C00) / 4u;
        return gicd_icfgr_[idx];
    }
    // GICD_IGRPMODR (0x0D00..0x0D0C)
    if (offset >= 0x0D00 && offset < 0x0D10) {
        const std::size_t idx = (offset - 0x0D00) / 4u;
        return gicd_igrpmodr_[idx];
    }
    // GICD_IROUTER<n> (0x6100..0x63F8 for INTID 32..127)
    if (offset >= 0x6100 && offset < 0x6100 + 96 * 8) {
        const std::size_t idx = (offset - 0x6100) / 8u;
        if (idx < 96u) {
            if (width == 8u) {
                return gicd_irouter_[idx];
            } else if (width == 4u) {
                const unsigned sub = (offset % 8u == 0) ? 0 : 1;
                return (sub == 0) ? static_cast<std::uint32_t>(gicd_irouter_[idx])
                                  : static_cast<std::uint32_t>(gicd_irouter_[idx] >> 32u);
            }
        }
    }
    // GICD Identification registers (0xFFD0..0xFFFC)
    if (offset == 0xFFD0) return 0x04u; // PIDR4
    if (offset == 0xFFE0) return 0x00u; // PIDR0
    if (offset == 0xFFE4) return 0xB4u; // PIDR1
    if (offset == 0xFFE8) return 0x30u; // PIDR2: ArchRev bits [7:4] = 0x3 (GICv3)
    if (offset == 0xFFEC) return 0x00u; // PIDR3
    if (offset == 0xFFF0) return 0x0Du; // CIDR0
    if (offset == 0xFFF4) return 0xF0u; // CIDR1
    if (offset == 0xFFF8) return 0x05u; // CIDR2
    if (offset == 0xFFFC) return 0xB1u; // CIDR3
    return 0ULL;
}

void GICv3::write_gicd(std::uint64_t offset, std::uint64_t val, unsigned width) noexcept {
    if (offset == 0x0000) {
        gicd_ctlr_ = (gicd_ctlr_ & ~0x3u) | (static_cast<std::uint32_t>(val) & 0x3u);
        for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        return;
    }
    // GICD_IGROUPR (0x0080..0x008C)
    if (offset >= 0x0080 && offset < 0x0090) {
        const std::size_t idx = (offset - 0x0080) / 4u;
        if (idx > 0) {
            gicd_igroupr_[idx] = static_cast<std::uint32_t>(val);
            for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        }
        return;
    }
    // GICD_ISENABLER (0x0100..0x010C)
    if (offset >= 0x0100 && offset < 0x0110) {
        const std::size_t idx = (offset - 0x0100) / 4u;
        if (idx > 0) {
            gicd_isenabler_[idx] |= static_cast<std::uint32_t>(val);
            for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        }
        return;
    }
    // GICD_ICENABLER (0x0180..0x018C)
    if (offset >= 0x0180 && offset < 0x0190) {
        const std::size_t idx = (offset - 0x0180) / 4u;
        if (idx > 0) {
            gicd_isenabler_[idx] &= ~static_cast<std::uint32_t>(val);
            for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        }
        return;
    }
    // GICD_ISPENDR (0x0200..0x020C)
    if (offset >= 0x0200 && offset < 0x0210) {
        const std::size_t idx = (offset - 0x0200) / 4u;
        if (idx > 0) {
            gicd_ispendr_[idx] |= static_cast<std::uint32_t>(val);
            for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        }
        return;
    }
    // GICD_ICPENDR (0x0280..0x028C)
    if (offset >= 0x0280 && offset < 0x0290) {
        const std::size_t idx = (offset - 0x0280) / 4u;
        if (idx > 0) {
            gicd_ispendr_[idx] &= ~static_cast<std::uint32_t>(val);
            for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        }
        return;
    }
    // GICD_ISACTIVER (0x0300..0x030C)
    if (offset >= 0x0300 && offset < 0x0310) {
        const std::size_t idx = (offset - 0x0300) / 4u;
        if (idx > 0) {
            gicd_isactiver_[idx] |= static_cast<std::uint32_t>(val);
            for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        }
        return;
    }
    // GICD_ICACTIVER (0x0380..0x038C)
    if (offset >= 0x0380 && offset < 0x0390) {
        const std::size_t idx = (offset - 0x0380) / 4u;
        if (idx > 0) {
            gicd_isactiver_[idx] &= ~static_cast<std::uint32_t>(val);
            for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        }
        return;
    }
    // GICD_IPRIORITYR (0x0400..0x047F)
    if (offset >= 0x0400 && offset < 0x0480) {
        const std::size_t intid = offset - 0x0400;
        if (intid >= 32u) {
            if (width == 1u) {
                gicd_ipriorityr_[intid] = static_cast<std::uint8_t>(val);
            } else if (width == 4u && intid + 3 < 128) {
                for (unsigned i = 0; i < 4u; ++i) {
                    gicd_ipriorityr_[intid + i] = static_cast<std::uint8_t>((val >> (i * 8u)) & 0xFFu);
                }
            }
        }
        return;
    }
    // GICD_ICFGR (0x0C00..0x0C20)
    if (offset >= 0x0C00 && offset < 0x0C20) {
        const std::size_t idx = (offset - 0x0C00) / 4u;
        gicd_icfgr_[idx] = static_cast<std::uint32_t>(val);
        return;
    }
    // GICD_IGRPMODR (0x0D00..0x0D0C)
    if (offset >= 0x0D00 && offset < 0x0D10) {
        const std::size_t idx = (offset - 0x0D00) / 4u;
        if (idx > 0) {
            gicd_igrpmodr_[idx] = static_cast<std::uint32_t>(val);
            for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        }
        return;
    }
    // GICD_IROUTER<n> (0x6100..0x63F8 for INTID 32..127)
    if (offset >= 0x6100 && offset < 0x6100 + 96 * 8) {
        const std::size_t idx = (offset - 0x6100) / 8u;
        if (idx < 96u) {
            if (width == 8u) {
                gicd_irouter_[idx] = val;
            } else if (width == 4u) {
                const unsigned sub = (offset % 8u == 0) ? 0 : 1;
                if (sub == 0) {
                    gicd_irouter_[idx] = (gicd_irouter_[idx] & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFFULL);
                } else {
                    gicd_irouter_[idx] = (gicd_irouter_[idx] & 0x00000000FFFFFFFFULL) | (val << 32u);
                }
            }
            for (std::size_t i = 0; i < num_cores_; ++i) icc_[i].gic_enabled = true;
        }
        return;
    }
}

std::uint64_t GICv3::read_gicr(std::uint64_t offset, unsigned width) const noexcept {
    const std::size_t core_idx = static_cast<std::size_t>(offset / kGicrCoreStride);
    if (core_idx >= num_cores_) return 0ULL;
    const std::uint64_t core_offset = offset % kGicrCoreStride;
    const auto& r = redistributors_[core_idx];

    if (core_offset < 0x10000) {
        // RD_base
        if (core_offset == 0x0000) return r.ctlr;
        if (core_offset == 0x0004) return 0x0000043Bu; // GICR_IIDR
        if (core_offset == 0x0008) {
            std::uint64_t typer = (static_cast<std::uint64_t>(core_idx) << 32u) |
                                  (static_cast<std::uint64_t>(core_idx) << 8u);
            if (core_idx == num_cores_ - 1) typer |= 0x10ULL; // Last = 1
            if (width == 4u) return static_cast<std::uint32_t>(typer);
            return typer;
        }
        if (core_offset == 0x000C) {
            const std::uint64_t typer = (static_cast<std::uint64_t>(core_idx) << 32u) |
                                        (static_cast<std::uint64_t>(core_idx) << 8u);
            return static_cast<std::uint32_t>(typer >> 32u);
        }
        if (core_offset == 0x0014) return r.waker;
        // GICR Identification registers in RD_base (0xFFD0..0xFFFC)
        if (core_offset == 0xFFD0) return 0x04u; // PIDR4
        if (core_offset == 0xFFE0) return 0x00u; // PIDR0
        if (core_offset == 0xFFE4) return 0xB4u; // PIDR1
        if (core_offset == 0xFFE8) return 0x30u; // PIDR2: ArchRev bits [7:4] = 0x3 (GICv3)
        if (core_offset == 0xFFEC) return 0x00u; // PIDR3
        if (core_offset == 0xFFF0) return 0x0Du; // CIDR0
        if (core_offset == 0xFFF4) return 0xF0u; // CIDR1
        if (core_offset == 0xFFF8) return 0x05u; // CIDR2
        if (core_offset == 0xFFFC) return 0xB1u; // CIDR3
        return 0ULL;
    }

    // SGI_base (core_offset >= 0x10000)
    const std::uint64_t sgi_offset = core_offset - 0x10000;
    if (sgi_offset == 0x0080) return r.igroupr0;
    if (sgi_offset == 0x0100 || sgi_offset == 0x0180) return r.isenabler0;
    if (sgi_offset == 0x0200 || sgi_offset == 0x0280) {
        return r.ispendr0 | r.level0;
    }
    if (sgi_offset == 0x0300 || sgi_offset == 0x0380) return r.isactiver0;
    if (sgi_offset >= 0x0400 && sgi_offset < 0x0420) {
        const std::size_t intid = sgi_offset - 0x0400;
        if (width == 1u) return r.ipriorityr[intid];
        if (width == 4u && intid + 3 < 32) {
            std::uint32_t v = 0;
            for (int i = 3; i >= 0; --i) {
                v = (v << 8u) | r.ipriorityr[intid + i];
            }
            return v;
        }
    }
    if (sgi_offset == 0x0C00) return r.icfgr0;
    if (sgi_offset == 0x0C04) return r.icfgr1;
    if (sgi_offset == 0x0D00) return r.igrpmodr0;
    return 0ULL;
}

void GICv3::write_gicr(std::uint64_t offset, std::uint64_t val, unsigned width) noexcept {
    const std::size_t core_idx = static_cast<std::size_t>(offset / kGicrCoreStride);
    if (core_idx >= num_cores_) return;
    const std::uint64_t core_offset = offset % kGicrCoreStride;
    auto& r = redistributors_[core_idx];
    icc_[core_idx].gic_enabled = true;

    if (core_offset < 0x10000) {
        // RD_base
        if (core_offset == 0x0000) { r.ctlr = static_cast<std::uint32_t>(val); return; }
        if (core_offset == 0x0014) { r.waker = static_cast<std::uint32_t>(val); return; }
        return;
    }

    // SGI_base
    const std::uint64_t sgi_offset = core_offset - 0x10000;
    if (sgi_offset == 0x0080) { r.igroupr0 = static_cast<std::uint32_t>(val); return; }
    if (sgi_offset == 0x0100) { r.isenabler0 |= static_cast<std::uint32_t>(val); return; }
    if (sgi_offset == 0x0180) { r.isenabler0 &= ~static_cast<std::uint32_t>(val); return; }
    if (sgi_offset == 0x0200) { r.ispendr0 |= static_cast<std::uint32_t>(val); return; }
    if (sgi_offset == 0x0280) { r.ispendr0 &= ~static_cast<std::uint32_t>(val); return; }
    if (sgi_offset == 0x0300) { r.isactiver0 |= static_cast<std::uint32_t>(val); return; }
    if (sgi_offset == 0x0380) { r.isactiver0 &= ~static_cast<std::uint32_t>(val); return; }
    if (sgi_offset >= 0x0400 && sgi_offset < 0x0420) {
        const std::size_t intid = sgi_offset - 0x0400;
        if (width == 1u) {
            r.ipriorityr[intid] = static_cast<std::uint8_t>(val);
        } else if (width == 4u && intid + 3 < 32) {
            for (unsigned i = 0; i < 4u; ++i) {
                r.ipriorityr[intid + i] = static_cast<std::uint8_t>((val >> (i * 8u)) & 0xFFu);
            }
        }
        return;
    }
    if (sgi_offset == 0x0C04) { r.icfgr1 = static_cast<std::uint32_t>(val); return; }
    if (sgi_offset == 0x0D00) { r.igrpmodr0 = static_cast<std::uint32_t>(val); return; }
}

std::uint8_t GICv3::get_running_priority(std::size_t core_id) const noexcept {
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];
    if (icc.active_priorities.empty()) {
        return 0xFFu;
    }
    return *std::min_element(icc.active_priorities.begin(), icc.active_priorities.end());
}

std::uint8_t GICv3::get_running_virt_priority(std::size_t core_id) const noexcept {
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];
    if (icc.active_virt_priorities.empty()) {
        return 0xFFu;
    }
    return *std::min_element(icc.active_virt_priorities.begin(), icc.active_virt_priorities.end());
}

std::uint32_t GICv3::read_icc_rpr(std::size_t core_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];
    if (icc.active_priorities.empty()) {
        return 0xFFu;
    }
    const std::uint8_t min_prio = *std::min_element(icc.active_priorities.begin(), icc.active_priorities.end());
    const std::uint8_t bpr = std::min(icc.icc_bpr0_el1, icc.icc_bpr1_el1);
    const std::uint8_t mask = static_cast<std::uint8_t>(0xFFu << (std::min(bpr, static_cast<std::uint8_t>(6)) + 1u));
    return static_cast<std::uint32_t>(min_prio & mask);
}

void GICv3::write_icc_dir(std::uint32_t intid, std::uint64_t current_el, std::size_t core_id) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    auto& icc = icc_[c];
    auto& r = redistributors_[c];

    if (current_el <= 1 && (icc.ich_hcr_el2 & 0x1u) != 0) {
        for (unsigned i = 0; i < kNumListRegs; ++i) {
            std::uint64_t& lr = icc.ich_lr_el2[i];
            const std::uint32_t v_id = static_cast<std::uint32_t>(lr & 0xFFFFFFFFu);
            const std::uint64_t state = (lr >> 62u) & 0x3u;
            if (v_id == intid && (state & 0x2u) != 0) {
                lr &= ~(0x2ULL << 62u);
                return;
            }
        }
    }

    if (intid < 32u) {
        r.isactiver0 &= ~(1u << intid);
    } else if (intid < kTotalIrqs) {
        const std::size_t w = intid / 32u;
        const std::uint32_t b = intid % 32u;
        gicd_isactiver_[w] &= ~(1u << b);
    }
}

static inline bool can_preempt_priority(std::uint8_t cand_prio, std::uint8_t run_prio, std::uint8_t bpr) noexcept {
    if (run_prio == 0xFFu) return true;
    const std::uint8_t shift = std::min(bpr, static_cast<std::uint8_t>(6)) + 1u;
    const std::uint8_t mask = static_cast<std::uint8_t>(0xFFu << shift);
    return (cand_prio & mask) < (run_prio & mask);
}

int GICv3::get_highest_pending_virt_lr(std::uint8_t& out_prio, std::size_t core_id) const noexcept {
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];

    int best_lr = -1;
    std::uint8_t best_prio = 0xFFu;

    const bool grp0_en = (icc.ich_vmcr_el2 & 0x1u) != 0;
    const bool grp1_en = (icc.ich_vmcr_el2 & 0x2u) != 0;
    if (!grp0_en && !grp1_en) return -1;

    for (unsigned i = 0; i < kNumListRegs; ++i) {
        const std::uint64_t lr = icc.ich_lr_el2[i];
        const std::uint64_t state = (lr >> 62u) & 0x3u;
        if ((state & 0x1u) == 0) continue;

        const bool is_grp1 = ((lr >> 60u) & 0x1u) != 0;
        if (is_grp1 && !grp1_en) continue;
        if (!is_grp1 && !grp0_en) continue;

        const std::uint8_t prio = static_cast<std::uint8_t>((lr >> 48u) & 0xFFu);
        if (best_lr == -1 || prio < best_prio) {
            best_lr = static_cast<int>(i);
            best_prio = prio;
        }
    }
    out_prio = best_prio;
    return best_lr;
}

GICv3::PendingSignal GICv3::evaluate(std::uint64_t current_el, std::uint64_t hcr_el2, std::size_t core_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    (void)hcr_el2;
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];
    const auto& r = redistributors_[c];

    PendingSignal signal{};

    // 1. Evaluate Virtual Interrupts (applicable when executing at EL0 or EL1 with ICH_HCR_EL2.En=1)
    if (current_el <= 1 && (icc.ich_hcr_el2 & 0x1u) != 0) {
        std::uint8_t vprio = 0xFFu;
        const int lr_idx = get_highest_pending_virt_lr(vprio, c);
        if (lr_idx >= 0) {
            const std::uint8_t vpmr = static_cast<std::uint8_t>((icc.ich_vmcr_el2 >> 24u) & 0xFFu);
            if (vprio < vpmr && vprio < get_running_virt_priority(c)) {
                const std::uint64_t lr = icc.ich_lr_el2[lr_idx];
                const bool is_grp1 = ((lr >> 60u) & 0x1u) != 0;
                signal.has_interrupt = true;
                signal.is_fiq = !is_grp1;
                signal.intid = static_cast<std::uint32_t>(lr & 0xFFFFFFFFu);
                signal.priority = vprio;
                signal.is_virtual = true;
                signal.lr_index = lr_idx;
                return signal;
            }
        }
    }

    // 2. Evaluate Physical Interrupts (SGIs, PPIs, SPIs)
    const bool grp0_dist_en = (gicd_ctlr_ & 0x1u) != 0;
    const bool grp1_dist_en = (gicd_ctlr_ & 0x2u) != 0;
    const bool grp0_cpu_en  = (icc.icc_igrpen0_el1 & 0x1u) != 0;
    const bool grp1_cpu_en  = (icc.icc_igrpen1_el1 & 0x1u) != 0;

    std::uint32_t best_intid = kSpuriousIntId;
    std::uint8_t best_prio = 0xFFu;
    bool best_is_grp0 = false;

    // Check SGIs & PPIs (0..31) in Core c's Redistributor
    const std::uint32_t pending_r = (r.ispendr0 | r.level0) & r.isenabler0 & ~r.isactiver0;
    for (std::uint32_t i = 0; i < 32u; ++i) {
        if ((pending_r & (1u << i)) != 0) {
            const bool is_grp0 = ((r.igroupr0 & (1u << i)) == 0) && ((r.igrpmodr0 & (1u << i)) == 0);
            const bool is_grp1 = !is_grp0;
            if (is_grp1 && (!grp1_dist_en || !grp1_cpu_en)) continue;
            if (is_grp0 && (!grp0_dist_en || !grp0_cpu_en)) continue;

            const std::uint8_t prio = r.ipriorityr[i];
            const std::uint8_t bpr = is_grp0 ? icc.icc_bpr0_el1 : icc.icc_bpr1_el1;
            if (prio < icc.icc_pmr_el1 && can_preempt_priority(prio, get_running_priority(c), bpr)) {
                if (best_intid == kSpuriousIntId || prio < best_prio) {
                    best_intid = i;
                    best_prio = prio;
                    best_is_grp0 = is_grp0;
                }
            }
        }
    }

    // Check SPIs (32..127) in Distributor
    for (std::size_t w = 1; w < 4; ++w) {
        const std::uint32_t pending_d = (gicd_ispendr_[w] | gicd_level_[w]) & gicd_isenabler_[w] & ~gicd_isactiver_[w];
        if (!pending_d) continue;
        for (std::uint32_t b = 0; b < 32u; ++b) {
            if ((pending_d & (1u << b)) != 0) {
                const std::uint32_t intid = static_cast<std::uint32_t>(w * 32u + b);
                const std::uint64_t router = gicd_irouter_[intid - 32u];
                const bool irm = (router & (1ULL << 31u)) != 0;
                const std::uint64_t target_aff = router & 0xFFu;
                if (!irm && target_aff != c) continue;

                const bool is_grp0 = ((gicd_igroupr_[w] & (1u << b)) == 0) && ((gicd_igrpmodr_[w] & (1u << b)) == 0);
                const bool is_grp1 = !is_grp0;
                if (is_grp1 && (!grp1_dist_en || !grp1_cpu_en)) continue;
                if (is_grp0 && (!grp0_dist_en || !grp0_cpu_en)) continue;

                const std::uint8_t prio = gicd_ipriorityr_[intid];
                const std::uint8_t bpr = is_grp0 ? icc.icc_bpr0_el1 : icc.icc_bpr1_el1;
                if (prio < icc.icc_pmr_el1 && can_preempt_priority(prio, get_running_priority(c), bpr)) {
                    if (best_intid == kSpuriousIntId || prio < best_prio) {
                        best_intid = intid;
                        best_prio = prio;
                        best_is_grp0 = is_grp0;
                    }
                }
            }
        }
    }

    if (best_intid != kSpuriousIntId) {
        signal.has_interrupt = true;
        signal.is_fiq = best_is_grp0;
        signal.intid = best_intid;
        signal.priority = best_prio;
        signal.is_virtual = false;
        signal.lr_index = -1;
    }

    return signal;
}

std::uint32_t GICv3::acknowledge_group0(std::uint64_t current_el, std::size_t core_id) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    auto& icc = icc_[c];
    auto& r = redistributors_[c];

    // Check virtual interrupt first if guest
    if (current_el <= 1 && (icc.ich_hcr_el2 & 0x1u) != 0) {
        std::uint8_t prio = 0xFFu;
        const int lr_idx = get_highest_pending_virt_lr(prio, c);
        if (lr_idx >= 0) {
            std::uint64_t& lr = icc.ich_lr_el2[lr_idx];
            const bool is_grp1 = ((lr >> 60u) & 0x1u) != 0;
            if (!is_grp1) {
                // Transition state from Pending (0b01) to Active (0b10)
                lr = (lr & ~(0x3ULL << 62u)) | (0x2ULL << 62u);
                icc.active_virt_priorities.push_back(prio);
                return static_cast<std::uint32_t>(lr & 0xFFFFFFFFu);
            }
        }
    }

    // Physical Group 0 acknowledge
    if (!(icc.icc_igrpen0_el1 & 0x1u) || !(gicd_ctlr_ & 0x1u)) {
        return kSpuriousIntId;
    }

    std::uint32_t best_intid = kSpuriousIntId;
    std::uint8_t best_prio = 0xFFu;

    const std::uint32_t pending_r = (r.ispendr0 | r.level0) & r.isenabler0 & ~r.isactiver0;
    for (std::uint32_t i = 0; i < 32u; ++i) {
        if ((pending_r & (1u << i)) != 0) {
            const bool is_grp0 = ((r.igroupr0 & (1u << i)) == 0) && ((r.igrpmodr0 & (1u << i)) == 0);
            if (!is_grp0) continue;
            const std::uint8_t prio = r.ipriorityr[i];
            if (prio < icc.icc_pmr_el1 && can_preempt_priority(prio, get_running_priority(c), icc.icc_bpr0_el1)) {
                if (best_intid == kSpuriousIntId || prio < best_prio) {
                    best_intid = i;
                    best_prio = prio;
                }
            }
        }
    }

    for (std::size_t w = 1; w < 4; ++w) {
        const std::uint32_t pending_d = (gicd_ispendr_[w] | gicd_level_[w]) & gicd_isenabler_[w] & ~gicd_isactiver_[w];
        for (std::uint32_t b = 0; b < 32u; ++b) {
            if ((pending_d & (1u << b)) != 0) {
                const std::uint32_t intid = static_cast<std::uint32_t>(w * 32u + b);
                const std::uint64_t router = gicd_irouter_[intid - 32u];
                const bool irm = (router & (1ULL << 31u)) != 0;
                const std::uint64_t target_aff = router & 0xFFu;
                if (!irm && target_aff != c) continue;

                const bool is_grp0 = ((gicd_igroupr_[w] & (1u << b)) == 0) && ((gicd_igrpmodr_[w] & (1u << b)) == 0);
                if (!is_grp0) continue;
                const std::uint8_t prio = gicd_ipriorityr_[intid];
                if (prio < icc.icc_pmr_el1 && can_preempt_priority(prio, get_running_priority(c), icc.icc_bpr0_el1)) {
                    if (best_intid == kSpuriousIntId || prio < best_prio) {
                        best_intid = intid;
                        best_prio = prio;
                    }
                }
            }
        }
    }

    if (best_intid != kSpuriousIntId) {
        if (best_intid < 32u) {
            r.isactiver0 |= (1u << best_intid);
            r.ispendr0 &= ~(1u << best_intid);
        } else {
            const std::size_t w = best_intid / 32u;
            const std::uint32_t b = best_intid % 32u;
            gicd_isactiver_[w] |= (1u << b);
            gicd_ispendr_[w] &= ~(1u << b);
        }
        icc.active_priorities.push_back(best_prio);
        return best_intid;
    }

    return kSpuriousIntId;
}

std::uint32_t GICv3::acknowledge_group1(std::uint64_t current_el, std::size_t core_id) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    auto& icc = icc_[c];
    auto& r = redistributors_[c];

    // Check virtual interrupt first if guest
    if (current_el <= 1 && (icc.ich_hcr_el2 & 0x1u) != 0) {
        std::uint8_t prio = 0xFFu;
        const int lr_idx = get_highest_pending_virt_lr(prio, c);
        if (lr_idx >= 0) {
            std::uint64_t& lr = icc.ich_lr_el2[lr_idx];
            const bool is_grp1 = ((lr >> 60u) & 0x1u) != 0;
            if (is_grp1) {
                // Transition state from Pending (0b01) to Active (0b10)
                lr = (lr & ~(0x3ULL << 62u)) | (0x2ULL << 62u);
                icc.active_virt_priorities.push_back(prio);
                return static_cast<std::uint32_t>(lr & 0xFFFFFFFFu);
            }
        }
    }

    // Physical Group 1 acknowledge
    if (!(icc.icc_igrpen1_el1 & 0x1u) || !(gicd_ctlr_ & 0x2u)) {
        return kSpuriousIntId;
    }

    std::uint32_t best_intid = kSpuriousIntId;
    std::uint8_t best_prio = 0xFFu;

    const std::uint32_t pending_r = (r.ispendr0 | r.level0) & r.isenabler0 & ~r.isactiver0;
    for (std::uint32_t i = 0; i < 32u; ++i) {
        if ((pending_r & (1u << i)) != 0) {
            const bool is_grp0 = ((r.igroupr0 & (1u << i)) == 0) && ((r.igrpmodr0 & (1u << i)) == 0);
            const bool is_grp1 = !is_grp0;
            if (!is_grp1) continue;
            const std::uint8_t prio = r.ipriorityr[i];
            if (prio < icc.icc_pmr_el1 && can_preempt_priority(prio, get_running_priority(c), icc.icc_bpr1_el1)) {
                if (best_intid == kSpuriousIntId || prio < best_prio) {
                    best_intid = i;
                    best_prio = prio;
                }
            }
        }
    }

    for (std::size_t w = 1; w < 4; ++w) {
        const std::uint32_t pending_d = (gicd_ispendr_[w] | gicd_level_[w]) & gicd_isenabler_[w] & ~gicd_isactiver_[w];
        for (std::uint32_t b = 0; b < 32u; ++b) {
            if ((pending_d & (1u << b)) != 0) {
                const std::uint32_t intid = static_cast<std::uint32_t>(w * 32u + b);
                const std::uint64_t router = gicd_irouter_[intid - 32u];
                const bool irm = (router & (1ULL << 31u)) != 0;
                const std::uint64_t target_aff = router & 0xFFu;
                if (!irm && target_aff != c) continue;

                const bool is_grp0 = ((gicd_igroupr_[w] & (1u << b)) == 0) && ((gicd_igrpmodr_[w] & (1u << b)) == 0);
                const bool is_grp1 = !is_grp0;
                if (!is_grp1) continue;
                const std::uint8_t prio = gicd_ipriorityr_[intid];
                if (prio < icc.icc_pmr_el1 && can_preempt_priority(prio, get_running_priority(c), icc.icc_bpr1_el1)) {
                    if (best_intid == kSpuriousIntId || prio < best_prio) {
                        best_intid = intid;
                        best_prio = prio;
                    }
                }
            }
        }
    }

    if (best_intid != kSpuriousIntId) {
        if (best_intid < 32u) {
            r.isactiver0 |= (1u << best_intid);
            r.ispendr0 &= ~(1u << best_intid);
        } else {
            const std::size_t w = best_intid / 32u;
            const std::uint32_t b = best_intid % 32u;
            gicd_isactiver_[w] |= (1u << b);
            gicd_ispendr_[w] &= ~(1u << b);
        }
        icc.active_priorities.push_back(best_prio);
        return best_intid;
    }

    return kSpuriousIntId;
}

void GICv3::end_of_interrupt_group0(std::uint32_t intid, std::uint64_t current_el, std::size_t core_id) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    auto& icc = icc_[c];
    auto& r = redistributors_[c];

    // Virtual EOI if guest
    if (current_el <= 1 && (icc.ich_hcr_el2 & 0x1u) != 0) {
        for (unsigned i = 0; i < kNumListRegs; ++i) {
            std::uint64_t& lr = icc.ich_lr_el2[i];
            const std::uint32_t v_id = static_cast<std::uint32_t>(lr & 0xFFFFFFFFu);
            const std::uint64_t state = (lr >> 62u) & 0x3u;
            if (v_id == intid && (state & 0x2u) != 0) { // Active
                lr &= ~(0x2ULL << 62u); // clear active bit
                if (!icc.active_virt_priorities.empty()) {
                    auto it = std::min_element(icc.active_virt_priorities.begin(), icc.active_virt_priorities.end());
                    icc.active_virt_priorities.erase(it);
                }
                return;
            }
        }
    }

    // Physical Group 0 Priority Drop & Deactivation
    if (!icc.active_priorities.empty()) {
        auto it = std::min_element(icc.active_priorities.begin(), icc.active_priorities.end());
        icc.active_priorities.erase(it);
    }
    const bool eoi_mode = (icc.icc_ctlr_el1 & 0x2u) != 0;
    if (!eoi_mode) {
        if (intid < 32u) {
            r.isactiver0 &= ~(1u << intid);
        } else if (intid < 128u) {
            const std::size_t w = intid / 32u;
            const std::uint32_t b = intid % 32u;
            gicd_isactiver_[w] &= ~(1u << b);
        }
    }
}

void GICv3::end_of_interrupt_group1(std::uint32_t intid, std::uint64_t current_el, std::size_t core_id) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    auto& icc = icc_[c];
    auto& r = redistributors_[c];

    // Virtual EOI if guest
    if (current_el <= 1 && (icc.ich_hcr_el2 & 0x1u) != 0) {
        for (unsigned i = 0; i < kNumListRegs; ++i) {
            std::uint64_t& lr = icc.ich_lr_el2[i];
            const std::uint32_t v_id = static_cast<std::uint32_t>(lr & 0xFFFFFFFFu);
            const std::uint64_t state = (lr >> 62u) & 0x3u;
            if (v_id == intid && (state & 0x2u) != 0) { // Active
                lr &= ~(0x2ULL << 62u); // clear active bit
                if (!icc.active_virt_priorities.empty()) {
                    auto it = std::min_element(icc.active_virt_priorities.begin(), icc.active_virt_priorities.end());
                    icc.active_virt_priorities.erase(it);
                }
                return;
            }
        }
    }

    // Physical Group 1 Priority Drop & Deactivation
    if (!icc.active_priorities.empty()) {
        auto it = std::min_element(icc.active_priorities.begin(), icc.active_priorities.end());
        icc.active_priorities.erase(it);
    }
    const bool eoi_mode = (icc.icc_ctlr_el1 & 0x2u) != 0;
    if (!eoi_mode) {
        if (intid < 32u) {
            r.isactiver0 &= ~(1u << intid);
        } else if (intid < 128u) {
            const std::size_t w = intid / 32u;
            const std::uint32_t b = intid % 32u;
            gicd_isactiver_[w] &= ~(1u << b);
        }
    }
}

std::uint32_t GICv3::read_hppir0(std::uint64_t current_el, std::size_t core_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];

    if (current_el <= 1 && (icc.ich_hcr_el2 & 0x1u) != 0) {
        std::uint8_t prio = 0xFFu;
        const int lr_idx = get_highest_pending_virt_lr(prio, c);
        if (lr_idx >= 0) {
            const std::uint64_t lr = icc.ich_lr_el2[lr_idx];
            if (((lr >> 60u) & 0x1u) == 0) {
                return static_cast<std::uint32_t>(lr & 0xFFFFFFFFu);
            }
        }
    }
    const auto sig = evaluate(current_el, 0, c);
    if (sig.has_interrupt && sig.is_fiq) {
        return sig.intid;
    }
    return kSpuriousIntId;
}

std::uint32_t GICv3::read_hppir1(std::uint64_t current_el, std::size_t core_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];

    if (current_el <= 1 && (icc.ich_hcr_el2 & 0x1u) != 0) {
        std::uint8_t prio = 0xFFu;
        const int lr_idx = get_highest_pending_virt_lr(prio, c);
        if (lr_idx >= 0) {
            const std::uint64_t lr = icc.ich_lr_el2[lr_idx];
            if (((lr >> 60u) & 0x1u) != 0) {
                return static_cast<std::uint32_t>(lr & 0xFFFFFFFFu);
            }
        }
    }
    const auto sig = evaluate(current_el, 0, c);
    if (sig.has_interrupt && !sig.is_fiq) {
        return sig.intid;
    }
    return kSpuriousIntId;
}

void GICv3::send_sgi(std::size_t sender_core, std::uint64_t sgi1r) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::uint32_t intid = static_cast<std::uint32_t>((sgi1r >> 24u) & 0xFu);
    const std::uint32_t irm = static_cast<std::uint32_t>((sgi1r >> 40u) & 0x1u);
    const std::uint32_t target_list = static_cast<std::uint32_t>(sgi1r & 0xFFFFu);

    for (std::size_t i = 0; i < num_cores_; ++i) {
        bool target = false;
        if (irm == 1u) {
            target = (i != sender_core);
        } else {
            target = ((target_list & (1u << i)) != 0);
        }
        if (target && i < num_cores_) {
            redistributors_[i].ispendr0 |= (1u << intid);
            icc_[i].gic_enabled = true;
        }
    }
}

std::uint32_t GICv3::read_ich_misr(std::size_t core_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];
    std::uint32_t misr = 0;
    if (read_ich_eisr(c) != 0) {
        misr |= (1u << 0); // EOI maintenance interrupt
    }
    const std::uint32_t elrsr = read_ich_elrsr(c);
    unsigned empty_lrs = 0;
    for (unsigned i = 0; i < kNumListRegs; ++i) {
        if ((elrsr & (1u << i)) != 0) empty_lrs++;
    }
    const unsigned valid_lrs = kNumListRegs - empty_lrs;
    if ((icc.ich_hcr_el2 & (1u << 1)) != 0 && valid_lrs <= 1) { // UIE (Underflow)
        misr |= (1u << 1);
    }
    if ((icc.ich_hcr_el2 & (1u << 2)) != 0) { // LRENPIE
        bool any_pending = false;
        for (unsigned i = 0; i < kNumListRegs; ++i) {
            const std::uint64_t state = (icc.ich_lr_el2[i] >> 62u) & 0x3u;
            if ((state & 0x1u) != 0) { any_pending = true; break; }
        }
        if (!any_pending) misr |= (1u << 2);
    }
    if ((icc.ich_hcr_el2 & (1u << 3)) != 0) { // NPIE
        std::uint8_t vprio = 0xFFu;
        if (get_highest_pending_virt_lr(vprio, c) < 0) {
            misr |= (1u << 3);
        }
    }
    return misr;
}

std::uint32_t GICv3::read_ich_eisr(std::size_t core_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];
    std::uint32_t eisr = 0;
    for (unsigned i = 0; i < kNumListRegs; ++i) {
        const std::uint64_t lr = icc.ich_lr_el2[i];
        const std::uint64_t state = (lr >> 62u) & 0x3u;
        if (state == 0 && (lr & (1ULL << 41u)) != 0 && (lr & (1ULL << 61u)) == 0) {
            eisr |= (1u << i);
        }
    }
    return eisr;
}

std::uint32_t GICv3::read_ich_elrsr(std::size_t core_id) const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::size_t c = (core_id < num_cores_) ? core_id : 0;
    const auto& icc = icc_[c];
    std::uint32_t elrsr = 0;
    for (unsigned i = 0; i < kNumListRegs; ++i) {
        const std::uint64_t state = (icc.ich_lr_el2[i] >> 62u) & 0x3u;
        if (state == 0) {
            elrsr |= (1u << i);
        }
    }
    return elrsr;
}

} // namespace winios
