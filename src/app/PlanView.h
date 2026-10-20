#pragma once

#include "oa/core/document.h"

#include <QPointF>
#include <QWidget>

#include <optional>
#include <vector>

class Workspace;

// The 2D plan editor. World coordinates are inches with y pointing up
// (north); the view maps them to screen pixels with pan and zoom.
class PlanView : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Select, Wall, Door, Window, Beam, Floor, Roof, Room, RoofPlane };

    explicit PlanView(Workspace* ws, QWidget* parent = nullptr);

    Tool tool() const { return tool_; }
    void setTool(Tool tool);
    void setShowLoads(bool show);
    bool showLoads() const { return showLoads_; }

public slots:
    void zoomToFit();
    void deleteSelection();

signals:
    void statusMessage(const QString& text);
    void toolChanged(PlanView::Tool tool);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    // Tab cycles through stacked objects instead of moving focus.
    bool focusNextPrevChild(bool) override { return false; }

private:
    QPointF toScreen(oa::Vec2 p) const;
    oa::Vec2 toWorld(QPointF p) const;
    double pixels(double inches) const { return inches * scale_; }

    // Snaps a world point to wall ends, then angle and length (when drawing
    // from `from`), then the grid. Holding Ctrl disables snapping.
    oa::Vec2 snap(oa::Vec2 p, std::optional<oa::Vec2> from, Qt::KeyboardModifiers mods) const;
    // Everything under a point on the current level, topmost first:
    // beams, openings, walls, roof planes, roofs, floor framing, rooms.
    std::vector<oa::ObjectId> pickAll(oa::Vec2 p) const;
    oa::ObjectId pick(oa::Vec2 p) const;
    // Selects `id` from the stack `hits`, noting where it sits in the stack.
    void selectFromStack(const std::vector<oa::ObjectId>& hits, oa::ObjectId id);
    void cycleStack();
    void showStackMenu(QPointF screenPos, const std::vector<oa::ObjectId>& hits);
    const oa::Wall* wallNear(oa::Vec2 p, double pixelTolerance) const;

    void drawGrid(QPainter& g) const;
    void drawLevelBelow(QPainter& g) const;
    void drawRooms(QPainter& g) const;
    void drawPlatforms(QPainter& g) const;
    void drawWalls(QPainter& g) const;
    void drawOpenings(QPainter& g) const;
    void drawBeams(QPainter& g) const;
    void drawRoofs(QPainter& g) const;
    void drawRoofPlanes(QPainter& g) const;
    void drawLoads(QPainter& g) const;
    void drawToolPreview(QPainter& g) const;
    void drawLengthLabel(QPainter& g, oa::Vec2 a, oa::Vec2 b) const;

    void finishRect(oa::Vec2 a, oa::Vec2 b);
    void placeOpening(oa::Vec2 p, oa::OpeningKind kind);
    void finishRoofPlane(oa::Vec2 a, oa::Vec2 b, oa::Vec2 high);

    Workspace* ws_;
    Tool tool_ = Tool::Select;
    bool showLoads_ = false;

    double scale_ = 1.0;     // pixels per inch
    QPointF offset_{40, 40}; // screen position of the world origin
    bool fitted_ = false;

    oa::Vec2 cursor_;
    std::optional<oa::Vec2> anchor_; // first click of a two-point tool
    std::optional<oa::Vec2> anchor2_; // second click of the roof plane tool (end of the eave)
    QPointF panStart_;
    bool panning_ = false;

    // Dragging in the select tool edits a preview copy; releasing the
    // mouse commits it as one undoable step.
    enum class Drag { None, Move, EndA, EndB };
    Drag drag_ = Drag::None;
    oa::Vec2 dragStart_;
    std::optional<oa::Document> preview_;

    // Objects stacked under the last click, so clicking the same spot again
    // (or Tab) selects the next one down.
    std::vector<oa::ObjectId> stack_;
    bool cycleOnRelease_ = false;
    QString stackHint_;
    const oa::Document& doc() const;
};
