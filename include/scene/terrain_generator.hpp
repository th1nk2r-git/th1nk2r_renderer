#ifndef TERRAIN_GENERATOR_HPP
#define TERRAIN_GENERATOR_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/vec3.hpp>

#include "resource/gpu/resource_id.hpp"

class Model;
class Scene;

struct TerrainConfig {
    uint32_t seed{12345};
    // Heights count solid blocks, starting at y = 0.
    uint32_t min_height{1};
    uint32_t max_height{10};
    // Base noise samples per block, in (0, 1].
    float noise_frequency{0.035F};
    float block_size{1.0F};
    // Minimum corner of the terrain's footprint and base plane.
    glm::vec3 origin{-64.0F, 0.0F, -64.0F};
};

class TerrainGenerator {
public:
    static constexpr std::size_t width = 128;
    static constexpr std::size_t depth = 128;
    using HeightMap = std::array<uint32_t, width * depth>;

    // Appends exposed blocks using a centered unit-cube model. Camera,
    // lights and existing entities remain owned by the caller.
    // Repeated calls append another terrain; they do not replace entities.
    auto generate(
        Scene& scene,
        ResourceId<Model> block_model,
        const TerrainConfig& config = {}
    ) -> std::size_t;

    // Zero heights before the first generation; indexed as z * width + x.
    auto height_map() const noexcept -> const HeightMap& {
        return heights_;
    }

    auto height_at(std::size_t x, std::size_t z) const -> uint32_t;

    auto config() const noexcept -> const TerrainConfig& {
        return config_;
    }

private:
    TerrainConfig config_;
    HeightMap heights_{};
};

#endif
