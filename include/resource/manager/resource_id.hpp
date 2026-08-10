#ifndef RESOURCE_ID_HPP
#define RESOURCE_ID_HPP

#include <cstdint>
#include <limits>

template <typename T>
class ResourceId {
public:
    static constexpr auto invalid_value = std::numeric_limits<uint32_t>::max();

    constexpr ResourceId() noexcept = default;
    constexpr explicit ResourceId(uint32_t value) noexcept : value_(value) {}

    auto value() const noexcept -> uint32_t {
        return value_;
    }

    auto valid() const noexcept -> bool {
        return value_ != invalid_value;
    }

    auto operator==(const ResourceId&) const noexcept -> bool = default;

private:
    uint32_t value_ = invalid_value;
};

#endif
