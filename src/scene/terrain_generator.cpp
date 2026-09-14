#include "scene/terrain_generator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "scene/components/mesh_renderer.hpp"
#include "scene/components/transform.hpp"
#include "scene/scene.hpp"

namespace {
    auto validate_config(const TerrainConfig& config) -> void {
        if (config.min_height == 0 || config.max_height < config.min_height) {
            throw std::invalid_argument(
                "terrain heights must satisfy 1 <= min_height <= max_height"
            );
        }
        if (!std::isfinite(config.noise_frequency) ||
            config.noise_frequency <= 0.0F || config.noise_frequency > 1.0F) {
            throw std::invalid_argument(
                "terrain noise frequency must be finite and in (0, 1]"
            );
        }
        if (!std::isfinite(config.block_size) || config.block_size <= 0.0F) {
            throw std::invalid_argument(
                "terrain block size must be finite and greater than zero"
            );
        }

        const auto extents = std::array<double, 3>{
            TerrainGenerator::width * static_cast<double>(config.block_size),
            config.max_height * static_cast<double>(config.block_size),
            TerrainGenerator::depth * static_cast<double>(config.block_size)
        };
        for (std::size_t axis = 0; axis < extents.size(); ++axis) {
            const auto base = config.origin[static_cast<int>(axis)];
            const auto end = base + extents[axis];
            if (!std::isfinite(base) ||
                end > std::numeric_limits<float>::max()) {
                throw std::invalid_argument(
                    "terrain world bounds must fit finite float coordinates"
                );
            }
        }
    }

    // Integer hashing makes lattice values deterministic for every seed.
    auto lattice_value(uint32_t x, uint32_t z, uint32_t seed) noexcept -> double {
        auto hash = seed ^ (x * 0x9E3779B9U) ^ (z * 0x85EBCA6BU);
        hash ^= hash >> 16;
        hash *= 0x7FEB352DU;
        hash ^= hash >> 15;
        hash *= 0x846CA68BU;
        hash ^= hash >> 16;
        return static_cast<double>(hash >> 8) / 16777215.0;
    }

    auto fade(double value) noexcept -> double {
        return value * value * value *
            (value * (value * 6.0 - 15.0) + 10.0);
    }

    auto value_noise(double x, double z, uint32_t seed) noexcept -> double {
        const auto ix = static_cast<uint32_t>(std::floor(x));
        const auto iz = static_cast<uint32_t>(std::floor(z));
        const auto tx = fade(x - ix);
        const auto tz = fade(z - iz);
        return std::lerp(
            std::lerp(lattice_value(ix, iz, seed),
                      lattice_value(ix + 1, iz, seed), tx),
            std::lerp(lattice_value(ix, iz + 1, seed),
                      lattice_value(ix + 1, iz + 1, seed), tx),
            tz
        );
    }

    auto terrain_noise(double x, double z, uint32_t seed) noexcept -> double {
        double value = 0.0;
        double weight = 0.0;
        double amplitude = 1.0;
        constexpr uint32_t octave_count = 4;
        for (uint32_t octave = 0; octave < octave_count; ++octave) {
            value += value_noise(x, z, seed + octave * 0x9E3779B9U) * amplitude;
            weight += amplitude;
            x *= 2.0;
            z *= 2.0;
            amplitude *= 0.5;
        }
        return std::clamp(value / weight, 0.0, 1.0);
    }
}

auto TerrainGenerator::generate(
    Scene& scene,
    ResourceId<Model> block_model,
    const TerrainConfig& config
) -> std::size_t {
    validate_config(config);
    if (!block_model.valid()) {
        throw std::invalid_argument("terrain block model resource id is invalid");
    }

    config_ = config;
    const auto height_range = static_cast<double>(config.max_height) -
        config.min_height;
    for (std::size_t z = 0; z < depth; ++z) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto noise = terrain_noise(
                x * static_cast<double>(config.noise_frequency),
                z * static_cast<double>(config.noise_frequency),
                config.seed
            );
            heights_[z * width + x] = static_cast<uint32_t>(std::round(
                config.min_height + noise * height_range
            ));
        }
    }

    std::size_t block_count = 0;
    for (std::size_t z = 0; z < depth; ++z) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto height = heights_[z * width + x];
            // The top block is always exposed. Lower blocks are exposed when
            // they rise above any neighboring column; edges border air.
            auto first_side_block = uint32_t{0};
            if (x > 0 && x + 1 < width && z > 0 && z + 1 < depth) {
                first_side_block = std::min({
                    heights_[z * width + x - 1],
                    heights_[z * width + x + 1],
                    heights_[(z - 1) * width + x],
                    heights_[(z + 1) * width + x]
                });
            }
            const auto first_block = std::min(height - 1, first_side_block);
            const auto add_block = [&](uint32_t y) {
                auto& entity = scene.create_entity();
                auto& transform = entity.add_component<Transform>();
                transform.position() = glm::vec3{
                    static_cast<float>(config.origin.x +
                        (static_cast<double>(x) + 0.5) * config.block_size),
                    static_cast<float>(config.origin.y +
                        (static_cast<double>(y) + 0.5) * config.block_size),
                    static_cast<float>(config.origin.z +
                        (static_cast<double>(z) + 0.5) * config.block_size)
                };
                transform.scale() = glm::vec3{config.block_size};
                entity.add_component<MeshRenderer>(block_model);
                ++block_count;
            };

            // The base is also exposed from below, even for interior columns.
            add_block(0);
            for (auto y = std::max(uint32_t{1}, first_block); y < height; ++y) {
                add_block(y);
            }
        }
    }
    return block_count;
}

auto TerrainGenerator::height_at(std::size_t x, std::size_t z) const -> uint32_t {
    if (x >= width || z >= depth) {
        throw std::out_of_range("terrain height coordinates are out of bounds");
    }
    return heights_[z * width + x];
}
