#include "resource/registry/resource_registry.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
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

    auto triangle() -> MeshData {
        return MeshData{
            .vertices_ = {
                Vertex{.position = {-2.0F, 4.0F, 1.0F}},
                Vertex{.position = {3.0F, -1.0F, 2.0F}},
                Vertex{.position = {0.0F, 2.0F, -5.0F}}
            },
            .indices_ = {0, 2, 1}
        };
    }

    auto test_mesh_ranges_and_bounds() -> void {
        ResourceRegistry registry;
        const auto first = registry.add(triangle());
        auto quad = triangle();
        quad.vertices_.push_back(Vertex{.position = {7.0F, 8.0F, 9.0F}});
        quad.indices_ = {0, 1, 2, 2, 1, 3};
        const auto second = registry.add(std::move(quad));
        const auto third = registry.add(triangle());

        const auto& a = registry.query(first);
        const auto& b = registry.query(second);
        const auto& c = registry.query(third);
        require(first.value() == 0 && second.value() == 1 && third.value() == 2,
                "mesh ids do not follow registration order");
        require(a.vertex_offset == 0 && a.vertex_count == 3 &&
                a.first_index == 0 && a.index_count == 3,
                "first mesh range is incorrect");
        require(b.vertex_offset == 3 && b.vertex_count == 4 &&
                b.first_index == 3 && b.index_count == 6,
                "second mesh range is incorrect");
        require(c.vertex_offset == 7 && c.vertex_count == 3 &&
                c.first_index == 9 && c.index_count == 3,
                "vertex and index offsets were confused with byte offsets");
        require(a.bounds.minimum == glm::vec3{-2.0F, -1.0F, -5.0F} &&
                a.bounds.maximum == glm::vec3{3.0F, 4.0F, 2.0F},
                "mesh bounds are incorrect");
        require(b.bounds.maximum == glm::vec3{7.0F, 8.0F, 9.0F},
                "mesh bounds were shared between meshes");

        require_throws<std::logic_error>([&] { registry.vertex_buffer(); },
                                        "vertex buffer was exposed before upload");
        require_throws<std::logic_error>([&] { registry.index_buffer(); },
                                        "index buffer was exposed before upload");
    }

    auto test_invalid_meshes_do_not_consume_ranges() -> void {
        ResourceRegistry registry;
        const auto first = registry.add(triangle());
        require_throws<std::invalid_argument>([&] { registry.add(MeshData{}); },
                                             "empty mesh was accepted");
        auto missing_indices = triangle();
        missing_indices.indices_.clear();
        require_throws<std::invalid_argument>(
            [&] { registry.add(std::move(missing_indices)); },
            "mesh without indices was accepted"
        );
        auto missing_vertices = triangle();
        missing_vertices.vertices_.clear();
        require_throws<std::invalid_argument>(
            [&] { registry.add(std::move(missing_vertices)); },
            "mesh without vertices was accepted"
        );
        auto invalid_index = triangle();
        invalid_index.indices_[1] = 3;
        require_throws<std::out_of_range>(
            [&] { registry.add(std::move(invalid_index)); },
            "out-of-range local index was accepted"
        );
        const auto next = registry.add(triangle());
        require(first.value() == 0 && next.value() == 1 &&
                registry.query(next).vertex_offset == 3 &&
                registry.query(next).first_index == 3,
                "failed registrations consumed ids or geometry ranges");
        require_throws<std::out_of_range>(
            [&] { registry.query(ResourceId<Mesh>{}); }, "invalid id was accepted"
        );
        require_throws<std::out_of_range>(
            [&] { registry.query(ResourceId<Mesh>{2}); }, "missing id was accepted"
        );
    }

    auto test_model_ids_survive_more_mesh_imports() -> void {
        ResourceRegistry registry;
        const auto mesh_id = registry.add(triangle());
        const auto* mesh_address = &registry.query(mesh_id);
        const auto model_id = registry.add(std::make_unique<Model>(
            std::vector<Primitive>{
                Primitive{.mesh = mesh_id, .material = ResourceId<Material>{7}}
            }
        ));
        registry.set_model_name(model_id, "triangle");
        for (size_t i = 0; i < 512; ++i) {
            registry.add(triangle());
        }
        require(&registry.query(mesh_id) == mesh_address,
                "later mesh registrations invalidated an existing mesh reference");
        const auto& model = registry.query_model("triangle");
        require(registry.query_model_id("triangle") == model_id &&
                model.primitives()[0].mesh == mesh_id &&
                model.primitives()[0].material == ResourceId<Material>{7},
                "later mesh registrations changed model references");
        require(registry.query(model.primitives()[0].mesh).vertex_offset == 0,
                "original mesh id no longer resolves to its range");
        const auto& last = registry.query(ResourceId<Mesh>{512});
        require(last.vertex_offset == 1536 && last.first_index == 1536,
                "mesh ranges were lost while growing the registry");
    }
}

auto main() -> int {
    try {
        test_mesh_ranges_and_bounds();
        test_invalid_meshes_do_not_consume_ranges();
        test_model_ids_survive_more_mesh_imports();
        std::cout << "Resource registry tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Resource registry tests failed: " << error.what() << '\n';
        return 1;
    }
}
