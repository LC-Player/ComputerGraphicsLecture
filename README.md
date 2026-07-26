# ComputerGraphicsLecture

Computer Graphics lab projects — Vulkan 1.4 + C++20 + Slang Shaders

## Build

### Prerequisites

- Visual Studio 26 with MSVC C++ toolchain
- [Vulkan SDK](https://vulkan.lunarg.com/) (developed on 1.4.350.0)
  - `Bin` directory in PATH
  - `VULKAN_SDK` environment variable set
- CMake ≥ 3.25, Ninja, vcpkg (all integrated in VS26)

### Build Steps

1. Open the repository root in Visual Studio — CMakePresets will be picked up automatically
2. Select a target in Solution Explorer and build

Shaders are authored in Slang and compiled to SPIR-V automatically by the CMake build scripts via `slangc`.

## Subprojects

| Project | Lab | Description |
|---------|-----|-------------|
| **VkFromScratch** | Lab 1, 2 | Vulkan scaffold: triangle/quad rendering, instanced draw, ImGui debug panel |
| **BlinnPhong** | Lab 3 | Rasterization-based Blinn-Phong multi-light (point, spot, directional) + ambient |
| **RayTracing** | Final PJ (core) | Software ray tracing on Compute Shader: SAH BVH, PBR-inspired shading, recursive reflection/refraction |
| **PathTracing** | Final PJ (exploration) | Hardware path tracing via `VK_KHR_ray_tracing_pipeline`: Monte Carlo sampling, progressive accumulation |

## Directory Layout

```
.
├── CMakeLists.txt
├── CMakePresets.json.example
├── vcpkg.json
├── imgui/                         # Dear ImGui library source
├── docs/images/                   # Report screenshots
├── RayTracing/src/
│   ├── core/                      # Logger, exception, Vulkan macros
│   ├── utils/                     # File I/O
│   ├── window/                    # GLFW window management
│   └── vulkan/                    # Vulkan abstractions (instance, device, buffer, texture, pipeline, etc.)
├── PathTracing/src/               # Shares core/utils/window/vulkan modules with RayTracing
│   └── vulkan/                    # Also includes AccelerationStructure (TLAS/BLAS)
├── BlinnPhong/src/                # Shares core/utils/window/vulkan modules
│   ├── Model.*, Instance.*        # Model loading & instanced rendering
│   └── Application.*              # Rasterization pipeline
└── VkFromScratch/                 # Standalone scaffold
    ├── main.cpp, Application.*    # App init & main loop
    ├── Pipeline.*, Resources.*    # Pipeline creation, buffers & descriptors
    ├── Rendering.*                # Command recording & frame loop
    └── Camera.*
```

The `core/`, `utils/`, `window/`, `vulkan/` modules are shared across RayTracing, PathTracing, and BlinnPhong via per-project `target_sources` in CMake (not extracted as a library), so each project can add or remove sources freely.

## Development Notes

- C++20 standard enforced across all targets
- Code should leverage `vk::raii` — RAII wrappers for Vulkan handles that manage lifetime automatically. Avoid manual `vkDestroy*` / `vkFree*` calls.
- Dependencies managed by vcpkg (see `vcpkg.json`)
