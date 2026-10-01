#ifndef VEYRA_ENGINE_HPP
#define VEYRA_ENGINE_HPP
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
namespace veyra {
struct Update {
    std::uint32_t id;
    std::uint32_t value;
};
struct Rect {
    float x, y, w, h;
    std::uint32_t color;
};
struct Stats {
    std::uint64_t visited{}, invalidated{}, emitted{};
};
class Engine {
  public:
    virtual ~Engine() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual void reset(std::size_t count) = 0;
    virtual void apply(std::span<const Update> changes) = 0;
    virtual Stats frame(std::size_t first, std::size_t visible, std::vector<Rect>& out) = 0;
    virtual std::uint64_t checksum() const = 0;
};
std::unique_ptr<Engine> create_engine(std::string_view name);
std::vector<std::string_view> available_engines();
}

#endif
