#include "blackhole_widget.h"

#include <cmath>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QWheelEvent>

// Guard for headers that don't define this (e.g., macOS < 4.2 headers)
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif

// Minimal fallback shader sources in case Qt resources are not bundled.
// Use GLSL 410 for VS/FS (macOS compatible) and 430 for compute (when available).
static const char* kGridVertSrc = R"(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 viewProj;
void main(){ gl_Position = viewProj * vec4(aPos,1.0); }
)";

static const char* kGridFragSrc = R"(#version 410 core
out vec4 FragColor;
void main(){ FragColor = vec4(0.2,0.6,1.0,0.35); }
)";

static const char* kQuadVertSrc = R"(#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }
)";

static const char* kQuadFragSrc = R"(#version 410 core
in vec2 vUV;
layout(location=0) out vec4 FragColor;
uniform sampler2D uTex;
void main(){ FragColor = texture(uTex, vUV); }
)";

// Fallback fragment lensing shader (used on macOS or when compute unavailable).
static const char* kLensFragSrc = R"(#version 410 core
out vec4 FragColor;

uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uViewProj;
uniform vec4 uCamPos;
uniform vec4 uDisk; // x=r1, y=r2, z=density

in vec2 vUV;

bool intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius, out float tHit) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius*radius;
    float disc = b*b - c;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    float t0 = -b - s;
    float t1 = -b + s;
    tHit = (t0 > 0.0) ? t0 : ((t1 > 0.0) ? t1 : -1.0);
    return tHit > 0.0;
}

bool intersectPlaneY(vec3 ro, vec3 rd, float y, out float t) {
    if (abs(rd.y) < 1e-6) return false;
    t = (y - ro.y) / rd.y;
    return t > 0.0;
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;

    mat4 invProj = inverse(uProj);
    mat4 invView = inverse(uView);
    vec4 clip = vec4(ndc, 1.0, 1.0);
    vec4 viewPos = invProj * clip;
    viewPos /= viewPos.w;
    vec3 rdView = normalize(viewPos.xyz);
    vec3 rd = normalize((invView * vec4(rdView, 0.0)).xyz);
    vec3 ro = uCamPos.xyz;

    float r_s = uDisk.x / 2.2;
    float r1  = uDisk.x;
    float r2  = uDisk.y;

    float tHit;
    bool hitBH = intersectSphere(ro, rd, vec3(0.0), r_s, tHit);

    float tPlane;
    bool hitPlane = intersectPlaneY(ro, rd, 0.0, tPlane);
    bool inDisk = false;
    vec3 pDisk = vec3(0.0);
    if (hitPlane) {
        pDisk = ro + tPlane * rd;
        float R = length(pDisk.xz);
        inDisk = (R >= r1 && R <= r2);
    }

    vec3 col = vec3(0.0);
    if (hitBH && (!hitPlane || tHit < tPlane)) {
        col = vec3(0.0);
    } else if (hitPlane && inDisk) {
        float R = clamp(length(pDisk.xz), r1, r2);
        float a = atan(pDisk.z, pDisk.x);
        float v = 1.0 - smoothstep(r1, r2, R);
        vec3 tint = vec3(1.0, 0.9, 0.7);
        col = v * tint * (0.8 + 0.2 * sin(5.0*a));
    } else {
        float r = length(ndc);
        float vignette = smoothstep(1.2, 0.2, r);
        col = vec3(0.02) * vignette;
    }

    FragColor = vec4(col, 1.0);
}
)";

BlackHoleWidget::BlackHoleWidget(QWidget* parent)
    : QOpenGLWidget(parent) {

    // Set OpenGL format before context creation
    QSurfaceFormat format;
    format.setVersion(4, 1); // macOS supports up to 4.1
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    setFormat(format);

    setFocusPolicy(Qt::StrongFocus);

    // Single BH at origin (mass ~ 4.3e6 solar masses)
    constexpr double solar = 1.98847e30;
    m_objects.push_back({
        QVector4D(0, 0, 0, 5e10f), // pos + radius
        QVector4D(1, 1, 1, 1),     // color
        4.3e6 * solar,             // mass
        QVector3D(0, 0, 0)         // velocity (unused)
    });

    // Drive frames
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (!m_paused) {
            // future dynamics here (object movement, etc.)
        }
        // request repaint -> paintGL
        update();
    });
    m_timer.start(16); // ~60 fps
}

