#ifndef KRAKATOA_TRANSFORM_H
#define KRAKATOA_TRANSFORM_H
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
namespace graphics {
    class Renderable;
    /**Used to init all fields at once*/
    struct TransformComponentData {
        float position[3] = {0.0f, 0.0f, 0.0f};
        float rotation[3] = {0.0f, 0.0f, 0.0f};
        float scale[3] = {1.0f, 1.0f, 1.0f};
        bool hasPosition = false;
        bool hasRotation = false;
        bool hasScale = false;
    };
    /**Transform, with position, rotation, scale and hierarchy.
     * Rotation is stored in quaterions but there's also an euler version.
     * */
    class Transform {
    public:
        explicit Transform(Renderable* owner);

        // Hierarchy
        std::vector<Renderable*> GetChildren() const;
        void SetParent(Renderable* parent);

        // Initialization
        void InitFromComponentData(const TransformComponentData& transData);

        // Matrix operations
        glm::mat4 GetWorldMatrix();
        glm::mat4 GetInverseWorldMatrix();
        glm::mat4 GetLocalMatrix() const;

        // Position operations
        glm::vec3 GetPosition() const;
        void SetPosition(const glm::vec3& pos);
        void Translate(const glm::vec3& offset);

        // Scale operations
        glm::vec3 GetScale() const;
        void SetScale(const glm::vec3& s);
        void ScaleBy(const glm::vec3& factor);

        // Rotation operations (Euler angles in degrees)
        glm::vec3 GetEulerAngles() const;
        void SetEulerAngles(const glm::vec3& angles);
        void Rotate(const glm::vec3& deltaAngles);

        // Quaternion operations
        glm::quat GetRotationQuaternion() const;
        void SetRotationQuaternion(const glm::quat& q);

        // Look at target
        void LookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

        // Rotation around pivot/axis
        void RotateAroundWorldAxis(const glm::vec3& pivot, const glm::vec3& worldAxis, float angleDegrees);
        void RotateAroundPivotEuler(const glm::vec3& pivot, const glm::vec3& deltaAngles);

    private:
        void UpdateQuaternionFromEuler();
        void UpdateEulerFromQuaternion();

        Renderable* owner;
        Renderable* parent = nullptr;
        std::vector<Renderable*> children;

        glm::vec3 position = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion (w, x, y, z)
        glm::vec3 eulerAngles = glm::vec3(0.0f); // Degrees
        glm::vec3 scale = glm::vec3(1.0f);

        glm::mat4 worldMatrix = glm::mat4(1.0f);
    };
}


#endif //KRAKATOA_TRANSFORM_H
