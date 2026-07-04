set_project("th1nk2r_renderer")
set_arch("x64")
set_languages("c++20")
set_toolchains("msvc")

add_rules("mode.debug", "mode.release")

add_requires("vulkansdk", "glfw", "glm")

target("application")
    set_kind("binary")
    set_targetdir("bin")
    add_files("src/**.cpp")
    add_includedirs("./include")
    add_packages("vulkansdk", "glfw", "glm")

    after_build(function (target)
        local shader_dir = os.projectdir() .. "/shaders"
        local output_dir = os.projectdir() .. "/bin/spv"
        os.mkdir(output_dir)

        local stages = {"vertex", "fragment", "compute"}
        for _, stage in ipairs(stages) do
            local stage_dir = path.join(shader_dir, stage)
            if os.exists(stage_dir) then
                local files = os.files(path.join(stage_dir, "*.hlsl"))
                for _, file in ipairs(files) do
                    local name = path.basename(file)
                    local entry = "main"
                    local profile
                    if stage == "vertex" then
                        profile = "vs_6_0"
                    elseif stage == "fragment" then
                        profile = "ps_6_0"
                    elseif stage == "compute" then
                        profile = "cs_6_0"
                    end
                    local output_file = path.join(output_dir, name .. ".spv")

                    local cmd = string.format("dxc -E %s -T %s -spirv -Fo %s %s", entry, profile, output_file, file)
                    os.run(cmd)
                end
            end
        end
    end)
