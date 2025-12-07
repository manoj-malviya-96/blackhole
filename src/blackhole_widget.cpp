#include "blackhole_widget.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QOpenGLContext>
#include <cmath>

// Guard for headers that don't define this (e.g., macOS < 4.2 headers)
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif

BlackHoleWidget::BlackHoleWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    // Single BH at origin (mass ~ 4.3e6 solar masses)
    const double solar = 1.98847e30;
    objects_.push_back({
        QVector4D(0,0,0, 5e10f),      // pos + radius
        QVector4D(1,1,1,1),           // color
        4.3e6 * solar,                // mass
        QVector3D(0,0,0)              // velocity (unused)
    });

    // Drive frames
    connect(&timer_, &QTimer::timeout, this, [this]() {
        if (!paused_) {
            // future dynamics here (object movement, etc.)
        }
        // request repaint -> paintGL
        update();
    });
    timer_.start(16); // ~60 fps
}

BlackHoleWidget::~BlackHoleWidget() {
    makeCurrent();
    destroyGL();
    doneCurrent();
}

void BlackHoleWidget::initializeGL() {
    initializeOpenGLFunctions();

    // Detect compute support: GL 4.3 or ARB_compute_shader
    QOpenGLContext* ctx = context();
    const auto fmt = ctx->format();
    const bool verOK = (fmt.majorVersion() > 4) || (fmt.majorVersion() == 4 && fmt.minorVersion() >= 3);
    const bool hasARB = ctx->hasExtension(QByteArrayLiteral("GL_ARB_compute_shader"));
    useCompute_ = verOK || hasARB;

    // Compile shaders (from Qt resource)
    if (!gridProg_.addShaderFromSourceFile(QOpenGLShader::Vertex,  ":/shaders/grid.vert"))  qWarning() << gridProg_.log();
    if (!gridProg_.addShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/grid.frag"))  qWarning() << gridProg_.log();
    if (!gridProg_.link()) qWarning() << gridProg_.log();

    if (!quadProg_.addShaderFromSourceFile(QOpenGLShader::Vertex,  ":/shaders/quad.vert"))  qWarning() << quadProg_.log();
    if (!quadProg_.addShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/quad.frag"))  qWarning() << quadProg_.log();
    if (!quadProg_.link()) qWarning() << quadProg_.log();

    if (useCompute_) {
        if (!computeProg_.addShaderFromSourceFile(QOpenGLShader::Compute,":/shaders/geodesic.comp")) qWarning() << computeProg_.log();
        if (!computeProg_.link()) qWarning() << computeProg_.log();
    } else {
        // Fallback lens program (fragment shader does the work)
        if (!lensProg_.addShaderFromSourceFile(QOpenGLShader::Vertex,  ":/shaders/quad.vert"))  qWarning() << lensProg_.log();
        if (!lensProg_.addShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/lens.frag"))  qWarning() << lensProg_.log();
        if (!lensProg_.link()) qWarning() << lensProg_.log();
    }

    // UBOs
    glGenBuffers(1, &cameraUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(QMatrix4x4)*3 + sizeof(QVector4D), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO_);

    glGenBuffers(1, &diskUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, diskUBO_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float)*4, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO_);

    glGenBuffers(1, &objectsUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO_);
    glBufferData(GL_UNIFORM_BUFFER, 4096, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO_);

    // Geometry
    createQuad();
    rebuildGrid();
    if (useCompute_) {
        ensureOutputTex(computeW_, computeH_);
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    updateCameraMatrices();
    uploadCameraUBO();
    uploadDiskUBO();
    uploadObjectsUBO();
}

void BlackHoleWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    updateCameraMatrices();
    uploadCameraUBO();
}

void BlackHoleWidget::paintGL() {
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);

    updateCameraMatrices();
    uploadCameraUBO();
    uploadDiskUBO();
    uploadObjectsUBO();

    rebuildGrid();
    drawGrid();

    if (useCompute_) {
        // Choose compute resolution based on camera motion
        const bool moving = cam_.moving;
        const int targetW = moving ? hiComputeW_ : computeW_;
        const int targetH = moving ? hiComputeH_ : computeH_;
        ensureOutputTex(targetW, targetH);

        dispatchCompute();
        drawFullscreenQuad();
    } else {
        // Fallback: fragment shader computes per-pixel
        drawFullscreenLensFallback();
    }
}

void BlackHoleWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        cam_.dragging = true;
        cam_.lastPos = e->position();
    }
}

void BlackHoleWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!cam_.dragging) return;
    const QPointF pos = e->position();
    const QPointF d = pos - cam_.lastPos;
    cam_.azimuth   += float(d.x()) * cam_.orbitSpeed;
    cam_.elevation -= float(d.y()) * cam_.orbitSpeed;
    cam_.elevation = std::clamp(cam_.elevation, 0.01f, 3.1315926535f);
    cam_.lastPos = pos;
    cam_.moving = true;
}

void BlackHoleWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        cam_.dragging = false;
        cam_.moving = false;
    }
}

void BlackHoleWidget::wheelEvent(QWheelEvent* e) {
    const QPoint numDeg = e->angleDelta() / 120;
    if (!numDeg.isNull()) {
        cam_.radius -= float(numDeg.y()) * float(cam_.zoomSpeed);
        cam_.radius = std::clamp(cam_.radius, cam_.minRadius, cam_.maxRadius);
        cam_.moving = true;
    }
}

void BlackHoleWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Space: paused_ = !paused_; break;
    case Qt::Key_R:
        cam_.radius = 6.34e10f; cam_.azimuth = 0.0f; cam_.elevation = 1.5707963f;
        break;
    case Qt::Key_Escape: window()->close(); break;
    default: QOpenGLWidget::keyPressEvent(e);
    }
}

void BlackHoleWidget::createQuad() {
    const float verts[] = {
        // pos      // uv
        -1.f, -1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 0.f,
         1.f,  1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 1.f
    };
    const GLuint idx[] = {0,1,2, 0,2,3};

    glGenVertexArrays(1, &quadVAO_);
    glGenBuffers(1, &quadVBO_);
    glGenBuffers(1, &quadEBO_);

    glBindVertexArray(quadVAO_);

    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));

    glBindVertexArray(0);
}

void BlackHoleWidget::destroyGL() {
    if (gridEBO_) glDeleteBuffers(1, &gridEBO_), gridEBO_=0;
    if (gridVBO_) glDeleteBuffers(1, &gridVBO_), gridVBO_=0;
    if (gridVAO_) glDeleteVertexArrays(1, &gridVAO_), gridVAO_=0;

    if (quadEBO_) glDeleteBuffers(1, &quadEBO_), quadEBO_=0;
    if (quadVBO_) glDeleteBuffers(1, &quadVBO_), quadVBO_=0;
    if (quadVAO_) glDeleteVertexArrays(1, &quadVAO_), quadVAO_=0;

    if (outputTex_) glDeleteTextures(1, &outputTex_), outputTex_=0;

    if (cameraUBO_) glDeleteBuffers(1, &cameraUBO_), cameraUBO_=0;
    if (diskUBO_) glDeleteBuffers(1, &diskUBO_), diskUBO_=0;
    if (objectsUBO_) glDeleteBuffers(1, &objectsUBO_), objectsUBO_=0;

    if (gridProg_.isLinked()) gridProg_.removeAllShaders();
    if (quadProg_.isLinked()) quadProg_.removeAllShaders();
    if (computeProg_.isLinked()) computeProg_.removeAllShaders();
    if (lensProg_.isLinked()) lensProg_.removeAllShaders();
}

void BlackHoleWidget::ensureOutputTex(int w, int h) {
    if (!outputTex_) glGenTextures(1, &outputTex_);
    glBindTexture(GL_TEXTURE_2D, outputTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BlackHoleWidget::updateCameraMatrices() {
    view_.setToIdentity();
    const QVector3D eye = cam_.position();
    view_.lookAt(eye, cam_.target, QVector3D(0,1,0));

    proj_.setToIdentity();
    const float aspect = width() > 0 ? float(width()) / float(height() > 0 ? height() : 1) : 1.0f;
    proj_.perspective(60.0f, aspect, 1e9f, 1e14f);

    viewProj_ = proj_ * view_;
}

void BlackHoleWidget::uploadCameraUBO() {
    // layout(std140): we pack view, proj, viewProj, camPos
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO_);
    size_t offset = 0;
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QMatrix4x4), view_.constData()); offset += sizeof(QMatrix4x4);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QMatrix4x4), proj_.constData()); offset += sizeof(QMatrix4x4);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QMatrix4x4), viewProj_.constData()); offset += sizeof(QMatrix4x4);
    const QVector3D eye = cam_.position();
    const QVector4D camPos(eye.x(), eye.y(), eye.z(), 1.0f);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QVector4D), &camPos);
}

