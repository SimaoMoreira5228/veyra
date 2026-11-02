#include "veyra/c/veyra.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

int main(void) {
    CHECK(vy_abi_version() == VY_ABI_VERSION);
    CHECK(vy_status_name(VY_OK) != NULL);
    CHECK(strcmp(vy_status_name(VY_OUT_OF_MEMORY), "out of memory") == 0);

    vy_theme* theme = NULL;
    CHECK(vy_theme_create(VY_THEME_DARK, &theme) == VY_OK);
    CHECK(theme != NULL);

    uint8_t rgba[4] = {0, 0, 0, 0};
    CHECK(vy_theme_color(theme, VY_COLOR_SURFACE, rgba) == VY_OK);
    CHECK(rgba[0] == 28 && rgba[1] == 30 && rgba[2] == 38 && rgba[3] == 255);

    CHECK(
        vy_theme_set_color(theme, VY_COLOR_ACTION_PRIMARY, 10, 20, 30, 40, VY_LAYER_OVERRIDE) ==
        VY_OK
    );
    CHECK(vy_theme_color(theme, VY_COLOR_ACTION_PRIMARY, rgba) == VY_OK);
    CHECK(rgba[0] == 10 && rgba[1] == 20 && rgba[2] == 30 && rgba[3] == 40);
    CHECK(
        vy_theme_set_color(theme, VY_COLOR_COUNT, 0, 0, 0, 0, VY_LAYER_OVERRIDE) ==
        VY_INVALID_ARGUMENT
    );

    float length = 0.0F;
    CHECK(vy_theme_set_space(theme, VY_SPACE_LARGE, 22.0F, VY_LAYER_OVERRIDE) == VY_OK);
    CHECK(vy_theme_space(theme, VY_SPACE_LARGE, &length) == VY_OK && length == 22.0F);
    CHECK(vy_theme_set_radius(theme, VY_RADIUS_SMALL, 3.0F, VY_LAYER_OVERRIDE) == VY_OK);
    CHECK(vy_theme_radius(theme, VY_RADIUS_SMALL, &length) == VY_OK && length == 3.0F);

    uint32_t motion = 0;
    CHECK(vy_theme_set_motion(theme, VY_MOTION_FAST, 80, VY_LAYER_OVERRIDE) == VY_OK);
    CHECK(vy_theme_motion(theme, VY_MOTION_FAST, &motion) == VY_OK && motion == 80);

    const char file_text[] = "schema: 1\ncolor_surface = #102030\n";
    vy_theme_error error = {0};
    CHECK(
        vy_theme_apply(
            theme,
            (vy_utf8){.data = file_text, .size = sizeof(file_text) - 1},
            &error
        ) == VY_OK
    );
    CHECK(error.size == sizeof(vy_theme_error) && error.version == VY_STRUCT_VERSION);
    CHECK(vy_theme_color(theme, VY_COLOR_SURFACE, rgba) == VY_OK);
    CHECK(rgba[0] == 16 && rgba[1] == 32 && rgba[2] == 48);

    const char broken[] = "schema: 1\ncolor_missing = #ffffff\n";
    CHECK(
        vy_theme_apply(theme, (vy_utf8){.data = broken, .size = sizeof(broken) - 1}, &error) ==
        VY_FILE_UNKNOWN_TOKEN
    );
    CHECK(error.status == VY_FILE_UNKNOWN_TOKEN && error.line == 2);

    vy_property_set* properties = NULL;
    CHECK(vy_property_set_create(&properties) == VY_OK);
    CHECK(vy_property_set_apply_theme(properties, theme) == VY_OK);

    vy_resolved_property resolved = {0};
    CHECK(vy_property_set_resolve(properties, VY_PROPERTY_BACKGROUND, &resolved) == VY_OK);
    CHECK(resolved.size == sizeof(vy_resolved_property));
    CHECK(resolved.set == 1 && resolved.layer == VY_STYLE_THEME);
    CHECK(resolved.value_kind == VY_VALUE_COLOR);
    CHECK(resolved.from_token == 1 && resolved.token_kind == VY_TOKEN_COLOR);
    CHECK(resolved.token == VY_COLOR_SURFACE);
    CHECK(resolved.red == 16 && resolved.green == 32 && resolved.blue == 48);

    CHECK(
        vy_property_set_set_color(
            properties,
            VY_PROPERTY_FOREGROUND,
            VY_STYLE_LOCAL,
            200,
            10,
            20,
            255
        ) == VY_OK
    );
    CHECK(vy_property_set_resolve(properties, VY_PROPERTY_FOREGROUND, &resolved) == VY_OK);
    CHECK(resolved.layer == VY_STYLE_LOCAL && resolved.from_token == 0);
    CHECK(
        vy_property_set_set_length(properties, VY_PROPERTY_FOREGROUND, VY_STYLE_LOCAL, 4.0F) ==
        VY_INVALID_VALUE
    );

    vy_node_tree* tree = NULL;
    CHECK(vy_node_tree_create(&tree) == VY_OK);
    CHECK(vy_node_tree_reserve(tree, 8) == VY_OK);

    vy_node_id root = {0};
    vy_node_id child = {0};
    CHECK(vy_node_create(tree, &root) == VY_OK);
    CHECK(vy_node_create(tree, &child) == VY_OK);
    CHECK(vy_node_append(tree, root, child) == VY_OK);
    CHECK(vy_node_count(tree) == 2);

    vy_node_id parent = {0};
    CHECK(vy_node_parent(tree, child, &parent) == VY_OK);
    CHECK(parent.index == root.index && parent.generation == root.generation);
    CHECK(vy_node_parent(tree, root, &parent) == VY_OK);
    CHECK(parent.index == UINT32_MAX);
    CHECK(vy_node_append(tree, root, root) == VY_INVALID_ARGUMENT);

    CHECK(vy_theme_create(99, &theme) == VY_INVALID_ARGUMENT);
    CHECK(vy_theme_color(NULL, VY_COLOR_SURFACE, rgba) == VY_INVALID_ARGUMENT);
    CHECK(vy_property_set_resolve(NULL, VY_PROPERTY_BACKGROUND, &resolved) == VY_INVALID_ARGUMENT);
    CHECK(vy_node_count(NULL) == 0);

    vy_theme_destroy(theme);
    vy_property_set_destroy(properties);
    vy_node_tree_destroy(tree);
    vy_theme_destroy(NULL);
    vy_property_set_destroy(NULL);
    vy_node_tree_destroy(NULL);

    if (failures != 0) {
        fprintf(stderr, "%d C ABI checks failed\n", failures);
        return 1;
    }

    printf("C ABI checks passed\n");
    return 0;
}