BlackHoleWidget::~BlackHoleWidget() {
    makeCurrent();
    destroyGL();
    doneCurrent();
}

void BlackHoleWidget::initializeGL() {
    makeCurrent();

    if (!initializeOpenGLFunctions()) {
        qWarning() << "Failed to initialize OpenGL functions";
        assert(false);
        return;
    }

    // Detect compute support: GL 4.3 or ARB_compute_shader
    QOpenGLContext* ctx = context();
    const auto fmt = ctx->format();
    const bool verOK = (fmt.majorVersion() > 4) || (fmt.majorVersion() == 4 && fmt.minorVersion() >= 3);
    const bool hasARB = ctx->hasExtension(QByteArrayLiteral("GL_ARB_compute_shader"));
    m_useCompute = verOK || hasARB;

    // Helper to load shader from resource, falling back to embedded code
    auto addFromResOrCode =
        [](QOpenGLShaderProgram& prog, QOpenGLShader::ShaderType type, const char* resPath, const char* fallbackSrc
        ) -> bool {
        if (prog.addShaderFromSourceFile(type, resPath))
            return true;
        return prog.addShaderFromSourceCode(type, fallbackSrc);
    };

    // Compile grid program
    bool ok = addFromResOrCode(m_gridProg, QOpenGLShader::Vertex, ":/shaders/grid.vert", kGridVertSrc);
    ok = addFromResOrCode(m_gridProg, QOpenGLShader::Fragment, ":/shaders/grid.frag", kGridFragSrc) && ok;
    if (!m_gridProg.link() || !ok) {
        qWarning() << "Failed to build grid program:" << m_gridProg.log();
    }

    // Compile quad program
    ok = addFromResOrCode(m_quadProg, QOpenGLShader::Vertex, ":/shaders/quad.vert", kQuadVertSrc);
    ok = addFromResOrCode(m_quadProg, QOpenGLShader::Fragment, ":/shaders/quad.frag", kQuadFragSrc) && ok;
    if (!m_quadProg.link() || !ok) {
        qWarning() << "Failed to build quad program:" << m_quadProg.log();
    }

    if (m_useCompute) {
        // Compute shader is only available when GL 4.3+/ARB_compute_shader
        if (!m_computeProg.addShaderFromSourceFile(QOpenGLShader::Compute, ":/shaders/geodesic.comp")
            || !m_computeProg.link()) {
            qWarning() << "Compute shader missing or failed to link; falling back to fragment lensing.";
            m_useCompute = false;
        }
    }

    if (!m_useCompute) {
        // Fallback lens program (fragment shader does the lensing)
        ok = addFromResOrCode(m_lensProg, QOpenGLShader::Vertex, ":/shaders/quad.vert", kQuadVertSrc);
        ok = addFromResOrCode(m_lensProg, QOpenGLShader::Fragment, ":/shaders/lens.frag", kLensFragSrc) && ok;
        if (!m_lensProg.link() || !ok) {
            qWarning() << "Failed to build lens fallback program:" << m_lensProg.log();
        }
    }

    // Create fullscreen quad mesh
    createQuad();

    // Create grid mesh
    rebuildGrid();

    // Create Camera UBO
    glGenBuffers(1, &m_cameraUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_cameraUBO);
    glBufferData(GL_UNIFORM_BUFFER, 3 * sizeof(QMatrix4x4) + sizeof(QVector4D), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_cameraUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Create Disk UBO
    glGenBuffers(1, &m_diskUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_diskUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(QVector4D), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_diskUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Create Objects UBO (for future use, currently just a single BH)
    glGenBuffers(1, &m_objectsUBO);

    // Initial camera & disk settings
    uploadCameraUBO();
    uploadDiskUBO();

    // OpenGL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void BlackHoleWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    updateCameraMatrices();
    uploadCameraUBO();
}

void BlackHoleWidget::paintGL() {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    updateCameraMatrices();
    uploadCameraUBO();
    uploadDiskUBO();
    uploadObjectsUBO();

    rebuildGrid();
    drawGrid();

    if (m_useCompute) {
        // Choose compute resolution based on camera motion
        const bool moving = m_cam.moving;
        const int targetW = moving ? m_hiComputeW : m_computeW;
        const int targetH = moving ? m_hiComputeH : m_computeH;
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
        m_cam.dragging = true;
        m_cam.lastPos = e->position();
    }
}

void BlackHoleWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!m_cam.dragging)
        return;
    const QPointF pos = e->position();
    const QPointF d = pos - m_cam.lastPos;
    m_cam.azimuth += float(d.x()) * m_cam.orbitSpeed;
    m_cam.elevation -= float(d.y()) * m_cam.orbitSpeed;
    m_cam.elevation = std::clamp(m_cam.elevation, 0.01f, 3.1315926535f);
    m_cam.lastPos = pos;
    m_cam.moving = true;
}

void BlackHoleWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_cam.dragging = false;
        m_cam.moving = false;
    }
}

void BlackHoleWidget::wheelEvent(QWheelEvent* e) {
    const QPoint numDeg = e->angleDelta() / 120;
    if (!numDeg.isNull()) {
        m_cam.radius -= float(numDeg.y()) * float(m_cam.zoomSpeed);
        m_cam.radius = std::clamp(m_cam.radius, m_cam.minRadius, m_cam.maxRadius);
        m_cam.moving = true;
    }
}

void BlackHoleWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Space:
        m_paused = !m_paused;
        break;
    case Qt::Key_R:
        m_cam.radius = 6.34e10f;
        m_cam.azimuth = 0.0f;
        m_cam.elevation = 1.5707963f;
        break;
    case Qt::Key_Escape:
        window()->close();
        break;
    default:
        QOpenGLWidget::keyPressEvent(e);
    }
}

void BlackHoleWidget::createQuad() {
    // Ensure we have a valid OpenGL context
    if (!context() || !context()->isValid()) {
        qWarning() << "Invalid OpenGL context in createQuad()";
        return;
    }

    makeCurrent();

    const float verts[] = {// pos      // uv
                           -1.f,
                           -1.f,
                           0.f,
                           0.f,
                           1.f,
                           -1.f,
                           1.f,
                           0.f,
                           1.f,
                           1.f,
                           1.f,
                           1.f,
                           -1.f,
                           1.f,
                           0.f,
                           1.f
    };
    const GLuint idx[] = {0, 1, 2, 0, 2, 3};

    // Remove the assertion that checks uninitialized variables
    // Initialize the OpenGL objects
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glGenBuffers(1, &m_quadEBO);

    glBindVertexArray(m_quadVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void BlackHoleWidget::destroyGL() {
    if (m_gridEBO)
        glDeleteBuffers(1, &m_gridEBO), m_gridEBO = 0;
    if (m_gridVBO)
        glDeleteBuffers(1, &m_gridVBO), m_gridVBO = 0;
    if (m_gridVAO)
        glDeleteVertexArrays(1, &m_gridVAO), m_gridVAO = 0;

    if (m_quadEBO)
        glDeleteBuffers(1, &m_quadEBO), m_quadEBO = 0;
    if (m_quadVBO)
        glDeleteBuffers(1, &m_quadVBO), m_quadVBO = 0;
    if (m_quadVAO)
        glDeleteVertexArrays(1, &m_quadVAO), m_quadVAO = 0;

    if (m_outputTex)
        glDeleteTextures(1, &m_outputTex), m_outputTex = 0;

    if (m_cameraUBO)
        glDeleteBuffers(1, &m_cameraUBO), m_cameraUBO = 0;
    if (m_diskUBO)
        glDeleteBuffers(1, &m_diskUBO), m_diskUBO = 0;
    if (m_objectsUBO)
        glDeleteBuffers(1, &m_objectsUBO), m_objectsUBO = 0;

    if (m_gridProg.isLinked())
        m_gridProg.removeAllShaders();
    if (m_quadProg.isLinked())
        m_quadProg.removeAllShaders();
    if (m_computeProg.isLinked())
        m_computeProg.removeAllShaders();
    if (m_lensProg.isLinked())
        m_lensProg.removeAllShaders();
}

void BlackHoleWidget::ensureOutputTex(int w, int h) {
    if (!m_outputTex)
        glGenTextures(1, &m_outputTex);
    glBindTexture(GL_TEXTURE_2D, m_outputTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BlackHoleWidget::updateCameraMatrices() {
    m_view.setToIdentity();
    const QVector3D eye = m_cam.position();
    m_view.lookAt(eye, m_cam.target, QVector3D(0, 1, 0));

    m_proj.setToIdentity();
    const float aspect = width() > 0 ? float(width()) / float(height() > 0 ? height() : 1) : 1.0f;
    m_proj.perspective(60.0f, aspect, 1e9f, 1e14f);

    m_viewProj = m_proj * m_view;
}

void BlackHoleWidget::uploadCameraUBO() {
    // layout(std140): pack view, proj, viewProj, camPos
    glBindBuffer(GL_UNIFORM_BUFFER, m_cameraUBO);
    size_t offset = 0;
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QMatrix4x4), m_view.constData());
    offset += sizeof(QMatrix4x4);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QMatrix4x4), m_proj.constData());
    offset += sizeof(QMatrix4x4);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QMatrix4x4), m_viewProj.constData());
    offset += sizeof(QMatrix4x4);
    const QVector3D eye = m_cam.position();
    const QVector4D camPos(eye.x(), eye.y(), eye.z(), 1.0f);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(QVector4D), &camPos);
}

