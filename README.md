# Krakatoa

Real-time 3D reconstruction on Android using ARCore depth data, TSDF volumetric fusion (via OpenChisel), and Vulkan rendering.

## Documentation

- [TSDF (Truncated Signed Distance Field)](docs/TSDF.md) - How volumetric fusion works, truncation, mesh extraction
- [OpenChisel Library](docs/OpenChisel.md) - The chunked TSDF library: classes, integration flow, coordinate conventions
- [Architecture](docs/Architecture.md) - App structure, rendering pipeline, data flow, build system

## Building

Requires:
- Android Studio
- Android NDK (C++17)
- Target: API 33+, arm64-v8a
- ARCore-capable device with depth support

Dependencies are fetched automatically via CMake FetchContent (GLM, Eigen, OpenChisel, Assimp, nlohmann/json).
