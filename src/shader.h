#pragma once
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

namespace BlackHole {


class Shader {
public:
    unsigned int ID;
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void use() const;
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, const glm::vec2& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setVec4(const std::string& name, const glm::vec4& value) const;
    void setMat4(const std::string& name, const glm::mat4& mat) const;

private:
    static void checkCompileErrors(unsigned int shader, std::string type);
    int getUniformLocation(const std::string& name) const;
    mutable std::unordered_map<std::string, int> m_UniformLocationCache;
};
} // namespace BlackHole