void BlackHoleWidget::uploadDiskUBO() {
    // r1, r2 from Schwarzschild radius of primary object; density placeholder
    const double mass = m_objects.front().mass;
    const double r_s = 2.0 * G_ * mass / (C_ * C_);
    const float r1 = float(2.2 * r_s);
    const float r2 = float(5.2 * r_s);
    const float density = 2.0f;
    const float data[4] = {r1, r2, density, 0.0f};

    glBindBuffer(GL_UNIFORM_BUFFER, m_diskUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), data);
}

void BlackHoleWidget::uploadObjectsUBO() {
    // Reserved for future use (SSBO preferred); not used by shaders now.
    glBindBuffer(GL_UNIFORM_BUFFER, m_objectsUBO);
    glBufferData(
        GL_UNIFORM_BUFFER, GLsizeiptr(m_objects.size() * sizeof(ObjectData)), m_objects.data(), GL_DYNAMIC_DRAW
    );
}

void BlackHoleWidget::rebuildGrid() {
    // CPU grid generation with Schwarzschild-like warp
    const int gridSize = m_gridSize;
    const float spacing = m_spacing;
    std::vector<QVector3D> vertices;
    std::vector<GLuint> indices;
    vertices.reserve((gridSize + 1) * (gridSize + 1));
    indices.reserve(gridSize * gridSize * 4);

    for (int z = 0; z <= gridSize; ++z) {
        for (int x = 0; x <= gridSize; ++x) {
            const float worldX = (x - gridSize / 2) * spacing;
            const float worldZ = (z - gridSize / 2) * spacing;
            float y = 0.f;

            for (const auto& obj : m_objects) {
                const QVector3D objPos(obj.posRadius.x(), obj.posRadius.y(), obj.posRadius.z());
                const double mass = obj.mass;
                const double r_s = 2.0 * G_ * mass / (C_ * C_);
                const double dx = double(worldX) - double(objPos.x());
                const double dz = double(worldZ) - double(objPos.z());
                const double dist = std::sqrt(dx * dx + dz * dz);

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

    if (!m_gridVAO)
        glGenVertexArrays(1, &m_gridVAO);
    if (!m_gridVBO)
        glGenBuffers(1, &m_gridVBO);
    if (!m_gridEBO)
        glGenBuffers(1, &m_gridEBO);

    glBindVertexArray(m_gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVBO);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(QVector3D)), vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_gridEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(GLuint)), indices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QVector3D), (void*)0);
    glBindVertexArray(0);

    m_gridIndexCount = int(indices.size());
}

