#include "veyra/core/hit_test.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_z_order_and_fallback() {
    veyra::NodeTree tree;
    tree.reserve(4);

    const auto root = tree.create();
    const auto back = tree.create();
    const auto front = tree.create();
    const auto nested = tree.create();
    require(tree.append_child(root, back), "back node attaches");
    require(tree.append_child(root, front), "front node attaches");
    require(tree.append_child(front, nested), "nested node attaches");

    const std::array bounds{
        veyra::LayoutRect{.width = 100.0F, .height = 100.0F},
        veyra::LayoutRect{.width = 50.0F, .height = 50.0F},
        veyra::LayoutRect{.width = 50.0F, .height = 50.0F},
        veyra::LayoutRect{.x = 40.0F, .y = 40.0F, .width = 20.0F, .height = 20.0F},
    };
    veyra::NodeId target;

    auto result = veyra::hit_test(tree, root, {45.0F, 45.0F}, bounds, target);
    require(result == veyra::HitTestResult::hit, "point hits the tree");
    require(target == nested, "deepest frontmost child wins");

    result = veyra::hit_test(tree, root, {20.0F, 20.0F}, bounds, target);
    require(result == veyra::HitTestResult::hit, "point hits a parent without a child hit");
    require(target == front, "frontmost parent wins over a lower sibling");

    result = veyra::hit_test(tree, root, {75.0F, 75.0F}, bounds, target);
    require(result == veyra::HitTestResult::hit, "point falls back to root");
    require(target == root, "root is the target outside child bounds");
}

void check_clipping_and_invalid_inputs() {
    veyra::NodeTree tree;
    tree.reserve(2);

    const auto root = tree.create();
    const auto child = tree.create();
    require(tree.append_child(root, child), "child attaches");

    const std::array bounds{
        veyra::LayoutRect{.width = 40.0F, .height = 40.0F},
        veyra::LayoutRect{.x = 20.0F, .y = 20.0F, .width = 40.0F, .height = 40.0F},
    };
    veyra::NodeId target;

    auto result = veyra::hit_test(tree, root, {45.0F, 45.0F}, bounds, target);
    require(result == veyra::HitTestResult::no_hit, "root bounds clip overflowing children");
    require(target.is_null(), "no hit clears the output target");

    result = veyra::hit_test(tree, {}, {1.0F, 1.0F}, bounds, target);
    require(result == veyra::HitTestResult::invalid_root, "stale root is rejected");

    result = veyra::hit_test(
        tree,
        root,
        {std::numeric_limits<float>::quiet_NaN(), 1.0F},
        bounds,
        target
    );
    require(result == veyra::HitTestResult::invalid_point, "non-finite points are rejected");

    result =
        veyra::hit_test(tree, root, {1.0F, 1.0F}, std::span<const veyra::LayoutRect>{}, target);
    require(
        result == veyra::HitTestResult::insufficient_bounds,
        "missing root bounds are reported"
    );

    auto invalid_bounds = bounds;
    invalid_bounds[0].width = -1.0F;
    result = veyra::hit_test(tree, root, {1.0F, 1.0F}, invalid_bounds, target);
    require(result == veyra::HitTestResult::invalid_bounds, "negative dimensions are rejected");
}

}

int main() {
    try {
        check_z_order_and_fallback();
        check_clipping_and_invalid_inputs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
