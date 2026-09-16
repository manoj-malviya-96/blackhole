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
    QOpenGLShaderProgram m_gridProg;
    QOpenGLShaderProgram m_quadProg;
    QOpenGLShaderProgram m_computeProg;
    QOpenGLShaderProgram m_lensProg; // fallback fragment shader

    // UBOs
    GLuint m_cameraUBO = 0;
    GLuint m_diskUBO = 0;
    GLuint m_objectsUBO = 0;

    // Fullscreen quad + texture
    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;
    GLuint m_quadEBO = 0;
    GLuint m_outputTex = 0;

    // Grid
    GLuint m_gridVAO = 0;
    GLuint m_gridVBO = 0;
    GLuint m_gridEBO = 0;
    int m_gridIndexCount = 0;
    uint64_t m_lastGridVersion = 0;

    // Matrices (recomputed each frame in render())
    QMatrix4x4 m_view;
    QMatrix4x4 m_proj;
    QMatrix4x4 m_viewProj;
    QVector3D m_eye;

    // Params
    static constexpr int kComputeW = 200;
    static constexpr int kComputeH = 150;
    static constexpr int kHiComputeW = 640;
    static constexpr int kHiComputeH = 360;

    // QMatrix4x4's sizeof() is larger than 64 (it carries an internal flagBits
    // optimization flag alongside the 16 floats), so it must never be used to
    // size or step through a std140 mat4 in a UBO - use the GLSL mat4 size instead.
    static constexpr size_t kMat4Bytes = 16 * sizeof(float);

    bool m_useCompute = true;
};

} // namespace renderer
