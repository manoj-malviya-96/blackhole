#include "shader.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <filesystem>
#include <fstream>

// Helper function to create temporary shader files for testing
void createTempShaderFiles(const std::string& vertexPath, const std::string& fragmentPath) {
    // Basic vertex shader
    std::ofstream vertFile(vertexPath);
    vertFile << "#version 330 core\n"
             << "layout (location = 0) in vec3 aPos;\n"
             << "uniform mat4 model;\n"
             << "uniform mat4 view;\n"
             << "uniform mat4 projection;\n"
             << "void main() {\n"
             << "    gl_Position = projection * view * model * vec4(aPos, 1.0);\n"
             << "}\n";
    vertFile.close();

    // Basic fragment shader
    std::ofstream fragFile(fragmentPath);
    fragFile << "#version 330 core\n"
             << "out vec4 FragColor;\n"
             << "uniform vec3 objectColor;\n"
             << "void main() {\n"
             << "    FragColor = vec4(objectColor, 1.0);\n"
             << "}\n";
    fragFile.close();
}

// Helper function to clean up temporary files
void removeTempShaderFiles(const std::string& vertexPath, const std::string& fragmentPath) {
    std::filesystem::remove(vertexPath);
    std::filesystem::remove(fragmentPath);
}

// Mock for OpenGL functions as we can't test them directly in this environment
// In a real-world scenario, you might use a library like FakeIt or a more robust mocking solution
namespace GLMock {
unsigned int nextID = 1;
bool compileSuccess = true;
bool linkSuccess = true;

unsigned int createShader(unsigned int) { return nextID++; }

void shaderSource(unsigned int, int, const char**, void*) {}

void compileShader(unsigned int) {}

void getShaderiv(unsigned int, unsigned int, int* value) { *value = compileSuccess ? 1 : 0; }

void getShaderInfoLog(unsigned int, int, int*, char*) {}

unsigned int createProgram() { return nextID++; }

void attachShader(unsigned int, unsigned int) {}

void linkProgram(unsigned int) {}

void getProgramiv(unsigned int, unsigned int, int* value) { *value = linkSuccess ? 1 : 0; }

void getProgramInfoLog(unsigned int, int, int*, char*) {}

void deleteShader(unsigned int) {}

int getUniformLocation(unsigned int, const char*) {
    return 0; // Return a valid location for testing
}

void useProgram(unsigned int) {}

void uniform1i(int, int) {}
void uniform1f(int, float) {}
void uniform2fv(int, int, const float*) {}
void uniform3fv(int, int, const float*) {}
void uniform4fv(int, int, const float*) {}
void uniformMatrix4fv(int, int, unsigned char, const float*) {}

void deleteProgram(unsigned int) {}
} // namespace GLMock

// Replace OpenGL functions with our mock implementations
#define glCreateShader GLMock::createShader
#define glShaderSource GLMock::shaderSource
#define glCompileShader GLMock::compileShader
#define glGetShaderiv GLMock::getShaderiv
#define glGetShaderInfoLog GLMock::getShaderInfoLog
#define glCreateProgram GLMock::createProgram
#define glAttachShader GLMock::attachShader
#define glLinkProgram GLMock::linkProgram
#define glGetProgramiv GLMock::getProgramiv
#define glGetProgramInfoLog GLMock::getProgramInfoLog
#define glDeleteShader GLMock::deleteShader
#define glGetUniformLocation GLMock::getUniformLocation
#define glUseProgram GLMock::useProgram
#define glUniform1i GLMock::uniform1i
#define glUniform1f GLMock::uniform1f
#define glUniform2fv GLMock::uniform2fv
#define glUniform3fv GLMock::uniform3fv
#define glUniform4fv GLMock::uniform4fv
#define glUniformMatrix4fv GLMock::uniformMatrix4fv
#define glDeleteProgram GLMock::deleteProgram

// GL constants needed by the shader class
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FALSE 0

TEST_CASE("Shader constructor and basic operations", "[shader]") {
    // Setup test files
    std::string vertexPath = "test_vertex.glsl";
    std::string fragmentPath = "test_fragment.glsl";
    createTempShaderFiles(vertexPath, fragmentPath);

    SECTION("Constructor should create valid shader program") {
        GLMock::compileSuccess = true;
        GLMock::linkSuccess = true;

        BlackHole::Shader shader(vertexPath.c_str(), fragmentPath.c_str());
        REQUIRE(shader.ID > 0);

        // Test use function
        REQUIRE_NOTHROW(shader.use());
    }

    SECTION("Setting uniform values") {
        GLMock::compileSuccess = true;
        GLMock::linkSuccess = true;

        BlackHole::Shader shader(vertexPath.c_str(), fragmentPath.c_str());

        // Test all setter functions
        REQUIRE_NOTHROW(shader.setBool("testBool", true));
        REQUIRE_NOTHROW(shader.setInt("testInt", 42));
        REQUIRE_NOTHROW(shader.setFloat("testFloat", 3.14f));
        REQUIRE_NOTHROW(shader.setVec2("testVec2", glm::vec2(1.0f, 2.0f)));
        REQUIRE_NOTHROW(shader.setVec3("testVec3", glm::vec3(1.0f, 2.0f, 3.0f)));
        REQUIRE_NOTHROW(shader.setVec4("testVec4", glm::vec4(1.0f, 2.0f, 3.0f, 4.0f)));
        REQUIRE_NOTHROW(shader.setMat4("testMat4", glm::mat4(1.0f)));
    }

    SECTION("Move constructor and assignment") {
        GLMock::compileSuccess = true;
        GLMock::linkSuccess = true;

        BlackHole::Shader shader1(vertexPath.c_str(), fragmentPath.c_str());
        unsigned int originalID = shader1.ID;

        // Test move constructor
        BlackHole::Shader shader2(std::move(shader1));
        REQUIRE(shader2.ID == originalID);
        REQUIRE(shader1.ID == 0); // Original should be "empty" after move

        // Test move assignment
        BlackHole::Shader shader3(vertexPath.c_str(), fragmentPath.c_str());
        unsigned int thirdID = shader3.ID;
        shader3 = std::move(shader2);
        REQUIRE(shader3.ID == originalID);
        REQUIRE(shader2.ID == 0);
    }

    // Clean up
    removeTempShaderFiles(vertexPath, fragmentPath);
}
