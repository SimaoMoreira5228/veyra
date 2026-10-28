#ifndef VEYRA_LAYOUT_GEOMETRY_HPP
#define VEYRA_LAYOUT_GEOMETRY_HPP

namespace veyra {

struct LayoutPoint {
    float x{};
    float y{};
};

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

namespace veyra::foundation {
using ::veyra::LayoutInsets;
using ::veyra::LayoutPoint;
using ::veyra::LayoutRect;
}

#endif
