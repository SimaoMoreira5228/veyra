#ifndef VEYRA_LAYOUT_GEOMETRY_HPP
#define VEYRA_LAYOUT_GEOMETRY_HPP

namespace veyra {

struct LayoutInsets {
    float top{};
    float right{};
    float bottom{};
    float left{};
};

struct LayoutRect {
    float x{};
    float y{};
    float width{};
    float height{};
};

}

#endif
