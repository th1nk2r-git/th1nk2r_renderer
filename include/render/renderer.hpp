#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <memory>
#include <span>

#include "resource/gpu/resource_id.hpp"

class DeviceContext;
class Material;
class ResourceRegistry;
class Scene;
class Window;
template <int size>
class ThreadPool;
struct HdrImageData;

class Renderer {
public:
    enum class FrameResult {
        Rendered,
        Skipped
    };

    // Referenced dependencies must outlive the renderer.
    Renderer(
        DeviceContext& device_context,
        Window& window,
        const ResourceRegistry& registry,
        ThreadPool<8>& thread_pool
    );
    ~Renderer() noexcept;

    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;
    Renderer(Renderer&&) = delete;
    auto operator=(Renderer&&) -> Renderer& = delete;

    // Complete queued uploads and initialize material bindings for all passes.
    auto prepare_resources(std::span<const ResourceId<Material>> material_ids) -> void;

    auto set_environment(const HdrImageData& panorama) -> void;

    // Window changes and presentation recovery are handled internally.
    auto render(const Scene& scene) -> FrameResult;
    auto wait_idle() const -> void;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
