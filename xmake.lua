add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- Zoomidy is a client-side mod: it hooks the renderer, the keyboard and the
-- mouse, none of which exist in a dedicated server. Default accordingly.
option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("client")
option_end()

-- The hooks resolve against the exact game binary, so a build only works
-- against the LeviLamina/Minecraft line it was compiled for. CI builds both.
option("levilamina_version")
    set_default("26.20")
    set_showmenu(true)
    set_values("26.10", "26.20")
option_end()

-- get_config() reads nil for a custom option on xmake's early description-scope
-- passes, before command-line/default values are resolved; guard the
-- concatenation so those passes don't crash. The later, resolved passes are
-- what actually stick for the build.
add_requires("levilamina " .. (get_config("levilamina_version") or "26.20") .. ".x", {configs = {target_type = get_config("target_type")}})
add_requires("levibuildscript")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("zoomidy")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE", "_USE_MATH_DEFINES")
        set_exceptions("none") -- To avoid conflicts with /EHa.
        add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
