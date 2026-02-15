#include "renderable.h"
#include "transform.h"
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <cmath>
#include <stdexcept>
namespace graphics {
    Transform::Transform(Renderable* owner)
            : owner(owner)
    {
    }

    std::vector<Renderable*> Transform::GetChildren() const {
        return children;
    }

    void Transform::SetParent(Renderable* p) {
        parent = p;
        p->GetTransform().children.push_back(owner);
    }

    void Transform::InitFromComponentData(const TransformComponentData& transData) {
        if (transData.hasPosition) {
            position = glm::vec3(transData.position[0], transData.position[1], transData.position[2]);
        }
        if (transData.hasRotation) {
            eulerAngles = glm::vec3(transData.rotation[0], transData.rotation[1], transData.rotation[2]);
            UpdateQuaternionFromEuler();
        }
        if (transData.hasScale) {
            scale = glm::vec3(transData.scale[0], transData.scale[1], transData.scale[2]);
        }
    }

    glm::mat4 Transform::GetWorldMatrix() {
        if (parent == nullptr) {
            worldMatrix = GetLocalMatrix();
            return worldMatrix;
        }

        glm::mat4 parentWorldMatrix = parent->GetTransform().GetWorldMatrix();
        glm::mat4 localMatrix = GetLocalMatrix();
        worldMatrix = parentWorldMatrix * localMatrix;
        return worldMatrix;
    }

    glm::mat4 Transform::GetInverseWorldMatrix() {
        return glm::inverse(worldMatrix);
    }

    glm::mat4 Transform::GetLocalMatrix() const {
        glm::mat4 matrix = glm::mat4(1.0f);

        // GLM: translate * rotate * scale
        matrix = glm::translate(matrix, position);
        matrix = matrix * glm::mat4_cast(rotation);
        matrix = glm::scale(matrix, scale);

        return matrix;
    }

// Position operations
    glm::vec3 Transform::GetPosition() const {
        return position;
    }

    void Transform::SetPosition(const glm::vec3& pos) {
        position = pos;
    }

    void Transform::Translate(const glm::vec3& offset) {
        position += offset;
    }

// Scale operations
    glm::vec3 Transform::GetScale() const {
        return scale;
    }

    void Transform::SetScale(const glm::vec3& s) {
        scale = s;
    }

    void Transform::ScaleBy(const glm::vec3& factor) {
        scale *= factor;
    }

// Rotation operations (Euler angles)
    glm::vec3 Transform::GetEulerAngles() const {
        return eulerAngles;
    }

    void Transform::SetEulerAngles(const glm::vec3& angles) {
        eulerAngles = angles;
        UpdateQuaternionFromEuler();
    }

    void Transform::Rotate(const glm::vec3& deltaAngles) {
        eulerAngles += deltaAngles;
        UpdateQuaternionFromEuler();
    }

// Quaternion operations
    glm::quat Transform::GetRotationQuaternion() const {
        return rotation;
    }

    void Transform::SetRotationQuaternion(const glm::quat& q) {
        rotation = q;
        UpdateEulerFromQuaternion();
    }

    void Transform::LookAt(const glm::vec3& target, const glm::vec3& up) {
        // glm::lookAt creates a view matrix, we need the inverse for object orientation
        glm::mat4 lookAtMatrix = glm::lookAt(position, target, up);

        // Extract rotation from the look-at matrix
        // The look-at matrix is a view matrix, so we need to handle it appropriately
        rotation = glm::quat_cast(glm::transpose(glm::mat3(lookAtMatrix)));
        UpdateEulerFromQuaternion();
    }

    void Transform::UpdateQuaternionFromEuler() {
        // GLM expects angles in radians for quat construction
        glm::vec3 radiansAngles = glm::radians(eulerAngles);

        // Create quaternion from Euler angles (pitch, yaw, roll order - XYZ)
        rotation = glm::quat(radiansAngles);
    }

    void Transform::UpdateEulerFromQuaternion() {
        // Convert quaternion to Euler angles
        glm::vec3 radiansAngles = glm::eulerAngles(rotation);
        eulerAngles = glm::degrees(radiansAngles);
    }

    void Transform::RotateAroundWorldAxis(const glm::vec3& pivot, const glm::vec3& worldAxis, float angleDegrees) {
        float angleRadians = glm::radians(angleDegrees);

        // Create rotation matrix around the world axis
        glm::mat4 rotationMatrix = glm::rotate(glm::mat4(1.0f), angleRadians, worldAxis);

        // Translate position to pivot
        glm::vec3 offsetFromPivot = position - pivot;

        // Rotate the offset
        glm::vec3 rotatedOffset = glm::vec3(rotationMatrix * glm::vec4(offsetFromPivot, 1.0f));

        // Update position
        position = pivot + rotatedOffset;

        // Create quaternion from rotation
        glm::quat rotationQuat = glm::angleAxis(angleRadians, glm::normalize(worldAxis));

        // Apply rotation to object's rotation
        rotation = rotationQuat * rotation;
        UpdateEulerFromQuaternion();
    }

    void Transform::RotateAroundPivotEuler(const glm::vec3& pivot, const glm::vec3& deltaAngles) {
        // Translate to pivot
        position -= pivot;

        // Create rotation matrices from delta angles
        float radX = glm::radians(deltaAngles.x);
        float radY = glm::radians(deltaAngles.y);
        float radZ = glm::radians(deltaAngles.z);

        glm::mat4 rotMatX = glm::rotate(glm::mat4(1.0f), radX, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::mat4 rotMatY = glm::rotate(glm::mat4(1.0f), radY, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 rotMatZ = glm::rotate(glm::mat4(1.0f), radZ, glm::vec3(0.0f, 0.0f, 1.0f));

        // Combine rotations: Z * Y * X (typical order)
        glm::mat4 rotMat = rotMatZ * rotMatY * rotMatX;

        // Rotate position around origin
        position = glm::vec3(rotMat * glm::vec4(position, 1.0f));

        // Translate back from pivot
        position += pivot;

        // Apply rotation to object's own rotation
        glm::quat rotQuat = glm::quat_cast(rotMat);
        rotation = rotQuat * rotation;
        UpdateEulerFromQuaternion();
    }
}