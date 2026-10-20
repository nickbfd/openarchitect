#pragma once

#include "Mesh.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>

class Workspace;

// Live 3D model of the plan with an orbit camera. A left click selects the
// wall, opening, beam, roof or floor under the cursor; left drag orbits,
// right or middle drag pans, the wheel zooms, and F frames the model.
class ModelView3D : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit ModelView3D(Workspace* ws, QWidget* parent = nullptr);
    ~ModelView3D() override;

public slots:
    void frameModel();
    // Shows walls as studs, plates, headers and insulation.
    void setShowFraming(bool on);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void rebuild();
    QVector3D eye() const;
    QMatrix4x4 viewProjection() const;
    void pickAt(QPoint pos);

    Workspace* ws_;
    Mesh mesh_;
    bool meshDirty_ = true;
    bool framed_ = false;
    bool showFraming_ = false;

    QOpenGLShaderProgram program_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao_;
    int vertexCount_ = 0;

    // Orbit camera around a target, angles in degrees, distance in inches.
    QVector3D target_{240, 168, 100};
    float yaw_ = -60.0f;
    float pitch_ = 25.0f;
    float distance_ = 1200.0f;
    QPoint lastPos_;
    QPoint pressPos_;
    bool dragged_ = false;
};
