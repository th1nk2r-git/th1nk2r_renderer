local nrd_source = path.join(os.projectdir(), "build", "nrd-src")
local nrd_sdk = os.getenv("NRD_SDK_ROOT") or path.join(nrd_source, "_NRD_SDK")
local nri_sdk = os.getenv("NRI_SDK_ROOT") or path.join(nrd_source, "_NRI_SDK")
local nrd_revision = "7033ffbc48dbd74713194555abc13da8d7de4bcb" -- NRD 4.18.0
local nrd_mode = is_mode("debug") and "Debug" or "Release"
local nrd_lib = path.join(nrd_sdk, "Lib", nrd_mode)
local nri_lib = path.join(nri_sdk, "Lib", nrd_mode)

function nrd_shader_include_dir()
    return path.join(nrd_sdk, "Shaders")
end

rule("nrd.sdk")
    on_load(function (target)
        target:add("includedirs", path.join(nrd_sdk, "Include"), path.join(nrd_sdk, "Integration"), path.join(nri_sdk, "Include"))
        target:add("linkdirs", nrd_lib, nri_lib)
        target:add("links", "NRD", "NRI")
    end)

    before_build(function (target)
        local nrd_ready = os.isfile(path.join(nrd_lib, "NRD.lib")) and
                          os.isfile(path.join(nrd_lib, "NRD.dll")) and
                          os.isfile(path.join(nrd_sdk, "Shaders", "NRDConfig.hlsli"))
        local nri_ready = os.isfile(path.join(nri_lib, "NRI.lib")) and
                          os.isfile(path.join(nri_lib, "NRI.dll"))
        if nrd_ready and nri_ready then
            return
        end

        assert(not os.getenv("NRD_SDK_ROOT") and not os.getenv("NRI_SDK_ROOT"),
            "NRD/NRI SDK missing at the configured paths; unset both SDK roots for automatic setup")

        cprint("${yellow}[NRD] %s SDK is missing or incomplete. Preparing NRD/NRI; this may take several minutes.", nrd_mode)
        local function run_step(message, program, args, opt)
            cprint("${cyan}[NRD] %s", message)
            assert(os.execv(program, args, opt) == 0, "NRD setup failed: " .. message)
        end

        if not os.isfile(path.join(nrd_source, "CMakeLists.txt")) then
            os.mkdir(nrd_source)
            run_step("Initializing NRD source directory...", "git", {"init", nrd_source})
            run_step("Fetching NRD 4.18.0 from GitHub (network speed may vary)...", "git", {"-C", nrd_source, "fetch", "--progress", "--depth", "1", "https://github.com/NVIDIA-RTX/NRD.git", nrd_revision})
            run_step("Checking out NRD source...", "git", {"-C", nrd_source, "checkout", "--detach", "FETCH_HEAD"})
        end

        local cmake = path.join(target:pkg("cmake"):installdir(), "bin", "cmake.exe")
        local build_dir = path.join(nrd_source, "_Build")
        local cl_flags = os.getenv("CL") or ""
        if not cl_flags:find("/EHsc", 1, true) then
            os.setenv("CL", cl_flags .. " /EHsc")
        end
        run_step("Configuring NRD/NRI with CMake...", cmake, {
            "-S", nrd_source, "-B", build_dir,
            "-DNRD_NRI=ON",
            "-DNRD_EMBEDS_DXIL_SHADERS=OFF",
            "-DNRD_EMBEDS_DXBC_SHADERS=OFF",
            "-DNRD_SUPPORTS_QUAD_INTRINSICS=OFF",
            "-DNRD_NORMAL_ENCODING=2",
            "-DNRD_ROUGHNESS_ENCODING=1"
        })
        run_step("Building NRD/NRI (" .. nrd_mode .. ")...", cmake, {"--build", build_dir, "--config", nrd_mode, "-j"})

        cprint("${cyan}[NRD] Preparing the NRD/NRI SDK for %s...", nrd_mode)
        local nri_source = path.join(build_dir, "_deps", "nri-src")
        local build_output = path.join(nrd_source, "_Bin", nrd_mode)
        local function copy_sdk(source, destination)
            os.cp(source, destination)
        end

        os.mkdir(path.join(nrd_sdk, "Include"))
        os.mkdir(path.join(nrd_sdk, "Integration"))
        os.mkdir(path.join(nrd_sdk, "Shaders"))
        os.mkdir(nrd_lib)
        os.mkdir(path.join(nri_sdk, "Include"))
        os.mkdir(nri_lib)
        copy_sdk(path.join(nrd_source, "Include", "*"), path.join(nrd_sdk, "Include"))
        copy_sdk(path.join(nrd_source, "Integration", "*"), path.join(nrd_sdk, "Integration"))
        copy_sdk(path.join(nrd_source, "Shaders", "NRD.hlsli"), path.join(nrd_sdk, "Shaders"))
        copy_sdk(path.join(nrd_source, "Shaders", "NRDConfig.hlsli"), path.join(nrd_sdk, "Shaders"))
        copy_sdk(path.join(nrd_source, "LICENSE.txt"), nrd_sdk)
        copy_sdk(path.join(nri_source, "Include", "*"), path.join(nri_sdk, "Include"))
        copy_sdk(path.join(nri_source, "LICENSE.txt"), nri_sdk)
        for _, library in ipairs({"NRD", "NRI"}) do
            local library_dir = library == "NRD" and nrd_lib or nri_lib
            for _, extension in ipairs({"dll", "lib"}) do
                copy_sdk(path.join(build_output, library .. "." .. extension), library_dir)
            end
            local pdb = path.join(build_output, library .. ".pdb")
            if os.isfile(pdb) then
                copy_sdk(pdb, library_dir)
            end
        end

        assert(os.isfile(path.join(nrd_lib, "NRD.lib")) and os.isfile(path.join(nrd_lib, "NRD.dll")),
            "NRD SDK preparation did not produce the selected build configuration")
        assert(os.isfile(path.join(nri_lib, "NRI.lib")) and os.isfile(path.join(nri_lib, "NRI.dll")),
            "NRI SDK preparation did not produce the selected build configuration")
        assert(os.isfile(path.join(nrd_sdk, "Include", "NRD.h")) and
               os.isfile(path.join(nrd_sdk, "Integration", "NRDIntegration.h")) and
               os.isfile(path.join(nrd_sdk, "Shaders", "NRDConfig.hlsli")) and
               os.isfile(path.join(nri_sdk, "Include", "NRI.h")) and
               os.isfile(path.join(nri_sdk, "Include", "Extensions", "NRIDeviceCreation.h")),
            "NRD/NRI SDK preparation did not produce the required headers and shaders")
        cprint("${green}[NRD] SDK ready. Continuing project build.")
    end)

    after_build(function (target)
        os.cp(path.join(nrd_lib, "NRD.dll"), target:targetdir())
        os.cp(path.join(nri_lib, "NRI.dll"), target:targetdir())
    end)
