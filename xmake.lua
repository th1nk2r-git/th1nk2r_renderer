set_project("th1nk2r_renderer")
set_arch("x64")
set_languages("c++20")
set_toolchains("msvc")

add_rules("mode.debug", "mode.release")

add_requires(
    "vulkansdk",
    "glfw",
    "glm",
    "vulkan-memory-allocator",
    "stb 2026.03.18",
    "assimp 6.0.4"
)

rule("shader.spirv")
    set_extensions(".slang")

    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        local stage = path.filename(path.directory(sourcefile))
        assert(
            stage == "vertex" or
            stage == "fragment" or
            stage == "compute",
            "shader must be placed in shaders/vertex, shaders/fragment, or shaders/compute"
        )

        local output_file = path.join(
            target:targetdir(),
            "spv",
            path.basename(sourcefile) .. ".spv"
        )
        batchcmds:show_progress(
            opt.progress,
            "${color.build.object}compiling.shader %s",
            sourcefile
        )
        batchcmds:mkdir(path.directory(output_file))
        batchcmds:vrunv("slangc", {
            "-target", "spirv",
            "-stage", stage,
            "-entry", "main",
            "-lang", "slang",
            "-o", output_file,
            sourcefile
        })
        batchcmds:add_depfiles(sourcefile)
        batchcmds:set_depmtime(os.mtime(output_file))
        batchcmds:set_depcache(target:dependfile(output_file))
    end)

target("th1nk2r_renderer")
    set_kind("binary")
    set_targetdir("bin")
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
        "assimp"
    )

    after_build(function (target)
        local asset_dir = path.join(os.projectdir(), "assets")
        if os.isdir(asset_dir) then
            os.cp(asset_dir, target:targetdir())
        end
    end)
