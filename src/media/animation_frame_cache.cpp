#include "veyra/media/animation_frame_cache.hpp"

namespace veyra {
namespace {

std::uint64_t mix_hash(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

}

std::size_t AnimationFrameHash::operator()(const AnimationFrameKey& key) const noexcept {
    const auto combined = key.animation_id ^ (static_cast<std::uint64_t>(key.frame_index) << 32U);
    return static_cast<std::size_t>(mix_hash(combined));
}

bool AnimationFrameKeyValidator::operator()(const AnimationFrameKey& key) const noexcept {
    return key.animation_id != 0;
}

}
