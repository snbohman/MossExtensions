-- Used for Github Actions unit testing
local is_standalone = _ACTION and not _G.__root_workspace_defined

if is_standalone then
    _G.__root_workspace_defined = true

    workspace("MossDivided")
        language "C++"
        -- cppdialect "C++20"
        architecture "x86_64"
        toolset "clang"

        buildoptions { "-std=c++20", "-Wno-nullability-completeness" } 

        location "scripts"
        flags { "MultiProcessorCompile" }
        configurations { "debug", "release" }
        linkoptions { "-fuse-ld=llvm-ar" } 

        startproject("mossExtensions") -- Helpful in IDE
end

project "mossExtensions"
    kind "StaticLib"
    location "scripts"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "build/%{cfg.buildcfg}/%{prj.name}"

    files { "src/**.cpp" }
    includedirs {
        "include",
        "external",
        "../core/include",
        "$(VULKAN_SDK)/Include",
        "$(GLFW_ROOT)/Include"
    }

    libdirs {
        "external",
        "$(VULKAN_SDK)/Lib",
        "$(GLFW_ROOT)/lib-vc2022"
    }

    links {
        -- "raylib",
        "vulkan-1",
        "glfw3"
        -- "vk-bootstrap"
    }

    filter "configurations:debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:release"
        defines { "NDEBUG" }
        optimize "On"
