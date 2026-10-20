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
    enum class Tool {
        Select, Wall, Door, Window, Beam, Floor, Roof, RoomDivider, RoofPlane, JoinPlanes, Post, Dimension, Elevation
    };

    explicit PlanView(Workspace* ws, QWidget* parent = nullptr);

    Tool tool() const { return tool_; }
    void setTool(Tool tool);
    void setShowLoads(bool show);
    bool showLoads() const { return showLoads_; }
    // The wall type new walls get.
    void setWallType(oa::ObjectId id) { wallTypeId_ = id; }
    oa::ObjectId wallType() const { return wallTypeId_; }
    // The size new doors or windows get.
    void setOpeningSize(oa::OpeningKind kind, double width, double height, double sill);
    void setShowOpeningSizes(bool show);

public slots:
    void zoomToFit();
    void deleteSelection();

signals:
    void statusMessage(const QString& text);
    void toolChanged(PlanView::Tool tool);

protected:
    bool event(QEvent* e) override;
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
    const oa::Wall* wallNear(oa::Vec2 p, double pixelTolerance, bool skipDividers = false) const;
    // The roof plane edge nearest `p` on this level, within the pick distance.
    std::optional<std::pair<oa::ObjectId, int>> roofEdgeNear(oa::Vec2 p) const;
    // A handle of the selected roof plane under a screen point: a corner, or
    // the middle of an edge.
    std::optional<int> roofVertexAt(QPointF pos) const;
    std::optional<int> roofEdgeHandleAt(QPointF pos) const;

    void drawGrid(QPainter& g) const;
    void drawLevelBelow(QPainter& g) const;
    void drawRooms(QPainter& g) const;
    void drawPlatforms(QPainter& g) const;
    void drawWalls(QPainter& g) const;
    void drawOpenings(QPainter& g) const;
    void drawBeams(QPainter& g) const;
    void drawRoofs(QPainter& g) const;
    void drawRoofPlanes(QPainter& g) const;
    void drawPosts(QPainter& g) const;
    void drawDimensions(QPainter& g) const;
    void drawDimension(QPainter& g, oa::Vec2 a, oa::Vec2 b, double offset, bool selected) const;
    void drawTerrain(QPainter& g) const;
    void drawTyped(QPainter& g) const;
    void drawLoads(QPainter& g) const;
    void drawToolPreview(QPainter& g) const;
    void drawLengthLabel(QPainter& g, oa::Vec2 a, oa::Vec2 b) const;

    // Shift box selection: the objects entirely inside a box, limited to
    // the kind the current tool draws (everything with Select).
    bool toolAccepts(oa::ObjectId id) const;
    std::vector<oa::ObjectId> boxPick(QPointF a, QPointF b) const;
    void finishBox(QPointF a, QPointF b, bool add);
    // Snapping for dimension ends: wall framing corners, openings, posts and
    // beam ends, then the usual snapping.
    std::optional<oa::Vec2> dimensionSnap(oa::Vec2 p, Qt::KeyboardModifiers mods) const;
    // The node of the selected dimension under a screen point.
    enum class Drag;
    std::optional<Drag> dimensionHandleAt(QPointF pos) const;
    void drawBox(QPainter& g) const;

    void finishRect(oa::Vec2 a, oa::Vec2 b);
    void placeOpening(oa::Vec2 p, oa::OpeningKind kind);
    void finishRoofPlane(oa::Vec2 a, oa::Vec2 b, oa::Vec2 high);
    void addWall(oa::Vec2 a, oa::Vec2 b);
    void placePost(oa::Vec2 p);
    void joinClick(oa::Vec2 p);
    void toolClick(oa::Vec2 p, oa::Vec2 raw);
    void finishDimension(oa::Vec2 a, oa::Vec2 b, oa::Vec2 at);
    void placeElevation(oa::Vec2 p);
    // Typing a length while drawing: the tools that take one, and Enter.
    bool takesTypedLength() const;
    void commitTyped();

    Workspace* ws_;
    Tool tool_ = Tool::Select;
    bool showLoads_ = false;
    oa::ObjectId wallTypeId_ = oa::kNoId;
    bool showOpeningSizes_ = true;
    struct OpeningSize {
        double width, height, sill;
    };
    OpeningSize doorSize_{36, 80, 0};
    OpeningSize windowSize_{36, 48, 32};
    QString typed_; // a length being typed while drawing
    // First pick of the join tool: a roof plane and one of its edges.
    std::optional<std::pair<oa::ObjectId, int>> joinEdge_;

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
    enum class Drag { None, Move, EndA, EndB, Vertex, Edge, DimA, DimB, Box };
    Drag drag_ = Drag::None;
    int dragIndex_ = -1; // the roof plane corner or edge being dragged
    oa::Vec2 dragStart_;
    std::optional<oa::Document> preview_;
    QPointF boxStart_, boxEnd_; // screen corners of a Shift box selection
    bool boxAdds_ = false;      // the box adds to the selection instead of replacing it
    // Dimension snap points of the current level, rebuilt when the plan changes.
    mutable std::vector<oa::Vec2> dimensionSnaps_;
    mutable bool dimensionSnapsValid_ = false;

    // Objects stacked under the last click, so clicking the same spot again
    // (or Tab) selects the next one down.
    std::vector<oa::ObjectId> stack_;
    bool cycleOnRelease_ = false;
    QString stackHint_;
    const oa::Document& doc() const;
};
