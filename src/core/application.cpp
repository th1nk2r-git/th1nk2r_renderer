#include "core/application.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "io/model_loader.hpp"
#include "io/image_loader.hpp"
#include "resource/gpu/material.hpp"
#include "resource/gpu/mesh.hpp"
#include "resource/gpu/model.hpp"

namespace {
    constexpr std::size_t shadow_recording_slot = 0;
    constexpr std::size_t forward_recording_slot = 1;

    auto make_model_name(
        const std::filesystem::path& root,
        const std::filesystem::path& model_path
    ) -> std::string {
        const auto relative_directory = model_path.parent_path().lexically_relative(root);
        if (relative_directory.empty() || relative_directory == ".") {
            return model_path.stem().string();
        }
        return relative_directory.generic_string();
    }

    auto import_model(
        const std::filesystem::path& path,
        DeviceContext& device_context,
        ResourceRegistry& registry
    ) -> Model {
        const auto data = load_model(path);
        if (data.material_.empty()) {
            throw std::runtime_error(
                "loaded model does not contain a local material list"
            );
        }

        std::vector<std::optional<ResourceId<Material>>> material_ids(
            data.material_.size()
        );
        std::vector<Mesh> meshes;
        meshes.reserve(data.meshes_.size());

        for (const auto& mesh_data : data.meshes_) {
            const auto material_index = mesh_data.material_index_;
            if (material_index >= data.material_.size()) {
                throw std::out_of_range(
                    "mesh local material index is out of range"
                );
            }

            auto& material_id = material_ids[material_index];
            if (!material_id) {
                auto material = std::make_unique<Material>(
                    data.material_[material_index],
                    device_context.device(),
                    device_context.allocator(),
                    device_context.image_uploader()
                );
                material_id = registry.add(std::move(material));
            }

            meshes.emplace_back(
                mesh_data,
                *material_id,
                device_context.allocator(),
                device_context.buffer_uploader()
            );
        }

        return Model{std::move(meshes)};
    }

}

Application::Application()
    : window_(1200, 800),
      device_context_(window_),
      renderer_(device_context_, window_),
      shadow_pass_(
          device_context_.device(),
          device_context_.allocator(),
          renderer_.frame_count()
      ),
      forward_pass_(
          device_context_.device(),
          device_context_.allocator(),
          renderer_.render_pass(),
          shadow_pass_.descriptor_set_layout(),
          renderer_.frame_count()
      ),
      input_system_(
          window_,
          scene_.camera(),
          GeneralController::Callbacks{
              .toggle_fps = [this]() {
                  fps_enabled_ = !fps_enabled_;
                  frame_rate_counter_.reset();
              }
          }
      ),
      imgui_layer_(
          window_,
          device_context_,
          renderer_.render_pass(),
          renderer_.image_count()
      ) {}

auto Application::run() -> void {
    const auto material_ids = load_models("./assets");
    shadow_pass_.write_material(material_ids, registry_);
    forward_pass_.write_material(material_ids, registry_);
    forward_pass_.write_environment(
        load_image_rgba32f("./assets/sponza/mud_road_puresky_2k.hdr"),
        device_context_.image_uploader()
    );
    setup_scene();
    loop();
}

auto Application::load_models(const std::filesystem::path& root)
    -> std::vector<ResourceId<Material>> {
    std::vector<std::filesystem::path> model_paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto extension = entry.path().extension().string();
        std::ranges::transform(
            extension,
            extension.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        if (extension == ".obj" || extension == ".fbx" ||extension == ".gltf" || extension == ".glb") {
            model_paths.push_back(entry.path());
        }
    }

    std::ranges::sort(model_paths);
    std::vector<ResourceId<Material>> material_ids;
    std::unordered_set<uint32_t> material_id_values;
    for (const auto& path : model_paths) {
        auto name = make_model_name(root, path);
        if (name.empty()) {
            throw std::invalid_argument("model name cannot be empty");
        }
        if (registry_.contains_model(name)) {
            throw std::invalid_argument(
                "model name is already registered: " + name
            );
        }

        auto model = import_model(
            path,
            device_context_,
            registry_
        );
        for (const auto& mesh : model.meshes()) {
            const auto material_id = mesh.material();
            if (material_id_values.insert(material_id.value()).second) {
                material_ids.push_back(material_id);
            }
        }
        const auto model_id = registry_.add(
            std::make_unique<Model>(std::move(model))
        );
        registry_.set_model_name(model_id, std::move(name));
    }
    device_context_.buffer_uploader().submit_and_wait();
    device_context_.image_uploader().submit_and_wait();
    return material_ids;
}