void BlackHoleWidget::uploadDiskUBO() {
    // r1, r2 from Schwarzschild radius of primary object; density placeholder
    const double mass = objects_.front().mass;
    const double r_s = 2.0 * G_ * mass / (C_ * C_);
    const float r1 = float(2.2 * r_s);
    const float r2 = float(5.2 * r_s);
    const float density = 2.0f;
    const float data[4] = { r1, r2, density, 0.0f };

    glBindBuffer(GL_UNIFORM_BUFFER, diskUBO_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), data);
}

void BlackHoleWidget::uploadObjectsUBO() {
    // Reserved for future use (SSBO preferred); not used by compute shader now.
    glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO_);
    glBufferData(GL_UNIFORM_BUFFER, GLsizeiptr(objects_.size() * sizeof(ObjectData)),
                 objects_.data(), GL_DYNAMIC_DRAW);
}

void BlackHoleWidget::rebuildGrid() {
    // CPU grid generation with Schwarzschild-like warp (matches your reference idea)
    const int gridSize = gridSize_;
    const float spacing = spacing_;
    std::vector<QVector3D> vertices;
    std::vector<GLuint> indices;
    vertices.reserve((gridSize+1)*(gridSize+1));
    indices.reserve(gridSize*gridSize*4);

    for (int z = 0; z <= gridSize; ++z) {
        for (int x = 0; x <= gridSize; ++x) {
            const float worldX = (x - gridSize/2) * spacing;
            const float worldZ = (z - gridSize/2) * spacing;
            float y = 0.f;

            for (const auto& obj : objects_) {
                const QVector3D objPos(obj.posRadius.x(), obj.posRadius.y(), obj.posRadius.z());
                const double mass = obj.mass;
                const double r_s = 2.0 * G_ * mass / (C_ * C_);
                const double dx = double(worldX) - double(objPos.x());
                const double dz = double(worldZ) - double(objPos.z());
                const double dist = std::sqrt(dx*dx + dz*dz);

                if (dist > r_s) {
                    const double deltaY = 2.0 * std::sqrt(r_s * (dist - r_s));
                    y += float(deltaY) - 3e10f;
                } else {
                    y += 2.0f * float(std::sqrt(r_s * r_s)) - 3e10f;
                }
            }
            vertices.emplace_back(worldX, y, worldZ);
        }
    }

    for (int z = 0; z < gridSize; ++z) {
        for (int x = 0; x < gridSize; ++x) {
            const int i = z * (gridSize + 1) + x;
            indices.push_back(i);
            indices.push_back(i + 1);

            indices.push_back(i);
            indices.push_back(i + gridSize + 1);
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

void BlackHoleWidget::dispatchCompute() {
    computeProg_.bind();

    // Bind UBOs by binding index (matches layout(binding=...))
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO_);

    // Bind output image
    glBindImageTexture(0, outputTex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    // Determine current texture size
    glBindTexture(GL_TEXTURE_2D, outputTex_);
    int w=0,h=0;
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

void BlackHoleWidget::drawGrid() {
    gridProg_.bind();
    const int loc = gridProg_.uniformLocation("viewProj");
    if (loc >= 0) gridProg_.setUniformValue(loc, viewProj_);

    glBindVertexArray(gridVAO_);
    glDrawElements(GL_LINES, gridIndexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    gridProg_.release();
}

void BlackHoleWidget::drawFullscreenQuad() {
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

void BlackHoleWidget::drawFullscreenLensFallback() {
    // Bind UBOs for fragment shader too
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, objectsUBO_);

    lensProg_.bind();
    glBindVertexArray(quadVAO_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    lensProg_.release();
}

QPointF BlackHoleWidget::projectToScreen(const QVector3D& p, bool& clipped) const {
    QVector4D hp = viewProj_ * QVector4D(p, 1.0f);
    if (hp.w() == 0.0f) { clipped = true; return {}; }
    const QVector3D ndc = QVector3D(hp.x()/hp.w(), hp.y()/hp.w(), hp.z()/hp.w());
    clipped = (ndc.x() < -1.f || ndc.x() > 1.f || ndc.y() < -1.f || ndc.y() > 1.f || ndc.z() < -1.f || ndc.z() > 1.f);
    const float sx = (ndc.x() * 0.5f + 0.5f) * float(width());
    const float sy = (1.0f - (ndc.y() * 0.5f + 0.5f)) * float(height());
    return QPointF(sx, sy);
}