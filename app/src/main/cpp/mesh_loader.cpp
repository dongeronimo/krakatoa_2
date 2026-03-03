#include "mesh_loader.h"
#include "asset_loader.h"
#include "android_log.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cassert>
using namespace io;

MeshData MeshLoader::Load(const std::string& assetPath) {
    MeshData result;

    // Load raw bytes from APK assets
    std::vector<uint8_t> fileData = AssetLoader::loadFile(assetPath);
    if (fileData.empty()) {
        LOGE("MeshLoader: failed to load asset '%s'", assetPath.c_str());
        return result;
    }

    // Determine file extension hint for Assimp
    std::string hint;
    auto dotPos = assetPath.rfind('.');
    if (dotPos != std::string::npos) {
        hint = assetPath.substr(dotPos + 1);
    }

    // Parse with Assimp from memory
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(
            fileData.data(),
            fileData.size(),
            aiProcess_Triangulate | aiProcess_FlipUVs,
            hint.c_str());

    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        LOGE("MeshLoader: Assimp failed to parse '%s': %s",
             assetPath.c_str(), importer.GetErrorString());
        return result;
    }

    if (scene->mNumMeshes == 0) {
        LOGE("MeshLoader: no meshes found in '%s'", assetPath.c_str());
        return result;
    }

    // Take the first mesh (one file = one mesh for now)
    const aiMesh* mesh = scene->mMeshes[0];
    if (scene->mNumMeshes > 1) {
        LOGW("MeshLoader: '%s' has %u meshes, using first only", assetPath.c_str(), scene->mNumMeshes);
    }

    // Build interleaved vertex data: px py pz nx ny nz u v
    const uint32_t floatsPerVertex = 8;
    result.vertexCount = mesh->mNumVertices;
    result.vertices.resize(result.vertexCount * floatsPerVertex);

    for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
        uint32_t base = i * floatsPerVertex;
        // Position
        result.vertices[base + 0] = mesh->mVertices[i].x;
        result.vertices[base + 1] = mesh->mVertices[i].y;
        result.vertices[base + 2] = mesh->mVertices[i].z;
        // Normal
        result.vertices[base + 3] = mesh->mNormals[i].x;
        result.vertices[base + 4] = mesh->mNormals[i].y;
        result.vertices[base + 5] = mesh->mNormals[i].z;
        // UV (first texture coordinate set)
        if (mesh->mTextureCoords[0]) {
            result.vertices[base + 6] = mesh->mTextureCoords[0][i].x;
            result.vertices[base + 7] = mesh->mTextureCoords[0][i].y;
        } else {
            result.vertices[base + 6] = 0.0f;
            result.vertices[base + 7] = 0.0f;
        }
    }

    // Build index data from faces (all triangulated by aiProcess_Triangulate)
    result.indexCount = mesh->mNumFaces * 3;
    result.indices.resize(result.indexCount);
    uint32_t idx = 0;
    for (uint32_t f = 0; f < mesh->mNumFaces; f++) {
        const aiFace& face = mesh->mFaces[f];
        assert(face.mNumIndices == 3);
        result.indices[idx++] = face.mIndices[0];
        result.indices[idx++] = face.mIndices[1];
        result.indices[idx++] = face.mIndices[2];
    }

    LOGI("MeshLoader: loaded '%s' - %u vertices, %u indices",
         assetPath.c_str(), result.vertexCount, result.indexCount);

    return result;
}

MeshData MeshLoader::CreateFullscreenQuad() {
    MeshData result;

    // 4 vertices covering NDC [-1,1] x [-1,1], Z=0
    // Vertex format: px py pz  nx ny nz  u v
    //
    //  V2 (-1,-1)----V3 (+1,-1)     UV (0,0)----(1,0)   ← top of screen
    //     |  \           |               |  \        |
    //     |    \         |               |    \      |
    //     |      \       |               |      \    |
    //  V0 (-1,+1)----V1 (+1,+1)     UV (0,1)----(1,1)   ← bottom of screen
    //
    // Vulkan NDC: Y=-1 is top, Y=+1 is bottom. UV (0,0) = top-left of texture.
    result.vertexCount = 4;
    result.vertices = {
        // V0: bottom-left (Vulkan NDC)
        -1.0f,  1.0f, 0.0f,   0.0f, 0.0f, -1.0f,   0.0f, 1.0f,
        // V1: bottom-right
         1.0f,  1.0f, 0.0f,   0.0f, 0.0f, -1.0f,   1.0f, 1.0f,
        // V2: top-left
        -1.0f, -1.0f, 0.0f,   0.0f, 0.0f, -1.0f,   0.0f, 0.0f,
        // V3: top-right
         1.0f, -1.0f, 0.0f,   0.0f, 0.0f, -1.0f,   1.0f, 0.0f,
    };

    // Two CCW triangles: V0-V2-V1, V1-V2-V3
    result.indexCount = 6;
    result.indices = { 0, 2, 1,  1, 2, 3 };

    LOGI("MeshLoader: created fullscreen quad - 4 vertices, 6 indices");
    return result;
}

std::shared_ptr<MeshData> io::GenerateARPlaneMesh(const float* polygonXZ,
                                            int floatCount,
                                            float uvScale) {
    auto result = std::make_shared<MeshData>();
    int vertexCount = floatCount / 2;
    if (vertexCount < 3) return result;
    // Compute centroid of the polygon
    float cx = 0.0f, cz = 0.0f;
    for (int i = 0; i < vertexCount; i++) {
        cx += polygonXZ[i * 2];
        cz += polygonXZ[i * 2 + 1];
    }
    cx /= static_cast<float>(vertexCount);
    cz /= static_cast<float>(vertexCount);
    // Normal pointing up in plane-local space (the plane lies on XZ, Y=0)
    const float nx = 0.0f, ny = 1.0f, nz = 0.0f;
    // Vertex 0: centroid
    result->vertices.push_back(cx);
    result->vertices.push_back(0.0f);
    result->vertices.push_back(cz);
    result->vertices.push_back(nx);
    result->vertices.push_back(ny);
    result->vertices.push_back(nz);
    result->vertices.push_back(cx * uvScale);
    result->vertices.push_back(cz * uvScale);
    // Vertices 1..vertexCount: polygon boundary points
    for (int i = 0; i < vertexCount; i++) {
        float x = polygonXZ[i * 2];
        float z = polygonXZ[i * 2 + 1];
        result->vertices.push_back(x);
        result->vertices.push_back(0.0f);
        result->vertices.push_back(z);
        result->vertices.push_back(nx);
        result->vertices.push_back(ny);
        result->vertices.push_back(nz);
        result->vertices.push_back(x * uvScale);
        result->vertices.push_back(z * uvScale);
    }
    // Fan triangles from centroid to each edge.
    // ARCore polygon vertices are CCW from above.  The projection matrix
    // has its Y negated for Vulkan NDC, which preserves the screen-space
    // winding, so the original CCW order [centroid, current, next] is
    // front-facing with VK_FRONT_FACE_COUNTER_CLOCKWISE.
    for (int i = 0; i < vertexCount; i++) {
        result->indices.push_back(0);                              // centroid
        result->indices.push_back(static_cast<uint32_t>(i + 1));   // current
        result->indices.push_back(static_cast<uint32_t>((i + 1) % vertexCount + 1)); // next (wraps)
    }
    result->vertexCount = static_cast<uint32_t>(result->vertices.size() / 8); // 8 floats per vertex
    result->indexCount  = static_cast<uint32_t>(result->indices.size());
    return result;
}


