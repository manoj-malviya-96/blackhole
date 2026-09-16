#include "renderer.h"
#include <QOpenGLContext>
#include <QDebug>
#include <cmath>

// Guard for headers that don't define this (e.g., macOS < 4.2 headers)
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif

void Renderer::initialize() {
    initializeOpenGLFunctions();

    // Detect compute support: GL 4.3 or ARB_compute_shader
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    const auto fmt = ctx->format();
    const bool verOK = (fmt.majorVersion() > 4) || (fmt.majorVersion() == 4 && fmt.minorVersion() >= 3);
    const bool hasARB = ctx->hasExtension(QByteArrayLiteral("GL_ARB_compute_shader"));
    useCompute_ = verOK || hasARB;

    if (!gridProg_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/grid.vert")) qWarning() << gridProg_.log();
    if (!gridProg_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/grid.frag")) qWarning() << gridProg_.log();
    if (!gridProg_.link()) qWarning() << gridProg_.log();

    if (!quadProg_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/quad.vert")) qWarning() << quadProg_.log();
    if (!quadProg_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/quad.frag")) qWarning() << quadProg_.log();
    if (!quadProg_.link()) qWarning() << quadProg_.log();

    if (useCompute_) {
        if (!computeProg_.addShaderFromSourceFile(QOpenGLShader::Compute, ":/shaders/geodesic.comp")) qWarning() << computeProg_.log();
        if (!computeProg_.link()) qWarning() << computeProg_.log();
    } else {
        if (!lensProg_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/quad.vert")) qWarning() << lensProg_.log();
        if (!lensProg_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/lens.frag")) qWarning() << lensProg_.log();
        if (!lensProg_.link()) qWarning() << lensProg_.log();
        bindUniformBlocks(lensProg_);
    }

    glGenBuffers(1, &cameraUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO_);
    glBufferData(GL_UNIFORM_BUFFER, kMat4Bytes * 3 + sizeof(QVector4D), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO_);

    glGenBuffers(1, &diskUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, diskUBO_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO_);

    glGenBuffers(1, &objectsUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO_);
    glBufferData(GL_UNIFORM_BUFFER, 4096, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO_);

    createQuad();
    if (useCompute_) {
        ensureOutputTex(kComputeW, kComputeH);
    }

    clock_.start();

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
    if (gridEBO_) glDeleteBuffers(1, &gridEBO_), gridEBO_ = 0;
    if (gridVBO_) glDeleteBuffers(1, &gridVBO_), gridVBO_ = 0;
    if (gridVAO_) glDeleteVertexArrays(1, &gridVAO_), gridVAO_ = 0;

    if (quadEBO_) glDeleteBuffers(1, &quadEBO_), quadEBO_ = 0;
    if (quadVBO_) glDeleteBuffers(1, &quadVBO_), quadVBO_ = 0;
    if (quadVAO_) glDeleteVertexArrays(1, &quadVAO_), quadVAO_ = 0;

    if (outputTex_) glDeleteTextures(1, &outputTex_), outputTex_ = 0;

    if (cameraUBO_) glDeleteBuffers(1, &cameraUBO_), cameraUBO_ = 0;
    if (diskUBO_) glDeleteBuffers(1, &diskUBO_), diskUBO_ = 0;
    if (objectsUBO_) glDeleteBuffers(1, &objectsUBO_), objectsUBO_ = 0;

    if (gridProg_.isLinked()) gridProg_.removeAllShaders();
    if (quadProg_.isLinked()) quadProg_.removeAllShaders();
    if (computeProg_.isLinked()) computeProg_.removeAllShaders();
    if (lensProg_.isLinked()) lensProg_.removeAllShaders();
}

void Renderer::resize(int w, int h) {
    glViewport(0, 0, w, h);
}

void Renderer::render(const Camera& camera, const std::vector<SceneObject>& objects, int viewportW, int viewportH) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    view_ = camera.viewMatrix();
    const float aspect = viewportW > 0 ? float(viewportW) / float(viewportH > 0 ? viewportH : 1) : 1.0f;
    proj_ = camera.projMatrix(aspect);
    viewProj_ = proj_ * view_;
    eye_ = camera.position();

    uploadCameraUBO();
    uploadDiskUBO(objects);
    uploadObjectsUBO(objects);

    rebuildGrid(objects);
    drawGrid();

    if (useCompute_) {
        const int targetW = camera.moving ? kHiComputeW : kComputeW;
        const int targetH = camera.moving ? kHiComputeH : kComputeH;
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

    glGenVertexArrays(1, &quadVAO_);
    glGenBuffers(1, &quadVBO_);
    glGenBuffers(1, &quadEBO_);

    glBindVertexArray(quadVAO_);

    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void Renderer::ensureOutputTex(int w, int h) {
    if (!outputTex_) glGenTextures(1, &outputTex_);
    glBindTexture(GL_TEXTURE_2D, outputTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::uploadCameraUBO() {
    // layout(std140): we pack view, proj, viewProj, camPos
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO_);
    size_t offset = 0;
    glBufferSubData(GL_UNIFORM_BUFFER, offset, kMat4Bytes, view_.constData()); offset += kMat4Bytes;
    glBufferSubData(GL_UNIFORM_BUFFER, offset, kMat4Bytes, proj_.constData()); offset += kMat4Bytes;
    glBufferSubData(GL_UNIFORM_BUFFER, offset, kMat4Bytes, viewProj_.constData()); offset += kMat4Bytes;
    const QVector4D camPos(eye_.x(), eye_.y(), eye_.z(), 1.0f);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QVector4D), &camPos);
}

void Renderer::uploadDiskUBO(const std::vector<SceneObject>& objects) {
    // r1, r2 from Schwarzschild radius of primary object.
    const double r_s = physics::schwarzschildRadius(objects.front().mass);
    const float r1 = float(2.2 * r_s);
    const float r2 = float(5.2 * r_s);
    const float spin = float(objects.front().spin);
    const float time = float(clock_.elapsed()) / 1000.0f;
    const float data[4] = {r1, r2, spin, time};

    glBindBuffer(GL_UNIFORM_BUFFER, diskUBO_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), data);
}

void Renderer::uploadObjectsUBO(const std::vector<SceneObject>& objects) {
    // Reserved for future use (SSBO preferred); not used by compute shader now.
    glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO_);
    glBufferData(GL_UNIFORM_BUFFER, GLsizeiptr(objects.size() * sizeof(SceneObject)),
                 objects.data(), GL_DYNAMIC_DRAW);
}

void Renderer::rebuildGrid(const std::vector<SceneObject>& objects) {
    // CPU grid generation with Schwarzschild-like warp
    std::vector<QVector3D> vertices;
    std::vector<GLuint> indices;
    vertices.reserve((kGridSize + 1) * (kGridSize + 1));
    indices.reserve(kGridSize * kGridSize * 4);

    for (int z = 0; z <= kGridSize; ++z) {
        for (int x = 0; x <= kGridSize; ++x) {
            const float worldX = (x - kGridSize / 2) * kGridSpacing;
            const float worldZ = (z - kGridSize / 2) * kGridSpacing;
            float y = 0.f;

            for (const auto& obj : objects) {
                const double r_s = physics::schwarzschildRadius(obj.mass);
                const double dx = double(worldX) - double(obj.posRadius.x());
                const double dz = double(worldZ) - double(obj.posRadius.z());
                const double dist = std::sqrt(dx * dx + dz * dz);

                if (dist > r_s) {
                    const double deltaY = 2.0 * std::sqrt(r_s * (dist - r_s));
                    y += float(deltaY) - 3e10f;
                } else {
                    y += 2.0f * float(r_s) - 3e10f;
                }
            }
            vertices.emplace_back(worldX, y, worldZ);
        }
    }

    for (int z = 0; z < kGridSize; ++z) {
        for (int x = 0; x < kGridSize; ++x) {
            const int i = z * (kGridSize + 1) + x;
            indices.push_back(i);
            indices.push_back(i + 1);

            indices.push_back(i);
            indices.push_back(i + kGridSize + 1);
        }
    }

    if (!gridVAO_) glGenVertexArrays(1, &gridVAO_);
    if (!gridVBO_) glGenBuffers(1, &gridVBO_);
    if (!gridEBO_) glGenBuffers(1, &gridEBO_);

    glBindVertexArray(gridVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(QVector3D)),
                 vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(GLuint)),
                 indices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QVector3D), (void*)0);
    glBindVertexArray(0);

    gridIndexCount_ = int(indices.size());
}

void Renderer::dispatchCompute() {
    computeProg_.bind();

    glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO_);

    glBindImageTexture(0, outputTex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    glBindTexture(GL_TEXTURE_2D, outputTex_);
    int w = 0, h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glBindTexture(GL_TEXTURE_2D, 0);

    const int WG = 8;
    const int gx = (w + WG - 1) / WG;
    const int gy = (h + WG - 1) / WG;
    glDispatchCompute(gx, gy, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    computeProg_.release();
}

void Renderer::drawGrid() {
    gridProg_.bind();
    const int loc = gridProg_.uniformLocation("viewProj");
    if (loc >= 0) gridProg_.setUniformValue(loc, viewProj_);

    glBindVertexArray(gridVAO_);
    glDrawElements(GL_LINES, gridIndexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    gridProg_.release();
}

void Renderer::drawFullscreenQuad() {
    quadProg_.bind();
    const int u = quadProg_.uniformLocation("uTex");
    if (u >= 0) quadProg_.setUniformValue(u, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, outputTex_);

    glBindVertexArray(quadVAO_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    quadProg_.release();
}

void Renderer::drawFullscreenLensFallback() {
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO_);

    lensProg_.bind();
    glBindVertexArray(quadVAO_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    lensProg_.release();
}
