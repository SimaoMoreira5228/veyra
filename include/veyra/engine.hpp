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

    virtual Stats frame(std::size_t first, std::size_t visible, std::span<Rect> out) = 0;

    virtual std::uint64_t checksum() const = 0;
};

class ArenaEngine final : public Engine {
  public:
    std::string_view name() const noexcept override;

    void reset(std::size_t count) override;

    void apply(std::span<const Update> changes) override;

    Stats frame(std::size_t first, std::size_t visible, std::span<Rect> out) override;

    std::uint64_t checksum() const override;

  private:
    struct Node {
        std::uint32_t value{};
        std::uint32_t painted{};
        bool dirty{true};
    };

    std::vector<Node> nodes_;
};

class ReactiveEngine final : public Engine {
  public:
    std::string_view name() const noexcept override;

    void reset(std::size_t count) override;

    void apply(std::span<const Update> changes) override;

    Stats frame(std::size_t first, std::size_t visible, std::span<Rect> out) override;

    std::uint64_t checksum() const override;

  private:
    std::vector<std::uint32_t> values_;
    std::vector<std::uint32_t> cache_;
    std::vector<std::uint8_t> pending_;
};

class EcsEngine final : public Engine {
  public:
    std::string_view name() const noexcept override;

    void reset(std::size_t count) override;

    void apply(std::span<const Update> changes) override;

    Stats frame(std::size_t first, std::size_t visible, std::span<Rect> out) override;

    std::uint64_t checksum() const override;

  private:
    std::vector<std::uint32_t> values_;
    std::vector<std::uint32_t> painted_;
    std::vector<std::uint8_t> changed_;
};

std::unique_ptr<Engine> create_engine(std::string_view name);

std::span<const std::string_view> available_engines() noexcept;

}

#endif
