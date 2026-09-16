#include "renderer/renderer.h"
#include <QOpenGLContext>
#include <QDebug>

// Guard for headers that don't define this (e.g., macOS < 4.2 headers)
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif

namespace renderer {

void Renderer::initialize() {
    initializeOpenGLFunctions();

    // Detect compute support: GL 4.3 or ARB_compute_shader
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    const auto fmt = ctx->format();
    const bool verOK = (fmt.majorVersion() > 4) || (fmt.majorVersion() == 4 && fmt.minorVersion() >= 3);
    const bool hasARB = ctx->hasExtension(QByteArrayLiteral("GL_ARB_compute_shader"));
    m_useCompute = verOK || hasARB;

    if (!m_gridProg.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/grid.vert")) qWarning() << m_gridProg.log();
    if (!m_gridProg.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/grid.frag")) qWarning() << m_gridProg.log();
    if (!m_gridProg.link()) qWarning() << m_gridProg.log();

    if (!m_quadProg.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/quad.vert")) qWarning() << m_quadProg.log();
    if (!m_quadProg.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/quad.frag")) qWarning() << m_quadProg.log();
    if (!m_quadProg.link()) qWarning() << m_quadProg.log();

    if (m_useCompute) {
        if (!m_computeProg.addShaderFromSourceFile(QOpenGLShader::Compute, ":/shaders/geodesic.comp")) qWarning() << m_computeProg.log();
        if (!m_computeProg.link()) qWarning() << m_computeProg.log();
    } else {
        if (!m_lensProg.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/quad.vert")) qWarning() << m_lensProg.log();
        if (!m_lensProg.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/lens.frag")) qWarning() << m_lensProg.log();
        if (!m_lensProg.link()) qWarning() << m_lensProg.log();
        bindUniformBlocks(m_lensProg);
    }

    glGenBuffers(1, &m_cameraUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_cameraUBO);
    glBufferData(GL_UNIFORM_BUFFER, kMat4Bytes * 3 + sizeof(QVector4D), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_cameraUBO);

    glGenBuffers(1, &m_diskUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_diskUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_diskUBO);

    glGenBuffers(1, &m_objectsUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_objectsUBO);
    glBufferData(GL_UNIFORM_BUFFER, 4096, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_objectsUBO);

    createQuad();
    if (m_useCompute) {
        ensureOutputTex(kComputeW, kComputeH);
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::bindUniformBlocks(QOpenGLShaderProgram& program) {
    // GLSL versions below 420 don't support layout(binding=N) on uniform
    // blocks, so bind them explicitly from the host side instead.
    const GLuint prog = program.programId();
    const GLuint camIdx = glGetUniformBlockIndex(prog, "CameraBlock");
    if (camIdx != GL_INVALID_INDEX) glUniformBlockBinding(prog, camIdx, 1);
    const GLuint diskIdx = glGetUniformBlockIndex(prog, "DiskBlock");
    if (diskIdx != GL_INVALID_INDEX) glUniformBlockBinding(prog, diskIdx, 2);
}

void Renderer::shutdown() {
    if (m_gridEBO) { glDeleteBuffers(1, &m_gridEBO); m_gridEBO = 0; }
    if (m_gridVBO) { glDeleteBuffers(1, &m_gridVBO); m_gridVBO = 0; }
    if (m_gridVAO) { glDeleteVertexArrays(1, &m_gridVAO); m_gridVAO = 0; }

    if (m_quadEBO) { glDeleteBuffers(1, &m_quadEBO); m_quadEBO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }
    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }

    if (m_outputTex) { glDeleteTextures(1, &m_outputTex); m_outputTex = 0; }

    if (m_cameraUBO) { glDeleteBuffers(1, &m_cameraUBO); m_cameraUBO = 0; }
    if (m_diskUBO) { glDeleteBuffers(1, &m_diskUBO); m_diskUBO = 0; }
    if (m_objectsUBO) { glDeleteBuffers(1, &m_objectsUBO); m_objectsUBO = 0; }

    if (m_gridProg.isLinked()) m_gridProg.removeAllShaders();
    if (m_quadProg.isLinked()) m_quadProg.removeAllShaders();
    if (m_computeProg.isLinked()) m_computeProg.removeAllShaders();
    if (m_lensProg.isLinked()) m_lensProg.removeAllShaders();
}

void Renderer::resize(int w, int h) {
    glViewport(0, 0, w, h);
}

void Renderer::render(const Camera& camera, const engine::Engine& engine, int viewportW, int viewportH) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    m_view = camera.viewMatrix();
    const float aspect = viewportW > 0 ? float(viewportW) / float(viewportH > 0 ? viewportH : 1) : 1.0f;
    m_proj = camera.projMatrix(aspect);
    m_viewProj = m_proj * m_view;
    m_eye = camera.position();

    uploadCameraUBO();
    uploadDiskUBO(engine.diskParams(), static_cast<float>(engine.time()));
    uploadObjectsUBO(engine.objects());

    if (engine.gridVersion() != m_lastGridVersion || m_gridIndexCount == 0) {
        updateGridMesh(engine.gridMesh());
        m_lastGridVersion = engine.gridVersion();
    }
    drawGrid();

    if (m_useCompute) {
        const int targetW = camera.m_moving ? kHiComputeW : kComputeW;
        const int targetH = camera.m_moving ? kHiComputeH : kComputeH;
        ensureOutputTex(targetW, targetH);

        dispatchCompute();
        drawFullscreenQuad();
    } else {
        drawFullscreenLensFallback();
    }
}

void Renderer::createQuad() {
    const float verts[] = {
        // pos      // uv
        -1.f, -1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 0.f,
         1.f,  1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 1.f
    };
    const GLuint idx[] = {0, 1, 2, 0, 2, 3};

    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glGenBuffers(1, &m_quadEBO);

    glBindVertexArray(m_quadVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
}

void Renderer::ensureOutputTex(int w, int h) {
    if (!m_outputTex) glGenTextures(1, &m_outputTex);
    glBindTexture(GL_TEXTURE_2D, m_outputTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::uploadCameraUBO() {
    // layout(std140): we pack view, proj, viewProj, camPos
    glBindBuffer(GL_UNIFORM_BUFFER, m_cameraUBO);
    size_t offset = 0;
    glBufferSubData(GL_UNIFORM_BUFFER, offset, kMat4Bytes, m_view.constData()); offset += kMat4Bytes;
    glBufferSubData(GL_UNIFORM_BUFFER, offset, kMat4Bytes, m_proj.constData()); offset += kMat4Bytes;
    glBufferSubData(GL_UNIFORM_BUFFER, offset, kMat4Bytes, m_viewProj.constData()); offset += kMat4Bytes;
    const QVector4D camPos(m_eye.x(), m_eye.y(), m_eye.z(), 1.0f);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QVector4D), &camPos);
}

void Renderer::uploadDiskUBO(const engine::DiskParams& disk, float time) {
    const float data[4] = {disk.r1, disk.r2, disk.spin, time};
    glBindBuffer(GL_UNIFORM_BUFFER, m_diskUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), data);
}

void Renderer::uploadObjectsUBO(const std::vector<engine::SceneObject>& objects) {
    // Reserved for future use (SSBO preferred); not used by compute shader now.
    glBindBuffer(GL_UNIFORM_BUFFER, m_objectsUBO);
    glBufferData(GL_UNIFORM_BUFFER, GLsizeiptr(objects.size() * sizeof(engine::SceneObject)),
                 objects.data(), GL_DYNAMIC_DRAW);
}

void Renderer::updateGridMesh(const engine::GridMesh& mesh) {
    if (mesh.vertices.empty()) return;

    if (!m_gridVAO) glGenVertexArrays(1, &m_gridVAO);
    if (!m_gridVBO) glGenBuffers(1, &m_gridVBO);
    if (!m_gridEBO) glGenBuffers(1, &m_gridEBO);

    glBindVertexArray(m_gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVBO);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.vertices.size() * sizeof(QVector3D)),
                 mesh.vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_gridEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(mesh.indices.size() * sizeof(GLuint)),
                 mesh.indices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QVector3D), nullptr);
    glBindVertexArray(0);

    m_gridIndexCount = int(mesh.indices.size());
}

void Renderer::dispatchCompute() {
    m_computeProg.bind();

    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_cameraUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_diskUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_objectsUBO);

    glBindImageTexture(0, m_outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    glBindTexture(GL_TEXTURE_2D, m_outputTex);
    int w = 0, h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glBindTexture(GL_TEXTURE_2D, 0);

    const int WG = 8;
    const int gx = (w + WG - 1) / WG;
    const int gy = (h + WG - 1) / WG;
    glDispatchCompute(gx, gy, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    m_computeProg.release();
}

void Renderer::drawGrid() {
    m_gridProg.bind();
    const int loc = m_gridProg.uniformLocation("viewProj");
    if (loc >= 0) m_gridProg.setUniformValue(loc, m_viewProj);

    glBindVertexArray(m_gridVAO);
    glDrawElements(GL_LINES, m_gridIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    m_gridProg.release();
}

void Renderer::drawFullscreenQuad() {
    m_quadProg.bind();
    const int u = m_quadProg.uniformLocation("uTex");
    if (u >= 0) m_quadProg.setUniformValue(u, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_outputTex);

    glBindVertexArray(m_quadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    m_quadProg.release();
}

void Renderer::drawFullscreenLensFallback() {
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_cameraUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_diskUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_objectsUBO);

    m_lensProg.bind();
    glBindVertexArray(m_quadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    m_lensProg.release();
}

} // namespace renderer
