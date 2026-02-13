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
