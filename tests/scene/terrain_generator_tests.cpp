#include "scene/terrain_generator.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "core/input/controller/camera_controller.hpp"
#include "scene/components/mesh_renderer.hpp"
#include "scene/components/transform.hpp"
#include "scene/scene.hpp"

namespace {
    const auto block_model = ResourceId<Model>{7};

    auto require(bool condition, const char* message) -> void {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    template <typename Exception, typename Function>
    auto require_throws(Function&& function, const char* message) -> void {
        try {
            function();
        } catch (const Exception&) {
            return;
        }
        throw std::runtime_error(message);
    }

    auto test_flat_terrain() -> void {
        Scene scene;
        scene.create_entity();
        scene.camera().set_position(glm::vec3{3.0F, 4.0F, 5.0F});
        scene.add_point_light(PointLight{.intensity = 17.0F});
        TerrainGenerator generator;
        const auto config = TerrainConfig{
            .min_height = 4,
            .max_height = 4,
            .block_size = 2.0F,
            .origin = glm::vec3{10.0F, 20.0F, 30.0F}
        };
        const auto count = generator.generate(scene, block_model, config);
        constexpr auto perimeter = 2 * TerrainGenerator::width +
            2 * TerrainGenerator::depth - 4;
        constexpr auto expected = 2 * TerrainGenerator::width *
            TerrainGenerator::depth + 2 * perimeter;
        require(count == expected, "flat terrain surface count is incorrect");
        require(scene.entity_count() == count + 1, "existing entities were lost");
        require(scene.camera().position() == glm::vec3{3.0F, 4.0F, 5.0F},
                "generator changed the camera");
        require(scene.point_lights().size() == 1 &&
                scene.point_lights()[0].intensity == 17.0F,
                "generator changed the lights");
        require(std::all_of(generator.height_map().begin(),
                            generator.height_map().end(),
                            [](auto height) { return height == 4; }),
                "flat height map is incorrect");

        std::size_t base_count = 0;
        std::size_t top_count = 0;
        std::size_t side_count = 0;
        for (const auto& entity : scene.entities().subspan(1)) {
            const auto* transform = entity.get_component<Transform>();
            const auto* renderer = entity.get_component<MeshRenderer>();
            require(transform != nullptr && renderer != nullptr,
                    "terrain entity lacks rendering components");
            require(renderer->model_id() == block_model,
                    "terrain does not share the supplied model");
            require(transform->scale() == glm::vec3{2.0F},
                    "block size was not applied");
            const auto position = transform->position();
            require(position.x >= 11.0F && position.x <= 265.0F &&
                    position.z >= 31.0F && position.z <= 285.0F,
                    "terrain origin or footprint is incorrect");
            if (position.y == 21.0F) {
                ++base_count;
            } else if (position.y == 27.0F) {
                ++top_count;
            } else {
                require(position.y == 23.0F || position.y == 25.0F,
                        "block heights are incorrect");
                require(position.x == 11.0F || position.x == 265.0F ||
                        position.z == 31.0F || position.z == 285.0F,
                        "fully buried block was created");
                ++side_count;
            }
        }
        require(base_count == TerrainGenerator::width * TerrainGenerator::depth &&
                top_count == base_count && side_count == 2 * perimeter,
                "flat terrain has missing surface blocks");

        Scene single_layer;
        generator.generate(single_layer, block_model,
                           TerrainConfig{.min_height = 1, .max_height = 1});
        require(single_layer.entity_count() == TerrainGenerator::width *
                TerrainGenerator::depth, "single-layer blocks were duplicated");
    }

    auto test_noise_and_exposure() -> void {
        Scene scene;
        TerrainGenerator generator;
        const auto config = TerrainConfig{};
        const auto count = generator.generate(scene, block_model, config);
        const auto first_heights = generator.height_map();
        require(count == scene.entity_count(), "reported block count is incorrect");
        const auto [minimum, maximum] = std::minmax_element(
            first_heights.begin(), first_heights.end()
        );
        require(*minimum >= config.min_height && *maximum <= config.max_height &&
                *minimum != *maximum, "noise heights are flat or out of range");

        // Check every voxel against its six neighbors, independently of the
        // generator's column-range shortcut, including boundary and base faces.
        const auto width = static_cast<int>(TerrainGenerator::width);
        const auto depth = static_cast<int>(TerrainGenerator::depth);
        const auto solid = [&](int x, int y, int z) {
            return x >= 0 && x < width && z >= 0 && z < depth && y >= 0 &&
                static_cast<uint32_t>(y) < generator.height_at(x, z);
        };
        std::vector<bool> present(TerrainGenerator::width * TerrainGenerator::depth *
                                  config.max_height, false);
        const auto index = [&](int x, int y, int z) {
            return (static_cast<std::size_t>(z) * width + x) *
                config.max_height + y;
        };
        for (const auto& entity : scene.entities()) {
            const auto position = entity.get_component<Transform>()->position();
            const auto x = static_cast<int>(std::floor(position.x - config.origin.x));
            const auto y = static_cast<int>(std::floor(position.y - config.origin.y));
            const auto z = static_cast<int>(std::floor(position.z - config.origin.z));
            require(solid(x, y, z), "entity is outside the solid terrain");
            require(!present[index(x, y, z)], "a surface block was duplicated");
            present[index(x, y, z)] = true;
        }
        for (int z = 0; z < depth; ++z) {
            for (int x = 0; x < width; ++x) {
                const auto height = generator.height_at(x, z);
                for (int y = 0; y < static_cast<int>(height); ++y) {
                    const auto exposed = !solid(x - 1, y, z) ||
                        !solid(x + 1, y, z) || !solid(x, y - 1, z) ||
                        !solid(x, y + 1, z) || !solid(x, y, z - 1) ||
                        !solid(x, y, z + 1);
                    require(present[index(x, y, z)] == exposed,
                            "terrain has a missing surface or a buried block");
                }
            }
        }

        scene.clear_entities();
        generator.generate(scene, block_model, config);
        require(generator.height_map() == first_heights &&
                scene.entity_count() == count, "same seed is not reproducible");
        scene.clear_entities();
        auto other_config = config;
        ++other_config.seed;
        generator.generate(scene, block_model, other_config);
        require(generator.height_map() != first_heights,
                "different seeds generated the same terrain");
        require_throws<std::out_of_range>(
            [&] { generator.height_at(TerrainGenerator::width, 0); },
            "invalid x coordinate was accepted"
        );
        require_throws<std::out_of_range>(
            [&] { generator.height_at(0, TerrainGenerator::depth); },
            "invalid z coordinate was accepted"
        );
        std::cout << "Default seed: " << count << " surface blocks, heights "
                  << *minimum << ".." << *maximum << '\n';
    }

    auto test_invalid_inputs() -> void {
        Scene scene;
        TerrainGenerator generator;
        require_throws<std::invalid_argument>(
            [&] { generator.generate(scene, ResourceId<Model>{}); },
            "invalid model was accepted"
        );
        auto config = TerrainConfig{};
        const auto rejected = [&] {
            require_throws<std::invalid_argument>(
                [&] { generator.generate(scene, block_model, config); },
                "invalid terrain config was accepted"
            );
            require(scene.entity_count() == 0, "invalid config modified the scene");
            require(std::all_of(generator.height_map().begin(),
                                generator.height_map().end(),
                                [](auto h) { return h == 0; }),
                    "invalid config modified the height map");
        };
        config.min_height = 0;
        rejected();
        config = TerrainConfig{};
        config.max_height = config.min_height - 1;
        rejected();
        for (const auto frequency : {0.0F, -1.0F, 2.0F,
                                     std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::infinity()}) {
            config = TerrainConfig{};
            config.noise_frequency = frequency;
            rejected();
        }
        for (const auto size : {0.0F, -1.0F,
                               std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::max()}) {
            config = TerrainConfig{};
            config.block_size = size;
            rejected();
        }
        config = TerrainConfig{};
        config.origin.z = std::numeric_limits<float>::quiet_NaN();
        rejected();
    }

    auto test_initial_camera_control() -> void {
        Camera camera;
        CameraController controller{camera};
        camera.set_orientation(glm::quat{glm::vec3{glm::radians(-20.0F), 0.0F, 0.0F}});
        const auto initial_forward = camera.forward();
        controller.look(100.0, 100.0);
        controller.look(100.0, 100.0);
        require(glm::length(camera.forward() - initial_forward) < 0.00001F,
                "first mouse input reset the scene's camera orientation");
    }
}

auto main() -> int {
    try {
        test_flat_terrain();
        test_noise_and_exposure();
        test_invalid_inputs();
        test_initial_camera_control();
        std::cout << "Terrain generator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Terrain generator test failed: " << error.what() << '\n';
        return 1;
    }
}
