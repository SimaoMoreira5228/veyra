#include "veyra/c/veyra.h"

#include "veyra/core/tree/node_tree.hpp"
#include "veyra/style/property/property_set.hpp"
#include "veyra/style/theme/theme.hpp"
#include "veyra/style/theme/theme_file.hpp"

#include <new>
#include <span>
#include <string_view>

namespace {

constexpr auto kThemeMagic = std::uint32_t{0x56595448U};
constexpr auto kPropertyMagic = std::uint32_t{0x56595052U};
constexpr auto kTreeMagic = std::uint32_t{0x56594E54U};

struct ThemeHandle {
    std::uint32_t magic{};
    veyra::Theme theme{};
};

struct PropertyHandle {
    std::uint32_t magic{};
    veyra::PropertySet properties{};
};

struct TreeHandle {
    std::uint32_t magic{};
    veyra::NodeTree tree{};
};

bool valid_theme(const vy_theme* theme) noexcept {
    return theme != nullptr && reinterpret_cast<const ThemeHandle*>(theme)->magic == kThemeMagic;
}

bool valid_property_set(const vy_property_set* set) noexcept {
    return set != nullptr && reinterpret_cast<const PropertyHandle*>(set)->magic == kPropertyMagic;
}

bool valid_tree(const vy_node_tree* tree) noexcept {
    return tree != nullptr && reinterpret_cast<const TreeHandle*>(tree)->magic == kTreeMagic;
}

int32_t theme_status(veyra::ThemeUpdateResult result) noexcept {
    switch (result) {
    case veyra::ThemeUpdateResult::unchanged:
    case veyra::ThemeUpdateResult::changed:
        return VY_OK;
    case veyra::ThemeUpdateResult::invalid_token:
        return VY_INVALID_TOKEN;
    case veyra::ThemeUpdateResult::invalid_value:
    case veyra::ThemeUpdateResult::alias_cycle:
        return VY_INVALID_VALUE;
    }

    return VY_INVALID_VALUE;
}

int32_t property_status(veyra::PropertyUpdateResult result) noexcept {
    switch (result) {
    case veyra::PropertyUpdateResult::unchanged:
    case veyra::PropertyUpdateResult::changed:
        return VY_OK;
    case veyra::PropertyUpdateResult::invalid_property:
    case veyra::PropertyUpdateResult::invalid_layer:
        return VY_INVALID_ARGUMENT;
    case veyra::PropertyUpdateResult::invalid_value:
        return VY_INVALID_VALUE;
    }

    return VY_INVALID_VALUE;
}

int32_t file_status(veyra::ThemeFileResult result) noexcept {
    switch (result) {
    case veyra::ThemeFileResult::ok:
        return VY_OK;
    case veyra::ThemeFileResult::invalid_schema:
        return VY_FILE_INVALID_SCHEMA;
    case veyra::ThemeFileResult::unknown_token:
        return VY_FILE_UNKNOWN_TOKEN;
    case veyra::ThemeFileResult::bad_value:
        return VY_FILE_BAD_VALUE;
    case veyra::ThemeFileResult::duplicate_token:
        return VY_FILE_DUPLICATE_TOKEN;
    case veyra::ThemeFileResult::alias_cycle:
        return VY_FILE_ALIAS_CYCLE;
    case veyra::ThemeFileResult::truncated_output:
        return VY_INVALID_OPERATION;
    }

    return VY_INVALID_OPERATION;
}

void write_error(
    vy_theme_error* error,
    int32_t status,
    std::uint32_t line,
    std::uint32_t column
) noexcept {
    if (error == nullptr) {
        return;
    }

    error->size = sizeof(vy_theme_error);
    error->version = VY_STRUCT_VERSION;
    error->status = status;
    error->line = line;
    error->column = column;
}

bool valid_theme_layer(std::uint8_t layer) noexcept {
    return layer <= VY_LAYER_OVERRIDE;
}

bool valid_style_layer(std::uint8_t layer) noexcept {
    return layer <= VY_STYLE_ACCESSIBILITY;
}

bool valid_property(std::uint8_t property) noexcept {
    return property < VY_PROPERTY_COUNT;
}

bool valid_color_token(std::uint8_t token) noexcept {
    return token < VY_COLOR_COUNT;
}

bool valid_space_token(std::uint8_t token) noexcept {
    return token < VY_SPACE_COUNT;
}

bool valid_radius_token(std::uint8_t token) noexcept {
    return token < VY_RADIUS_COUNT;
}

bool valid_motion_token(std::uint8_t token) noexcept {
    return token < VY_MOTION_COUNT;
}

vy_node_id to_c(veyra::NodeId id) noexcept {
    return {.index = id.index, .generation = id.generation};
}

veyra::NodeId from_c(vy_node_id id) noexcept {
    return {.index = id.index, .generation = id.generation};
}

static_assert(
    static_cast<std::uint8_t>(veyra::StyleProperty::foreground) == VY_PROPERTY_FOREGROUND
);
static_assert(
    static_cast<std::uint8_t>(veyra::StyleProperty::transition_duration) ==
    VY_PROPERTY_TRANSITION_DURATION
);
static_assert(
    static_cast<std::uint8_t>(veyra::StyleLayer::platform_default) == VY_STYLE_PLATFORM_DEFAULT
);
static_assert(
    static_cast<std::uint8_t>(veyra::StyleLayer::accessibility) == VY_STYLE_ACCESSIBILITY
);

}

