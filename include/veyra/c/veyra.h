#ifndef VEYRA_C_VEYRA_H
#define VEYRA_C_VEYRA_H

#include <stdint.h>

#if defined(_WIN32)
#if defined(VEYRA_C_BUILD)
#define VY_API __declspec(dllexport)
#else
#define VY_API
#endif
#else
#define VY_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VY_ABI_VERSION (((uint32_t)0 << 16U) | (uint32_t)1)
#define VY_STRUCT_VERSION 1U

typedef struct vy_theme vy_theme;
typedef struct vy_property_set vy_property_set;
typedef struct vy_node_tree vy_node_tree;

typedef struct vy_utf8 {
    const char* data;
    uint64_t size;
} vy_utf8;

typedef struct vy_node_id {
    uint32_t index;
    uint32_t generation;
} vy_node_id;

enum vy_status {
    VY_OK = 0,
    VY_INVALID_ARGUMENT = 1,
    VY_INVALID_HANDLE = 2,
    VY_INVALID_TOKEN = 3,
    VY_INVALID_VALUE = 4,
    VY_OUT_OF_MEMORY = 5,
    VY_FILE_INVALID_SCHEMA = 6,
    VY_FILE_UNKNOWN_TOKEN = 7,
    VY_FILE_BAD_VALUE = 8,
    VY_FILE_DUPLICATE_TOKEN = 9,
    VY_FILE_ALIAS_CYCLE = 10,
    VY_INVALID_OPERATION = 11,
};

enum vy_theme_variant {
    VY_THEME_LIGHT = 0,
    VY_THEME_DARK = 1,
    VY_THEME_HIGH_CONTRAST = 2,
};

enum vy_theme_layer {
    VY_LAYER_DEFAULTS = 0,
    VY_LAYER_PRESET = 1,
    VY_LAYER_FILE = 2,
    VY_LAYER_OVERRIDE = 3,
};

enum vy_color_token {
    VY_COLOR_SURFACE = 0,
    VY_COLOR_TEXT_PRIMARY = 1,
    VY_COLOR_ACTION_PRIMARY = 2,
    VY_COLOR_ACTION_ON_PRIMARY = 3,
    VY_COLOR_ACTION_HOVERED = 4,
    VY_COLOR_ACTION_PRESSED = 5,
    VY_COLOR_ACTION_DISABLED = 6,
    VY_COLOR_TEXT_DISABLED = 7,
    VY_COLOR_TOGGLE_CHECKED = 8,
    VY_COLOR_TOGGLE_ON_CHECKED = 9,
    VY_COLOR_ERROR = 10,
    VY_COLOR_COUNT = 11,
};

enum vy_space_token {
    VY_SPACE_NONE = 0,
    VY_SPACE_EXTRA_SMALL = 1,
    VY_SPACE_SMALL = 2,
    VY_SPACE_MEDIUM = 3,
    VY_SPACE_LARGE = 4,
    VY_SPACE_EXTRA_LARGE = 5,
    VY_SPACE_COUNT = 6,
};

enum vy_radius_token {
    VY_RADIUS_NONE = 0,
    VY_RADIUS_SMALL = 1,
    VY_RADIUS_MEDIUM = 2,
    VY_RADIUS_LARGE = 3,
    VY_RADIUS_COUNT = 4,
};

enum vy_motion_token {
    VY_MOTION_INSTANT = 0,
    VY_MOTION_FAST = 1,
    VY_MOTION_NORMAL = 2,
    VY_MOTION_SLOW = 3,
    VY_MOTION_COUNT = 4,
};

enum vy_style_layer {
    VY_STYLE_PLATFORM_DEFAULT = 0,
    VY_STYLE_THEME = 1,
    VY_STYLE_RECIPE = 2,
    VY_STYLE_VARIANT = 3,
    VY_STYLE_STATE = 4,
    VY_STYLE_LOCAL = 5,
    VY_STYLE_CONTROLLER = 6,
    VY_STYLE_ACCESSIBILITY = 7,
};

