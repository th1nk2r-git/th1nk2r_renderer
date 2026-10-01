set_project("th1nk2r_renderer")
set_arch("x64")
set_languages("c++20")
set_toolchains("msvc")

add_rules("mode.debug", "mode.release")
set_policy("run.autobuild", true)

add_requires(
    "vulkansdk",
    "glfw",
    "glm",
    "vulkan-memory-allocator",
    "stb 2026.03.18",
    "assimp 6.0.4",
    "cmake"
)

includes("xmake/rules/nrd.lua")
includes("xmake/rules/shader.lua")

target("th1nk2r_renderer")
    set_kind("binary")
    set_targetdir("bin")
    add_rules("nrd.sdk")
    add_defines(
        "VULKAN_HPP_NO_STRUCT_CONSTRUCTORS",
        "GLM_FORCE_RADIANS",
        "GLM_FORCE_DEPTH_ZERO_TO_ONE"
    )
    add_files("src/**.cpp")
    add_files("shaders/**.slang", {rule = "shader.spirv"})
    add_includedirs("./include")
    add_packages(
        "vulkansdk",
        "glfw",
        "glm",
        "vulkan-memory-allocator",
        "stb",
        "assimp",
        "cmake"
    )

    after_build(function (target)
        local asset_dir = path.join(os.projectdir(), "assets")
        if os.isdir(asset_dir) then
            os.cp(asset_dir, target:targetdir())
        end
    end)
