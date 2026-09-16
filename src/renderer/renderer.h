#pragma once
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <vector>
#include "engine/engine.h"
#include "renderer/camera.h"

namespace renderer {

// Owns all GL resources (shaders, buffers, textures) and issues draw calls.
// Requires a current QOpenGLContext for initialize()/resize()/render()/shutdown().
class Renderer : protected QOpenGLExtraFunctions {
public:
    void initialize();
    void shutdown();
    void resize(int w, int h);
    void render(const Camera& camera, const engine::Engine& engine, int viewportW, int viewportH);

private:
    void createQuad();
    void ensureOutputTex(int w, int h);
    void updateGridMesh(const engine::GridMesh& mesh);
    void uploadCameraUBO();
    void uploadDiskUBO(const engine::DiskParams& disk, float time);
    void uploadObjectsUBO(const std::vector<engine::SceneObject>& objects);
    void dispatchCompute();
    void drawGrid();
    void drawFullscreenQuad();
    void drawFullscreenLensFallback();
    void bindUniformBlocks(QOpenGLShaderProgram& program);

    // Programs
    QOpenGLShaderProgram gridProg_;
    QOpenGLShaderProgram quadProg_;
    QOpenGLShaderProgram computeProg_;
    QOpenGLShaderProgram lensProg_; // fallback fragment shader

    // UBOs
    GLuint cameraUBO_ = 0;
    GLuint diskUBO_ = 0;
    GLuint objectsUBO_ = 0;

    // Fullscreen quad + texture
    GLuint quadVAO_ = 0;
    GLuint quadVBO_ = 0;
    GLuint quadEBO_ = 0;
    GLuint outputTex_ = 0;

    // Grid
    GLuint gridVAO_ = 0;
    GLuint gridVBO_ = 0;
    GLuint gridEBO_ = 0;
    int gridIndexCount_ = 0;
    uint64_t lastGridVersion_ = 0;

    // Matrices (recomputed each frame in render())
    QMatrix4x4 view_;
    QMatrix4x4 proj_;
    QMatrix4x4 viewProj_;
    QVector3D eye_;

    // Params
    static constexpr int kComputeW = 200;
    static constexpr int kComputeH = 150;
    static constexpr int kHiComputeW = 640;
    static constexpr int kHiComputeH = 360;

    // QMatrix4x4's sizeof() is larger than 64 (it carries an internal flagBits
    // optimization flag alongside the 16 floats), so it must never be used to
    // size or step through a std140 mat4 in a UBO - use the GLSL mat4 size instead.
    static constexpr size_t kMat4Bytes = 16 * sizeof(float);

    bool useCompute_ = true;
};

} // namespace renderer