void BlackHoleWidget::dispatchCompute() {
    m_computeProg.bind();

    // Bind UBOs by binding index (matches layout(binding=...))
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_cameraUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_diskUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_objectsUBO);

    // Bind output image
    glBindImageTexture(0, m_outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    // Determine current texture size
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

void BlackHoleWidget::drawGrid() {
    m_gridProg.bind();
    const int loc = m_gridProg.uniformLocation("viewProj");
    if (loc >= 0)
        m_gridProg.setUniformValue(loc, m_viewProj);

    glBindVertexArray(m_gridVAO);
    glDrawElements(GL_LINES, m_gridIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    m_gridProg.release();
}

void BlackHoleWidget::drawFullscreenQuad() {
    m_quadProg.bind();
    const int u = m_quadProg.uniformLocation("uTex");
    if (u >= 0)
        m_quadProg.setUniformValue(u, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_outputTex);

    glBindVertexArray(m_quadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    m_quadProg.release();
}

void BlackHoleWidget::drawFullscreenLensFallback() {
    // Bind UBOs for fragment shader too
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_cameraUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_diskUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, m_objectsUBO);

    m_lensProg.bind();

    // Add uniform setters for macOS OpenGL 4.1 compatibility
    QMatrix4x4 view, proj, viewProj;
    QVector4D camPos, diskParams;

    // Temporary buffers for QVector4D data
    float camPosData[4] = {0.0f};
    float diskParamsData[4] = {0.0f};

    // Retrieve data from UBOs
    glBindBuffer(GL_UNIFORM_BUFFER, m_cameraUBO);
    glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(QMatrix4x4), view.data());
    glGetBufferSubData(GL_UNIFORM_BUFFER, sizeof(QMatrix4x4), sizeof(QMatrix4x4), proj.data());
    glGetBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof(QMatrix4x4), sizeof(QMatrix4x4), viewProj.data());
    glGetBufferSubData(GL_UNIFORM_BUFFER, 3 * sizeof(QMatrix4x4), sizeof(float) * 4, camPosData);
    glBindBuffer(GL_UNIFORM_BUFFER, m_diskUBO);
    glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(float) * 4, diskParamsData);

    // Copy data to QVector4D objects
    camPos = QVector4D(camPosData[0], camPosData[1], camPosData[2], camPosData[3]);
    diskParams = QVector4D(diskParamsData[0], diskParamsData[1], diskParamsData[2], diskParamsData[3]);

    // Set uniforms directly instead of relying on UBO bindings
    m_lensProg.setUniformValue("uView", view);
    m_lensProg.setUniformValue("uProj", proj);
    m_lensProg.setUniformValue("uViewProj", viewProj);
    m_lensProg.setUniformValue("uCamPos", camPos);
    m_lensProg.setUniformValue("uDisk", diskParams);

    glBindVertexArray(m_quadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    m_lensProg.release();
}

QPointF BlackHoleWidget::projectToScreen(const QVector3D& p, bool& clipped) const {
    QVector4D hp = m_viewProj * QVector4D(p, 1.0f);
    if (hp.w() == 0.0f) {
        clipped = true;
        return {};
    }
    const QVector3D ndc = QVector3D(hp.x() / hp.w(), hp.y() / hp.w(), hp.z() / hp.w());
    clipped = (ndc.x() < -1.f || ndc.x() > 1.f || ndc.y() < -1.f || ndc.y() > 1.f || ndc.z() < -1.f || ndc.z() > 1.f);
    const float sx = (ndc.x() * 0.5f + 0.5f) * static_cast<float>(width());
    const float sy = (1.0f - (ndc.y() * 0.5f + 0.5f)) * static_cast<float>(height());
    return {sx, sy};
}