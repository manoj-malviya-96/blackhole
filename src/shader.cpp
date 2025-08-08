#include "shader.h"
#include <GL/glew.h>
#include <fstream>
#include <sstream>
#include "logger/logger.h"

namespace BlackHole {

Shader::Shader(const char* vertexPath, const char* fragmentPath) : ID(0) {
    // 1. Retrieve the vertex/fragment source code from filePath
    std::string vertexCode;
    std::string fragmentCode;
    std::ifstream vShaderFile;
    std::ifstream fShaderFile;

    // Ensure objects can throw exceptions:
    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
        vShaderFile.open(vertexPath);
        fShaderFile.open(fragmentPath);
        std::stringstream vShaderStream, fShaderStream;
        vShaderStream << vShaderFile.rdbuf();
        fShaderStream << fShaderFile.rdbuf();
        vShaderFile.close();
        fShaderFile.close();
        vertexCode = vShaderStream.str();
        fragmentCode = fShaderStream.str();
    } catch (std::ifstream::failure& e) {
        LOGE() << "SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what();
    }
    const char* vShaderCode = vertexCode.c_str();
    const char* fShaderCode = fragmentCode.c_str();

    // 2. Compile shaders
    unsigned int vertex, fragment;

    vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, NULL);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");

    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, NULL);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");

    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

Shader::~Shader() {
    if (ID != 0) {
        glDeleteProgram(ID);
    }
}

Shader::Shader(Shader&& other) noexcept
    : ID(other.ID)
    , m_UniformLocationCache(std::move(other.m_UniformLocationCache)) {
    other.ID = 0; // Use 0 to indicate a moved-from object
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (ID != 0) {
            glDeleteProgram(ID);
        }
        ID = other.ID;
        m_UniformLocationCache = std::move(other.m_UniformLocationCache);
        other.ID = 0;
    }
    return *this;
}


void Shader::use() const { glUseProgram(ID); }

void Shader::setBool(const std::string& name, bool value) const { glUniform1i(getUniformLocation(name), (int)value); }

void Shader::setInt(const std::string& name, int value) const { glUniform1i(getUniformLocation(name), value); }

void Shader::setFloat(const std::string& name, float value) const { glUniform1f(getUniformLocation(name), value); }

void Shader::setVec2(const std::string& name, const glm::vec2& value) const {
    glUniform2fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
    glUniform3fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setVec4(const std::string& name, const glm::vec4& value) const {
    glUniform4fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}


int Shader::getUniformLocation(const std::string& name) const {
    if (m_UniformLocationCache.count(name)) {
        return m_UniformLocationCache[name];
    }

    int location = glGetUniformLocation(ID, name.c_str());
    if (location == -1) {
        // We comment this out to avoid spamming the console, but it's useful for debugging
        // LOGW() << "Warning: uniform '" << name << "' doesn't exist!";
    }
    m_UniformLocationCache[name] = location;
    return location;
}


void Shader::checkCompileErrors(unsigned int shader, std::string type) {
    int success;
    char infoLog[1024];
    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            LOGE() << "SHADER_COMPILATION_ERROR of type: " << type << "\n"
                   << infoLog << "\n -- --------------------------------------------------- -- ";
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            LOGE() << "PROGRAM_LINKING_ERROR of type: " << type << "\n"
                   << infoLog << "\n -- --------------------------------------------------- -- ";
        }
    }
}

} // namespace BlackHole