extern "C" {

const char* vy_status_name(int32_t status) {
    switch (status) {
    case VY_OK:
        return "ok";
    case VY_INVALID_ARGUMENT:
        return "invalid argument";
    case VY_INVALID_HANDLE:
        return "invalid handle";
    case VY_INVALID_TOKEN:
        return "invalid token";
    case VY_INVALID_VALUE:
        return "invalid value";
    case VY_OUT_OF_MEMORY:
        return "out of memory";
    case VY_FILE_INVALID_SCHEMA:
        return "invalid schema";
    case VY_FILE_UNKNOWN_TOKEN:
        return "unknown token";
    case VY_FILE_BAD_VALUE:
        return "bad value";
    case VY_FILE_DUPLICATE_TOKEN:
        return "duplicate token";
    case VY_FILE_ALIAS_CYCLE:
        return "alias cycle";
    case VY_INVALID_OPERATION:
        return "invalid operation";
    default:
        return "unknown";
    }
}

uint32_t vy_abi_version(void) {
    return VY_ABI_VERSION;
}

int32_t vy_theme_create(int32_t variant, vy_theme** out_theme) {
    if (out_theme == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    auto theme = veyra::Theme{};
    switch (variant) {
    case VY_THEME_LIGHT:
        theme = veyra::Theme::light();
        break;
    case VY_THEME_DARK:
        theme = veyra::Theme::dark();
        break;
    case VY_THEME_HIGH_CONTRAST:
        theme = veyra::Theme::high_contrast();
        break;
    default:
        return VY_INVALID_ARGUMENT;
    }

    auto* handle = new (std::nothrow) ThemeHandle{.magic = kThemeMagic, .theme = theme};
    if (handle == nullptr) {
        return VY_OUT_OF_MEMORY;
    }

    *out_theme = reinterpret_cast<vy_theme*>(handle);
    return VY_OK;
}

void vy_theme_destroy(vy_theme* theme) {
    if (theme == nullptr) {
        return;
    }

    auto* handle = reinterpret_cast<ThemeHandle*>(theme);
    if (handle->magic != kThemeMagic) {
        return;
    }

    handle->magic = 0;
    delete handle;
}

int32_t vy_theme_set_color(
    vy_theme* theme,
    uint8_t token,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha,
    uint8_t layer
) {
    if (!valid_theme(theme) || !valid_color_token(token) || !valid_theme_layer(layer)) {
        return VY_INVALID_ARGUMENT;
    }

    auto* handle = reinterpret_cast<ThemeHandle*>(theme);
    return theme_status(handle->theme.set(
        static_cast<veyra::ColorToken>(token),
        veyra::Srgba8{.red = red, .green = green, .blue = blue, .alpha = alpha},
        static_cast<veyra::ThemeLayer>(layer)
    ));
}

int32_t vy_theme_color(const vy_theme* theme, uint8_t token, uint8_t out_rgba[4]) {
    if (!valid_theme(theme) || !valid_color_token(token) || out_rgba == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    const auto color = reinterpret_cast<const ThemeHandle*>(theme)->theme.color(
        static_cast<veyra::ColorToken>(token)
    );
    out_rgba[0] = color.red;
    out_rgba[1] = color.green;
    out_rgba[2] = color.blue;
    out_rgba[3] = color.alpha;
    return VY_OK;
}

int32_t vy_theme_set_space(vy_theme* theme, uint8_t token, float value, uint8_t layer) {
    if (!valid_theme(theme) || !valid_space_token(token) || !valid_theme_layer(layer)) {
        return VY_INVALID_ARGUMENT;
    }

    return theme_status(
        reinterpret_cast<ThemeHandle*>(theme)->theme.set(
            static_cast<veyra::SpaceToken>(token),
            veyra::LogicalPx{value},
            static_cast<veyra::ThemeLayer>(layer)
        )
    );
}

int32_t vy_theme_space(const vy_theme* theme, uint8_t token, float* out_value) {
    if (!valid_theme(theme) || !valid_space_token(token) || out_value == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    *out_value = reinterpret_cast<const ThemeHandle*>(theme)
                     ->theme.space(static_cast<veyra::SpaceToken>(token))
                     .value;
    return VY_OK;
}

int32_t vy_theme_set_radius(vy_theme* theme, uint8_t token, float value, uint8_t layer) {
    if (!valid_theme(theme) || !valid_radius_token(token) || !valid_theme_layer(layer)) {
        return VY_INVALID_ARGUMENT;
    }

    return theme_status(
        reinterpret_cast<ThemeHandle*>(theme)->theme.set(
            static_cast<veyra::RadiusToken>(token),
            veyra::LogicalPx{value},
            static_cast<veyra::ThemeLayer>(layer)
        )
    );
}

int32_t vy_theme_radius(const vy_theme* theme, uint8_t token, float* out_value) {
    if (!valid_theme(theme) || !valid_radius_token(token) || out_value == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    *out_value = reinterpret_cast<const ThemeHandle*>(theme)
                     ->theme.radius(static_cast<veyra::RadiusToken>(token))
                     .value;
    return VY_OK;
}

int32_t vy_theme_set_motion(vy_theme* theme, uint8_t token, uint32_t value, uint8_t layer) {
    if (!valid_theme(theme) || !valid_motion_token(token) || !valid_theme_layer(layer)) {
        return VY_INVALID_ARGUMENT;
    }

    return theme_status(
        reinterpret_cast<ThemeHandle*>(theme)->theme.set(
            static_cast<veyra::MotionToken>(token),
            veyra::Milliseconds{value},
            static_cast<veyra::ThemeLayer>(layer)
        )
    );
}

int32_t vy_theme_motion(const vy_theme* theme, uint8_t token, uint32_t* out_value) {
    if (!valid_theme(theme) || !valid_motion_token(token) || out_value == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    *out_value = reinterpret_cast<const ThemeHandle*>(theme)
                     ->theme.motion(static_cast<veyra::MotionToken>(token))
                     .value;
    return VY_OK;
}

int32_t vy_theme_apply(vy_theme* theme, vy_utf8 text, vy_theme_error* error) {
    if (!valid_theme(theme) || (text.data == nullptr && text.size != 0)) {
        write_error(error, VY_INVALID_ARGUMENT, 0, 0);
        return VY_INVALID_ARGUMENT;
    }

    auto overlay = veyra::Theme{};
    auto parse_error = veyra::ThemeFileError{};
    const auto text_view = std::string_view(text.data == nullptr ? "" : text.data, text.size);
    const auto parsed = veyra::parse_theme_file(
        std::span<const char>(text_view.data(), text_view.size()),
        overlay,
        parse_error
    );
    if (parsed != veyra::ThemeFileResult::ok) {
        const auto status = file_status(parsed);
        write_error(error, status, parse_error.line, parse_error.column);
        return status;
    }

    reinterpret_cast<ThemeHandle*>(theme)->theme.apply(overlay);
    write_error(error, VY_OK, 0, 0);
    return VY_OK;
}

int32_t vy_property_set_create(vy_property_set** out_set) {
    if (out_set == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    auto* handle = new (std::nothrow) PropertyHandle{.magic = kPropertyMagic};
    if (handle == nullptr) {
        return VY_OUT_OF_MEMORY;
    }

    *out_set = reinterpret_cast<vy_property_set*>(handle);
    return VY_OK;
}

void vy_property_set_destroy(vy_property_set* set) {
    if (set == nullptr) {
        return;
    }

    auto* handle = reinterpret_cast<PropertyHandle*>(set);
    if (handle->magic != kPropertyMagic) {
        return;
    }

    handle->magic = 0;
    delete handle;
}

int32_t vy_property_set_apply_theme(vy_property_set* set, const vy_theme* theme) {
    if (!valid_property_set(set) || !valid_theme(theme)) {
        return VY_INVALID_ARGUMENT;
    }

    reinterpret_cast<PropertyHandle*>(set)->properties.apply_theme(
        reinterpret_cast<const ThemeHandle*>(theme)->theme
    );
    return VY_OK;
}

int32_t vy_property_set_set_color(
    vy_property_set* set,
    uint8_t property,
    uint8_t layer,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
) {
    if (!valid_property_set(set) || !valid_property(property) || !valid_style_layer(layer)) {
        return VY_INVALID_ARGUMENT;
    }

    return property_status(
        reinterpret_cast<PropertyHandle*>(set)->properties.set(
            static_cast<veyra::StyleProperty>(property),
            static_cast<veyra::StyleLayer>(layer),
            veyra::make_color_value(
                veyra::Srgba8{.red = red, .green = green, .blue = blue, .alpha = alpha}
            )
        )
    );
}

int32_t
vy_property_set_set_length(vy_property_set* set, uint8_t property, uint8_t layer, float value) {
    if (!valid_property_set(set) || !valid_property(property) || !valid_style_layer(layer)) {
        return VY_INVALID_ARGUMENT;
    }

    return property_status(
        reinterpret_cast<PropertyHandle*>(set)->properties.set(
            static_cast<veyra::StyleProperty>(property),
            static_cast<veyra::StyleLayer>(layer),
            veyra::make_length_value(value)
        )
    );
}

int32_t vy_property_set_set_duration(
    vy_property_set* set,
    uint8_t property,
    uint8_t layer,
    uint32_t value
) {
    if (!valid_property_set(set) || !valid_property(property) || !valid_style_layer(layer)) {
        return VY_INVALID_ARGUMENT;
    }

    return property_status(
        reinterpret_cast<PropertyHandle*>(set)->properties.set(
            static_cast<veyra::StyleProperty>(property),
            static_cast<veyra::StyleLayer>(layer),
            veyra::make_duration_value(value)
        )
    );
}

int32_t
vy_property_set_set_number(vy_property_set* set, uint8_t property, uint8_t layer, float value) {
    if (!valid_property_set(set) || !valid_property(property) || !valid_style_layer(layer)) {
        return VY_INVALID_ARGUMENT;
    }

    return property_status(
        reinterpret_cast<PropertyHandle*>(set)->properties.set(
            static_cast<veyra::StyleProperty>(property),
            static_cast<veyra::StyleLayer>(layer),
            veyra::make_number_value(value)
        )
    );
}

int32_t
vy_property_set_resolve(const vy_property_set* set, uint8_t property, vy_resolved_property* out) {
    if (!valid_property_set(set) || !valid_property(property) || out == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    const auto resolved = reinterpret_cast<const PropertyHandle*>(set)->properties.resolve(
        static_cast<veyra::StyleProperty>(property)
    );
    *out = {};
    out->size = sizeof(vy_resolved_property);
    out->version = VY_STRUCT_VERSION;
    out->property = property;
    out->set = resolved.set ? 1U : 0U;
    out->value_kind = static_cast<std::uint8_t>(resolved.value.kind);
    out->layer = static_cast<std::uint8_t>(resolved.provenance.layer);
    out->red = resolved.value.color.red;
    out->green = resolved.value.color.green;
    out->blue = resolved.value.color.blue;
    out->alpha = resolved.value.color.alpha;
    out->length = resolved.value.length;
    out->duration_ms = resolved.value.duration_ms;
    out->number = resolved.value.number;
    out->from_token = resolved.provenance.from_token ? 1U : 0U;
    out->token_kind = static_cast<std::uint8_t>(resolved.provenance.token_kind);
    out->token = resolved.provenance.token;
    out->line = resolved.provenance.line;
    return VY_OK;
}

int32_t vy_node_tree_create(vy_node_tree** out_tree) {
    if (out_tree == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    auto* handle = new (std::nothrow) TreeHandle{.magic = kTreeMagic};
    if (handle == nullptr) {
        return VY_OUT_OF_MEMORY;
    }

    *out_tree = reinterpret_cast<vy_node_tree*>(handle);
    return VY_OK;
}

void vy_node_tree_destroy(vy_node_tree* tree) {
    if (tree == nullptr) {
        return;
    }

    auto* handle = reinterpret_cast<TreeHandle*>(tree);
    if (handle->magic != kTreeMagic) {
        return;
    }

    handle->magic = 0;
    delete handle;
}

int32_t vy_node_tree_reserve(vy_node_tree* tree, uint64_t count) {
    if (!valid_tree(tree)) {
        return VY_INVALID_HANDLE;
    }

    try {
        reinterpret_cast<TreeHandle*>(tree)->tree.reserve(static_cast<std::size_t>(count));
    } catch (const std::bad_alloc&) {
        return VY_OUT_OF_MEMORY;
    }

    return VY_OK;
}

int32_t vy_node_create(vy_node_tree* tree, vy_node_id* out_node) {
    if (!valid_tree(tree) || out_node == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    try {
        *out_node = to_c(reinterpret_cast<TreeHandle*>(tree)->tree.create());
    } catch (const std::bad_alloc&) {
        return VY_OUT_OF_MEMORY;
    }

    return VY_OK;
}

int32_t vy_node_append(vy_node_tree* tree, vy_node_id parent, vy_node_id child) {
    if (!valid_tree(tree)) {
        return VY_INVALID_HANDLE;
    }

    return reinterpret_cast<TreeHandle*>(tree)->tree.append_child(from_c(parent), from_c(child))
        ? VY_OK
        : VY_INVALID_ARGUMENT;
}

int32_t vy_node_parent(const vy_node_tree* tree, vy_node_id node, vy_node_id* out_parent) {
    if (!valid_tree(tree) || out_parent == nullptr) {
        return VY_INVALID_ARGUMENT;
    }

    const auto& native = reinterpret_cast<const TreeHandle*>(tree)->tree;
    if (!native.contains(from_c(node))) {
        return VY_INVALID_ARGUMENT;
    }

    *out_parent = to_c(native.parent_of(from_c(node)));
    return VY_OK;
}

uint64_t vy_node_count(const vy_node_tree* tree) {
    if (!valid_tree(tree)) {
        return 0;
    }

    return reinterpret_cast<const TreeHandle*>(tree)->tree.size();
}
}