enum vy_style_property {
    VY_PROPERTY_FOREGROUND = 0,
    VY_PROPERTY_BACKGROUND = 1,
    VY_PROPERTY_BORDER_COLOR = 2,
    VY_PROPERTY_CORNER_RADIUS = 3,
    VY_PROPERTY_BORDER_WIDTH = 4,
    VY_PROPERTY_PADDING = 5,
    VY_PROPERTY_FONT_SIZE = 6,
    VY_PROPERTY_OPACITY = 7,
    VY_PROPERTY_TRANSITION_DURATION = 8,
    VY_PROPERTY_COUNT = 9,
};

enum vy_property_value_kind {
    VY_VALUE_COLOR = 0,
    VY_VALUE_LENGTH = 1,
    VY_VALUE_DURATION = 2,
    VY_VALUE_NUMBER = 3,
};

enum vy_token_kind {
    VY_TOKEN_COLOR = 0,
    VY_TOKEN_SPACE = 1,
    VY_TOKEN_RADIUS = 2,
    VY_TOKEN_MOTION = 3,
};

typedef struct vy_theme_error {
    uint32_t size;
    uint32_t version;
    int32_t status;
    uint32_t line;
    uint32_t column;
} vy_theme_error;

typedef struct vy_resolved_property {
    uint32_t size;
    uint32_t version;
    uint8_t property;
    uint8_t set;
    uint8_t value_kind;
    uint8_t layer;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
    float length;
    uint32_t duration_ms;
    float number;
    uint8_t from_token;
    uint8_t token_kind;
    uint8_t token;
    uint32_t line;
} vy_resolved_property;

/* Text is borrowed for the duration of the call; it is UTF-8, not NUL-terminated. */
VY_API const char* vy_status_name(int32_t status);
VY_API uint32_t vy_abi_version(void);

VY_API int32_t vy_theme_create(int32_t variant, vy_theme** out_theme);
VY_API void vy_theme_destroy(vy_theme* theme);
VY_API int32_t vy_theme_set_color(
    vy_theme* theme,
    uint8_t token,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha,
    uint8_t layer
);
VY_API int32_t vy_theme_color(const vy_theme* theme, uint8_t token, uint8_t out_rgba[4]);
VY_API int32_t vy_theme_set_space(vy_theme* theme, uint8_t token, float value, uint8_t layer);
VY_API int32_t vy_theme_space(const vy_theme* theme, uint8_t token, float* out_value);
VY_API int32_t vy_theme_set_radius(vy_theme* theme, uint8_t token, float value, uint8_t layer);
VY_API int32_t vy_theme_radius(const vy_theme* theme, uint8_t token, float* out_value);
VY_API int32_t vy_theme_set_motion(vy_theme* theme, uint8_t token, uint32_t value, uint8_t layer);
VY_API int32_t vy_theme_motion(const vy_theme* theme, uint8_t token, uint32_t* out_value);
VY_API int32_t vy_theme_apply(vy_theme* theme, vy_utf8 text, vy_theme_error* error);

VY_API int32_t vy_property_set_create(vy_property_set** out_set);
VY_API void vy_property_set_destroy(vy_property_set* set);
VY_API int32_t vy_property_set_apply_theme(vy_property_set* set, const vy_theme* theme);
VY_API int32_t vy_property_set_set_color(
    vy_property_set* set,
    uint8_t property,
    uint8_t layer,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
);
VY_API int32_t
vy_property_set_set_length(vy_property_set* set, uint8_t property, uint8_t layer, float value);
VY_API int32_t
vy_property_set_set_duration(vy_property_set* set, uint8_t property, uint8_t layer, uint32_t value);
VY_API int32_t
vy_property_set_set_number(vy_property_set* set, uint8_t property, uint8_t layer, float value);
VY_API int32_t
vy_property_set_resolve(const vy_property_set* set, uint8_t property, vy_resolved_property* out);

VY_API int32_t vy_node_tree_create(vy_node_tree** out_tree);
VY_API void vy_node_tree_destroy(vy_node_tree* tree);
VY_API int32_t vy_node_tree_reserve(vy_node_tree* tree, uint64_t count);
VY_API int32_t vy_node_create(vy_node_tree* tree, vy_node_id* out_node);
VY_API int32_t vy_node_append(vy_node_tree* tree, vy_node_id parent, vy_node_id child);
VY_API int32_t vy_node_parent(const vy_node_tree* tree, vy_node_id node, vy_node_id* out_parent);
VY_API uint64_t vy_node_count(const vy_node_tree* tree);

#ifdef __cplusplus
}
#endif

#endif
