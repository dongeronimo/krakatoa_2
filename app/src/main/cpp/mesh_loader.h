#ifndef KRAKATOA_MESH_LOADER_H
#define KRAKATOA_MESH_LOADER_H
#include <vector>
#include <string>
#include <cstdint>
namespace io {
    /**
     * Result of loading a single mesh from a file.
     * Vertex data is interleaved: px py pz nx ny nz u v (8 floats per vertex).
     */
    struct MeshData {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };

    /**
     * Loads mesh data from GLTF files using Assimp.
     *
     * Currently assumes one file = one mesh. The class exists to hold
     * context for future expansion (multiple meshes, skinning, etc.).
     *
     * Usage:
     *   MeshLoader loader;
     *   MeshData data = loader.Load("meshes/cube.gltf");
     */
    class MeshLoader {
    public:
        MeshLoader() = default;
        ~MeshLoader() = default;

        /**
         * Load a mesh from an asset file.
         * @param assetPath Path relative to assets/ folder (e.g. "meshes/cube.gltf")
         * @return MeshData with interleaved vertex data (px py pz nx ny nz u v) and indices.
         *         Empty vectors if loading failed.
         */
        MeshData Load(const std::string& assetPath);

        /**
         * Generate a fullscreen quad (two triangles) in NDC.
         * Positions cover [-1, 1] in XY, Z = 0.
         * UVs go from (0,0) top-left to (1,1) bottom-right
         * (Vulkan convention: Y-down in UV space).
         * Normals point towards the camera (0, 0, -1).
         */
        static MeshData CreateFullscreenQuad();
    };
}
#endif //KRAKATOA_MESH_LOADER_H
