local nrd_shaders = nrd_shader_include_dir()

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
            "-I", nrd_shaders,
            "-o", output_file,
            sourcefile
        })
        batchcmds:add_depfiles(sourcefile)
        if path.basename(sourcefile):find("restir_di", 1, true) or path.basename(sourcefile) == "direct_light_fragment" then
            batchcmds:add_depfiles("shaders/common/restir_di.slangh")
        end
        if path.basename(sourcefile) == "direct_light_fragment" then
            batchcmds:add_depfiles("shaders/common/direct_light_nrd.slangh")
        end
        if path.basename(sourcefile) == "direct_light_composite_fragment" then
            batchcmds:add_depfiles("shaders/common/restir_di.slangh")
            batchcmds:add_depfiles("shaders/common/direct_light_nrd.slangh")
        end
        if path.basename(sourcefile) == "direct_light_denoise" then
            batchcmds:add_depfiles(path.join(nrd_shaders, "NRD.hlsli"))
            batchcmds:add_depfiles(path.join(nrd_shaders, "NRDConfig.hlsli"))
        end
        batchcmds:set_depmtime(os.mtime(output_file))
        batchcmds:set_depcache(target:dependfile(output_file))
    end)
