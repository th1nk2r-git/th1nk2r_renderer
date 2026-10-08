local nrd_shaders = nrd_shader_include_dir()

rule("shader.spirv")
    set_extensions(".slang")

    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        local stage = path.filename(path.directory(sourcefile))
        if stage == "ray_tracing" then
            local ray_stages = {
                global_light_raygen = "raygeneration",
                global_light_closest_hit = "closesthit",
                global_light_any_hit = "anyhit",
                global_light_miss = "miss",
                global_light_shadow_miss = "miss"
            }
            stage = ray_stages[path.basename(sourcefile)]
        end
        assert(
            stage == "vertex" or
            stage == "fragment" or
            stage == "compute" or
            stage == "raygeneration" or
            stage == "closesthit" or
            stage == "anyhit" or
            stage == "miss",
            "unsupported shader stage for " .. sourcefile .. ": " .. tostring(stage)
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
            "-I", nrd_shaders,
            "-o", output_file,
            sourcefile
        })
        batchcmds:add_depfiles(sourcefile)
        if path.basename(sourcefile):find("^global_light_") then
            batchcmds:add_depfiles("shaders/ray_tracing/global_light.slangh")
        end
        if path.basename(sourcefile) == "denoise" then
            batchcmds:add_depfiles(path.join(nrd_shaders, "NRD.hlsli"))
            batchcmds:add_depfiles(path.join(nrd_shaders, "NRDConfig.hlsli"))
        end
        batchcmds:set_depmtime(os.mtime(output_file))
        batchcmds:set_depcache(target:dependfile(output_file))
    end)
