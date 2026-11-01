#ifndef VEYRA_FOUNDATION_PREFERENCES_HPP
#define VEYRA_FOUNDATION_PREFERENCES_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veyra {

enum class ColorScheme : std::uint8_t {
    light,
    dark,
};

inline constexpr float kMinimumTextScale = 0.5F;
inline constexpr float kMaximumTextScale = 4.0F;

struct SystemPreferences {
    float text_scale{1.0F};
    bool reduced_motion{};
    bool high_contrast{};
    ColorScheme color_scheme{ColorScheme::light};
    std::uint64_t version{};

    constexpr bool operator==(const SystemPreferences&) const noexcept = default;
};

inline float sanitize_text_scale(float value) noexcept {
    if (!std::isfinite(value) || value <= 0.0F) {
        return 1.0F;
    }

    return std::clamp(value, kMinimumTextScale, kMaximumTextScale);
}

inline SystemPreferences sanitize_system_preferences(SystemPreferences preferences) noexcept {
    preferences.text_scale = sanitize_text_scale(preferences.text_scale);
    if (preferences.color_scheme != ColorScheme::light &&
        preferences.color_scheme != ColorScheme::dark) {
        preferences.color_scheme = ColorScheme::light;
    }

    return preferences;
}

using SystemPreferencesSource = SystemPreferences (*)(void* user) noexcept;

inline SystemPreferences
read_system_preferences(SystemPreferencesSource source, void* user) noexcept {
    if (source == nullptr) {
        return {};
    }

    return sanitize_system_preferences(source(user));
}

}

namespace veyra::foundation {
using ::veyra::ColorScheme;
using ::veyra::kMaximumTextScale;
using ::veyra::kMinimumTextScale;
using ::veyra::read_system_preferences;
using ::veyra::sanitize_system_preferences;
using ::veyra::sanitize_text_scale;
using ::veyra::SystemPreferences;
using ::veyra::SystemPreferencesSource;
}

#endif
