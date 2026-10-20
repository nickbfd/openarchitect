#include "ModelView3D.h"

#include "Workspace.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
uniform mat4 mvp;
out vec3 vNormal;
out vec3 vColor;
void main() {
    vNormal = normal;
    vColor = color;
    gl_Position = mvp * vec4(position, 1.0);
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec3 vNormal;
in vec3 vColor;
uniform vec3 lightDir;
out vec4 fragColor;
void main() {
    // Two-sided lighting so roof planes and wall faces read from either side.
    float diffuse = abs(dot(normalize(vNormal), lightDir));
    float sky = 0.5 + 0.5 * normalize(vNormal).z;
    fragColor = vec4(vColor * (0.35 + 0.15 * sky + 0.55 * diffuse), 1.0);
}
)";

} // namespace

ModelView3D::ModelView3D(Workspace* ws, QWidget* parent) : QOpenGLWidget(parent), ws_(ws) {
    setFocusPolicy(Qt::StrongFocus);
    connect(ws_, &Workspace::documentChanged, this, [this] {
        meshDirty_ = true;
        update();
    });
    connect(ws_, &Workspace::selectionChanged, this, [this] {
        meshDirty_ = true;
        update();
    });
}

ModelView3D::~ModelView3D() {
    makeCurrent();
    vbo_.destroy();
    vao_.destroy();
    doneCurrent();
}

void ModelView3D::setShowFraming(bool on) {
    showFraming_ = on;
    meshDirty_ = true;
    update();
}

void ModelView3D::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.80f, 0.86f, 0.93f, 1.0f);

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    program_.link();

    vao_.create();
    vbo_.create();
    meshDirty_ = true;
}

void ModelView3D::rebuild() {
    mesh_ = buildMesh(ws_->doc(), ws_->selectedIds(), showFraming_);
    vertexCount_ = static_cast<int>(mesh_.vertices.size());

    QOpenGLVertexArrayObject::Binder bind(&vao_);
    vbo_.bind();
    vbo_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    vbo_.allocate(mesh_.vertices.data(), static_cast<int>(mesh_.vertices.size() * sizeof(MeshVertex)));
    program_.bind();
    const int stride = sizeof(MeshVertex);
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, offsetof(MeshVertex, position), 3, stride);
    program_.enableAttributeArray(1);
    program_.setAttributeBuffer(1, GL_FLOAT, offsetof(MeshVertex, normal), 3, stride);
    program_.enableAttributeArray(2);
    program_.setAttributeBuffer(2, GL_FLOAT, offsetof(MeshVertex, color), 3, stride);
    vbo_.release();
    meshDirty_ = false;

    if (!framed_ && !mesh_.empty()) {
        frameModel();
        framed_ = true;
    }
}

void ModelView3D::frameModel() {
    if (mesh_.empty()) return;
    const auto& lo = mesh_.min;
    const auto& hi = mesh_.max;
    target_ = QVector3D(float(lo.x + hi.x) / 2, float(lo.y + hi.y) / 2, float(lo.z + hi.z) / 2);
    const float size = float(std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}));
    distance_ = std::max(size * 1.8f, 120.0f);
    update();
}

void ModelView3D::resizeGL(int, int) {}

QVector3D ModelView3D::eye() const {
    const float yaw = qDegreesToRadians(yaw_), pitch = qDegreesToRadians(pitch_);
    const QVector3D dir(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
    return target_ + dir * distance_;
}

QMatrix4x4 ModelView3D::viewProjection() const {
    QMatrix4x4 projection;
    projection.perspective(45.0f, float(width()) / float(std::max(height(), 1)), distance_ * 0.01f,
                           distance_ * 20.0f);
    QMatrix4x4 view;
    view.lookAt(eye(), target_, QVector3D(0, 0, 1));
    return projection * view;
}

void ModelView3D::paintGL() {
    if (meshDirty_) rebuild();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (vertexCount_ == 0) return;

    program_.bind();
    program_.setUniformValue("mvp", viewProjection());
    program_.setUniformValue("lightDir", QVector3D(0.4f, -0.6f, 0.7f).normalized());
    QOpenGLVertexArrayObject::Binder bind(&vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
}

void ModelView3D::mousePressEvent(QMouseEvent* e) {
    lastPos_ = pressPos_ = e->position().toPoint();
    dragged_ = false;
}

void ModelView3D::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && !dragged_) pickAt(e->position().toPoint());
}

// Casts a ray through the clicked pixel and selects what it hits first,
// switching to that object's level so the plan and properties follow.
void ModelView3D::pickAt(QPoint pos) {
    if (meshDirty_) return;
    const QMatrix4x4 inverse = viewProjection().inverted();
    const float x = 2.0f * float(pos.x()) / float(std::max(width(), 1)) - 1.0f;
    const float y = 1.0f - 2.0f * float(pos.y()) / float(std::max(height(), 1));
    const QVector3D nearPoint = inverse.map(QVector3D(x, y, -1.0f));
    const QVector3D farPoint = inverse.map(QVector3D(x, y, 1.0f));
    const QVector3D dir = (farPoint - nearPoint).normalized();
    const oa::ObjectId hit = pickMesh(mesh_, {nearPoint.x(), nearPoint.y(), nearPoint.z()}, {dir.x(), dir.y(), dir.z()});
    if (hit != oa::kNoId) ws_->setCurrentLevel(ws_->doc().levelOf(hit));
    ws_->select(hit);
}

void ModelView3D::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    const QPoint delta = pos - lastPos_;
    lastPos_ = pos;
    if ((pos - pressPos_).manhattanLength() > 3) dragged_ = true;
    if (!dragged_) return;
    if (e->buttons() & Qt::LeftButton) {
        yaw_ -= delta.x() * 0.4f;
        pitch_ = std::clamp(pitch_ + delta.y() * 0.4f, -5.0f, 89.0f);
    } else if (e->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
        const QVector3D forward = (target_ - eye()).normalized();
        const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 0, 1)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, forward);
        const float scale = distance_ * 0.0015f;
        target_ += (-right * float(delta.x()) + up * float(delta.y())) * scale;
    }
    update();
}

void ModelView3D::wheelEvent(QWheelEvent* e) {
    const float steps = e->angleDelta().y() / 120.0f;
    distance_ = std::clamp(distance_ * std::pow(0.85f, steps), 24.0f, 50000.0f);
    update();
}

void ModelView3D::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_F || e->key() == Qt::Key_Home) frameModel();
    else QOpenGLWidget::keyPressEvent(e);
}