auto Application::setup_scene() -> void {
    scene_.create_entity(
        registry_.query_model_id("sponza"),
        Transform {}
    );

    scene_.camera().set_position(glm::vec3{-2.0F, 2.0F, 7.0F});

    scene_.add_point_light(
        PointLight{
            .position = glm::vec3{0.0F, 6.0F, 0.0F},
            .color = glm::vec3{1.0F, 0.9F, 0.75F},
            .intensity = 100.0F,
            .casts_shadow = true,
            .shadow_near = 0.1F,
            .shadow_far = 25.0F,
            .source_radius = 0.2F
        }
    );
}

auto Application::update(float delta_time) -> void {
    input_system_.update(delta_time);
}

auto Application::loop() -> void {
    auto previous_time = glfwGetTime();

    while (!window_.should_close()) {
        if (fps_enabled_) {
            frame_rate_counter_.begin_frame();
        }
        window_.poll_events();

        const auto current_time = glfwGetTime();
        const auto delta_time = std::min(
            static_cast<float>(current_time - previous_time),
            0.05F
        );
        previous_time = current_time;

        if (window_.consume_framebuffer_resized()) {
            if (renderer_.recreate_swapchain(device_context_, window_)) {
                forward_pass_.recreate_pipeline(
                    device_context_.device(),
                    renderer_.render_pass()
                );
                imgui_layer_.recreate(
                    renderer_.render_pass(),
                    renderer_.image_count()
                );
            }
            previous_time = glfwGetTime();
            frame_rate_counter_.reset();
            continue;
        }

        update(delta_time);
        imgui_layer_.prepare_frame(
            fps_enabled_,
            frame_rate_counter_.average_fps()
        );

        try {
            renderer_.render(
                device_context_.device(),
                [this](const RenderFrameContext& frame) {
                    const auto shadow_output = shadow_pass_.prepare(
                        frame.frame_index,
                        scene_
                    );

                    auto shadow_future = thread_pool_.run([this, &frame, &shadow_output] {
                        frame.record(
                            shadow_recording_slot,
                            [this, &frame, &shadow_output](
                                vk::raii::CommandBuffer& command_buffer
                            ) {
                                shadow_pass_.record(
                                    ShadowPass::ExecutionContext{
                                        .command_buffer = command_buffer,
                                        .frame_index = frame.frame_index
                                    },
                                    ShadowPass::Input{
                                        .scene = scene_,
                                        .registry = registry_
                                    },
                                    shadow_output
                                );
                            }
                        );
                    });

                    auto forward_future = thread_pool_.run([this, &frame, &shadow_output] {
                        frame.record(
                            forward_recording_slot,
                            [this, &frame, &shadow_output](
                                vk::raii::CommandBuffer& command_buffer
                            ) {
                                forward_pass_.record(
                                    ForwardPass::ExecutionContext{
                                        .command_buffer = command_buffer,
                                        .frame_index = frame.frame_index
                                    },
                                    ForwardPass::Input{
                                        .scene = scene_,
                                        .registry = registry_,
                                        .shadow = shadow_output
                                    },
                                    ForwardPass::Output{
                                        .render_pass = frame.render_pass,
                                        .framebuffer = frame.framebuffer,
                                        .extent = frame.extent
                                    },
                                    [this](vk::raii::CommandBuffer& command_buffer) {
                                        imgui_layer_.record(command_buffer);
                                    }
                                );
                            }
                        );
                    });

                    shadow_future.wait();
                    forward_future.wait();
                    shadow_future.get();
                    forward_future.get();
                }
            );

            if (fps_enabled_) {
                frame_rate_counter_.end_frame();
            }
        }
        catch (const vk::OutOfDateKHRError&) {
            if (renderer_.recreate_swapchain(device_context_, window_)) {
                forward_pass_.recreate_pipeline(
                    device_context_.device(),
                    renderer_.render_pass()
                );
                imgui_layer_.recreate(
                    renderer_.render_pass(),
                    renderer_.image_count()
                );
            }
            previous_time = glfwGetTime();
            frame_rate_counter_.reset();
        }
    }
    renderer_.wait_idle(device_context_.device());
}
