#include "PlanView.h"

#include "Workspace.h"

#include "oa/core/dimensions.h"
#include "oa/core/editing.h"
#include "oa/core/roof_builder.h"
#include "oa/core/terrain.h"
#include "oa/core/units.h"
#include "oa/structural/members.h"
#include "oa/structural/posts.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace oa;

namespace {

constexpr double kPickPixels = 8.0;
constexpr double kSnapPixels = 10.0;
constexpr double kAngleStep = kPi / 12.0; // 15 degrees

QString lengthText(double inches) { return QString::fromStdString(formatFeetInches(inches, 8)); }

// The size callout drafters write on doors and windows: feet and inches of
// width, then of height, so 3'-0" x 6'-8" is "3068".
QString sizeCallout(double width, double height) {
    auto part = [](double inches) {
        const long whole = std::lround(inches);
        return QString::number(whole / 12) + QString::number(whole % 12);
    };
    return part(width) + part(height);
}

// Text drawn centered on `at`, turned to run along `dir` and kept upright.
void drawAlong(QPainter& g, QPointF at, QPointF dir, const QString& text, double lift) {
    double deg = std::atan2(dir.y(), dir.x()) * 180.0 / kPi;
    if (deg > 90.0) deg -= 180.0;
    if (deg <= -90.0) deg += 180.0;
    g.save();
    g.translate(at);
    g.rotate(deg);
    const QRectF box(-80, -lift - 16, 160, 16);
    g.drawText(box, Qt::AlignCenter, text);
    g.restore();
}

// The screen box drawAlong puts its text in, for clicking on it.
QRectF alongRect(QPointF at, QPointF dir, double lift) {
    double rad = std::atan2(dir.y(), dir.x());
    if (rad > kPi / 2) rad -= kPi;
    if (rad <= -kPi / 2) rad += kPi;
    const QPointF center = at + QPointF(std::sin(rad), -std::cos(rad)) * (lift + 8);
    const double c = std::abs(std::cos(rad)), s = std::abs(std::sin(rad));
    const double w = 84 * c + 18 * s, h = 18 * c + 84 * s;
    return {center.x() - w / 2, center.y() - h / 2, w, h};
}

// Typing a value: Enter applies it, Escape puts it away.
class ValueEdit : public QLineEdit {
public:
    using QLineEdit::QLineEdit;

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            hide();
            if (parentWidget()) parentWidget()->setFocus();
            return;
        }
        QLineEdit::keyPressEvent(e);
    }
};

QColor roomColor(RoomType t) {
    switch (t) {
    case RoomType::Bedroom: return QColor(222, 232, 247);
    case RoomType::Kitchen: return QColor(250, 240, 214);
    case RoomType::Bath: return QColor(214, 240, 240);
    case RoomType::Garage: return QColor(232, 232, 232);
    case RoomType::Deck:
    case RoomType::Balcony: return QColor(236, 226, 210);
    default: return QColor(238, 245, 230);
    }
}

// Green at light load through amber to red at the heaviest load in view.
QColor loadColor(double fraction) {
    fraction = std::clamp(fraction, 0.0, 1.0);
    const QColor lo(46, 160, 67), mid(230, 160, 20), hi(214, 40, 40);
    auto mix = [](QColor a, QColor b, double t) {
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t);
    };
    return fraction < 0.5 ? mix(lo, mid, fraction * 2) : mix(mid, hi, (fraction - 0.5) * 2);
}

} // namespace

namespace {

// Dimensions on the same line as `id`'s dimension line: one string, which
// moves in and out together.
std::vector<ObjectId> dimensionString(const Document& d, ObjectId id) {
    const Dimension* base = d.dimension(id);
    if (!base || base->length() < 1e-6) return {};
    const Vec2 dir = (base->b - base->a).normalized();
    const Vec2 origin = base->a + dir.perp() * base->offset;
    std::vector<ObjectId> out;
    for (const auto& x : d.dimensions) {
        if (x.levelId != base->levelId || x.length() < 1e-6) continue;
        const Vec2 xd = (x.b - x.a).normalized();
        if (std::abs(cross(xd, dir)) > 1e-3) continue;
        const Vec2 xa = x.a + xd.perp() * x.offset;
        if (std::abs(cross(xa - origin, dir)) > 0.5) continue;
        out.push_back(x.id);
    }
    return out;
}

// Moves objects by `delta`. Openings ride along with their walls, and rooms
// follow the walls on the next refresh.
void moveObjects(Document& d, const std::vector<ObjectId>& ids, Vec2 delta) {
    for (ObjectId id : ids) {
        if (Wall* w = d.wall(id)) {
            w->a += delta;
            w->b += delta;
        } else if (Beam* b = d.beam(id)) {
            b->a += delta;
            b->b += delta;
        } else if (RoofPlane* r = d.roofPlane(id)) {
            r->a += delta;
            r->b += delta;
            for (Vec2& p : r->outline) p += delta;
        } else if (Roof* r = d.roof(id)) {
            r->footprint.min += delta;
            r->footprint.max += delta;
        } else if (FloorPlatform* f = d.platform(id)) {
            f->area.min += delta;
            f->area.max += delta;
        } else if (Post* p = d.post(id)) {
            p->position += delta;
        } else if (ElevationPoint* e = d.elevationPoint(id)) {
            e->position += delta;
        } else if (Dimension* x = d.dimension(id)) {
            x->a += delta;
            x->b += delta;
            x->automatic = false;
        }
    }
}

bool wallIsJoined(const Document& d, const Wall& w) {
    for (const auto& v : d.walls)
        if (v.id != w.id && v.levelId == w.levelId)
            for (Vec2 end : {v.a, v.b})
                if (distance(end, w.a) <= 0.5 || distance(end, w.b) <= 0.5) return true;
    return false;
}

// Moves the selection the way Chief does: one wall joined to others slides
// square to itself and the walls at its ends stretch to follow; one opening
// slides along its wall; anything else moves as it is.
void shiftObjects(Document& d, const std::vector<ObjectId>& ids, Vec2 delta) {
    if (ids.size() == 1) {
        if (const Wall* w = d.wall(ids.front())) {
            if (wallIsJoined(d, *w) && w->length() > 1e-9) {
                const Vec2 n = (w->b - w->a).normalized().perp();
                delta = n * dot(delta, n);
            }
            setWallEnds(d, w->id, w->a + delta, w->b + delta);
            return;
        }
        if (Opening* o = d.opening(ids.front())) {
            if (const Wall* w = d.wall(o->wallId); w && w->length() > o->width)
                o->offset = std::clamp(o->offset + dot(delta, (w->b - w->a).normalized()), o->width / 2,
                                       w->length() - o->width / 2);
            return;
        }
    }
    moveObjects(d, ids, delta);
}

// How far an object is from a point, for choosing which end of a dimension
// it sits at.
double distanceToObject(const Document& d, ObjectId id, Vec2 p) {
    if (const Wall* w = d.wall(id)) return w->centerline().distanceTo(p);
    if (const Beam* b = d.beam(id)) return b->centerline().distanceTo(p);
    if (const Opening* o = d.opening(id))
        if (const Wall* w = d.wall(o->wallId)) return distance(w->a + (w->b - w->a).normalized() * o->offset, p);
    if (const Post* q = d.post(id)) return distance(q->position, p);
    if (const ElevationPoint* e = d.elevationPoint(id)) return distance(e->position, p);
    if (const RoofPlane* r = d.roofPlane(id)) return r->outline.empty() ? 1e30 : distance(centroid(r->outline), p);
    return 1e30;
}

} // namespace

PlanView::PlanView(Workspace* ws, QWidget* parent) : QWidget(parent), ws_(ws) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    valueEdit_ = new ValueEdit(this);
    valueEdit_->hide();
    valueEdit_->setAlignment(Qt::AlignCenter);
    valueEdit_->setToolTip("Type a length, like 12' 6\" or 150, then Enter");
    connect(valueEdit_, &QLineEdit::returnPressed, this, &PlanView::commitValue);
    connect(valueEdit_, &QLineEdit::editingFinished, this, [this] {
        if (!valueEdit_->hasFocus()) valueEdit_->hide();
    });
    connect(ws_, &Workspace::documentChanged, this, [this] {
        dimensionSnapsValid_ = false;
        update();
    });
    connect(ws_, &Workspace::selectionChanged, this, [this] {
        // A selection made elsewhere (the 3D view, the loads table) ends the stack.
        if (std::find(stack_.begin(), stack_.end(), ws_->selection()) == stack_.end()) {
            stack_.clear();
            stackHint_.clear();
        }
        update();
    });
    connect(ws_, &Workspace::currentLevelChanged, this, [this] {
        dimensionSnapsValid_ = false;
        anchor_.reset();
        anchor2_.reset();
        update();
    });
}

const Document& PlanView::doc() const { return preview_ ? *preview_ : ws_->doc(); }

void PlanView::setTool(Tool tool) {
    tool_ = tool;
    valueEdit_->hide();
    editing_.reset();
    anchor_.reset();
    anchor2_.reset();
    joinEdge_.reset();
    drag_ = Drag::None;
    preview_.reset();
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    emit toolChanged(tool);
    static const char* hints[] = {
        "Click to select; click again, Tab or right-click picks what's underneath. Drag to move; joined walls stretch. "
        "Click a dimension number to type a new one. Arrows nudge 1\" (Shift 1'). Double-click or Ctrl+E for properties.",
        "Click to start a wall, click again to end it. Keep clicking to chain walls; right-click or Esc to stop. "
        "Type a length and Enter, or Tab for length and angle. Ctrl turns off snapping. Space goes back to Select.",
        "Click on a wall to place a door.",
        "Click on a wall to place a window.",
        "Click the two ends of a beam. It carries what sits on it from the level above.",
        "Drag a rectangle for the floor framing of this level. Joists run the short way.",
        "Drag a rectangle over the bearing walls for a roof. The ridge runs the long way.",
        "Click to start a room divider, an invisible wall that only separates rooms. Rooms fill the areas walls enclose.",
        "Click the two ends of the eave bearing line (usually the wall), then click on the high side to set how far the plane runs uphill.",
        "Click the edge of a roof plane to change, then click the roof plane it should meet. The edge moves to the hip, valley or ridge line.",
        "Click to place a post, for example under a porch beam end. It snaps to beam and wall ends.",
        "Click the two points to dimension, then click where the dimension line goes. Type a length after the first click to set it.",
        "Click to place a spot elevation on the site, then set its height in Properties. Contours run between the points.",
    };
    emit statusMessage(hints[static_cast<int>(tool)]);
    update();
}

void PlanView::setOpeningSize(OpeningKind kind, double width, double height, double sill) {
    (kind == OpeningKind::Door ? doorSize_ : windowSize_) = {width, height, sill};
}

void PlanView::setShowOpeningSizes(bool show) {
    showOpeningSizes_ = show;
    update();
}

void PlanView::setShowLoads(bool show) {
    showLoads_ = show;
    update();
}

QPointF PlanView::toScreen(Vec2 p) const { return {p.x * scale_ + offset_.x(), -p.y * scale_ + offset_.y()}; }

Vec2 PlanView::toWorld(QPointF p) const { return {(p.x() - offset_.x()) / scale_, -(p.y() - offset_.y()) / scale_}; }

void PlanView::zoomToFit() {
    Rect box{{1e30, 1e30}, {-1e30, -1e30}};
    auto grow = [&box](Vec2 p) {
        box.min = {std::min(box.min.x, p.x), std::min(box.min.y, p.y)};
        box.max = {std::max(box.max.x, p.x), std::max(box.max.y, p.y)};
    };
    for (const auto& w : doc().walls) {
        grow(w.a);
        grow(w.b);
    }
    for (const auto& r : doc().roofs) {
        grow(r.footprint.min - Vec2{r.overhang, r.overhang});
        grow(r.footprint.max + Vec2{r.overhang, r.overhang});
    }
    for (const auto& r : doc().roofPlanes)
        for (Vec2 c : r.outline) grow(c);
    if (box.min.x > box.max.x) box = {{0, 0}, {feet(40), feet(30)}};
    const double margin = 48.0;
    const double sx = width() / (box.width() + 2 * margin);
    const double sy = height() / (box.height() + 2 * margin);
    scale_ = std::clamp(std::min(sx, sy), 0.02, 40.0);
    const Vec2 c = box.center();
    offset_ = QPointF(width() / 2.0 - c.x * scale_, height() / 2.0 + c.y * scale_);
    update();
}

// ---- Snapping and picking ------------------------------------------------

Vec2 PlanView::snap(Vec2 p, std::optional<Vec2> from, Qt::KeyboardModifiers mods) const {
    if (mods & Qt::ControlModifier) return p;
    const double tol = kSnapPixels / scale_;
    double best = tol;
    std::optional<Vec2> hit;
    auto consider = [&](Vec2 q) {
        const double d = distance(p, q);
        if (d < best) {
            best = d;
            hit = q;
        }
    };
    const Level* below = doc().levelBelow(ws_->currentLevel());
    for (const auto& w : doc().walls) {
        if (w.levelId != ws_->currentLevel() && !(below && w.levelId == below->id)) continue;
        consider(w.a);
        consider(w.b);
    }
    for (const auto& b : doc().beams) {
        if (b.levelId != ws_->currentLevel()) continue;
        consider(b.a);
        consider(b.b);
    }
    for (const auto& r : doc().roofPlanes) {
        if (r.levelId != ws_->currentLevel()) continue;
        for (size_t i = 0; i < r.outline.size(); ++i)
            if (!(r.id == ws_->selection() && static_cast<int>(i) == dragIndex_ && drag_ == Drag::Vertex))
                consider(r.outline[i]);
    }
    for (const auto& p : doc().posts)
        if (p.levelId == ws_->currentLevel() && p.id != ws_->selection()) consider(p.position);
    if (hit) return *hit;

    if (from) {
        const Vec2 d = p - *from;
        const double len = std::round(d.length());
        if (len < 1.0) return *from;
        const double angle = std::round(std::atan2(d.y, d.x) / kAngleStep) * kAngleStep;
        Vec2 r = *from + Vec2{std::cos(angle), std::sin(angle)} * len;
        return {std::round(r.x * 1e6) / 1e6, std::round(r.y * 1e6) / 1e6};
    }
    return {std::round(p.x), std::round(p.y)};
}

const Wall* PlanView::wallNear(Vec2 p, double pixelTolerance, bool skipDividers) const {
    const Wall* best = nullptr;
    double bestDist = 1e30;
    for (const auto& w : doc().walls) {
        if (w.levelId != ws_->currentLevel() || (skipDividers && w.roomDivider)) continue;
        const double d = w.centerline().distanceTo(p);
        if (d <= w.thickness / 2.0 + pixelTolerance / scale_ && d < bestDist) {
            best = &w;
            bestDist = d;
        }
    }
    return best;
}

std::optional<std::pair<ObjectId, int>> PlanView::roofEdgeNear(Vec2 p) const {
    std::optional<std::pair<ObjectId, int>> best;
    double bestDist = kPickPixels / scale_;
    for (const auto& r : doc().roofPlanes) {
        if (r.levelId != ws_->currentLevel()) continue;
        for (size_t i = 0; i < r.outline.size(); ++i) {
            const double d = Segment{r.outline[i], r.outline[(i + 1) % r.outline.size()]}.distanceTo(p);
            if (d <= bestDist) {
                bestDist = d;
                best = {r.id, static_cast<int>(i)};
            }
        }
    }
    return best;
}

std::optional<int> PlanView::roofVertexAt(QPointF pos) const {
    const RoofPlane* r = doc().roofPlane(ws_->selection());
    if (!r || ws_->selectedIds().size() != 1) return std::nullopt;
    for (size_t i = 0; i < r->outline.size(); ++i)
        if (QLineF(toScreen(r->outline[i]), pos).length() <= kPickPixels) return static_cast<int>(i);
    return std::nullopt;
}

std::optional<int> PlanView::roofEdgeHandleAt(QPointF pos) const {
    const RoofPlane* r = doc().roofPlane(ws_->selection());
    if (!r || ws_->selectedIds().size() != 1) return std::nullopt;
    const size_t n = r->outline.size();
    for (size_t i = 0; i < n; ++i)
        if (QLineF(toScreen((r->outline[i] + r->outline[(i + 1) % n]) * 0.5), pos).length() <= kPickPixels)
            return static_cast<int>(i);
    return std::nullopt;
}

std::vector<ObjectId> PlanView::pickAll(Vec2 p) const {
    const ObjectId level = ws_->currentLevel();
    const double tol = kPickPixels / scale_;
    std::vector<ObjectId> hits;

    for (const auto& d : doc().dimensions) {
        if (d.levelId != level || d.length() < 1e-6) continue;
        const Vec2 n = (d.b - d.a).normalized().perp() * d.offset;
        const Segment line{d.a + n, d.b + n};
        // The line itself, or the length written over its middle.
        const double t = line.project(p);
        if (line.distanceTo(p) <= tol || (t > 0.2 && t < 0.8 && line.distanceTo(p) <= 2.5 * tol))
            hits.push_back(d.id);
    }
    if (const Level* ground = doc().groundLevel(); ground && ground->id == level)
        for (const auto& e : doc().terrain.points)
            if (distance(e.position, p) <= tol) hits.push_back(e.id);
    for (const auto& post : doc().posts) {
        const auto* m = structural::findPost(post.memberId);
        const double half = (m ? m->d : 3.5) / 2.0 + tol;
        if (post.levelId == level && std::abs(p.x - post.position.x) <= half && std::abs(p.y - post.position.y) <= half)
            hits.push_back(post.id);
    }

    for (const auto& b : doc().beams)
        if (b.levelId == level && b.centerline().distanceTo(p) <= 3.0 + tol) hits.push_back(b.id);
    for (const auto& o : doc().openings) {
        const Wall* w = doc().wall(o.wallId);
        if (!w || w->levelId != level) continue;
        const Segment line = w->centerline();
        const double along = line.project(p) * w->length();
        if (line.distanceTo(p) <= w->thickness / 2.0 + tol && std::abs(along - o.offset) <= o.width / 2.0)
            hits.push_back(o.id);
    }
    for (const auto& w : doc().walls)
        if (w.levelId == level && w.centerline().distanceTo(p) <= w.thickness / 2.0 + tol) hits.push_back(w.id);
    // Roofs are picked near their edges so the rooms under them stay easy to click.
    auto nearEdges = [&](const Polygon& c, double band) {
        for (size_t i = 0; i < c.size(); ++i)
            if (Segment{c[i], c[(i + 1) % c.size()]}.distanceTo(p) <= band) return true;
        return false;
    };
    for (const auto& r : doc().roofPlanes) {
        if (r.levelId != level || r.outline.size() < 3) continue;
        if (nearEdges(r.outline, tol) || (contains(r.outline, p) && nearEdges(r.outline, 18.0 + tol)))
            hits.push_back(r.id);
    }
    for (const auto& r : doc().roofs) {
        if (r.levelId != level) continue;
        const Polygon c = r.footprint.corners();
        for (size_t i = 0; i < 4; ++i)
            if (Segment{c[i], c[(i + 1) % 4]}.distanceTo(p) <= r.overhang + tol &&
                !Rect{r.footprint.min + Vec2{r.overhang, r.overhang}, r.footprint.max - Vec2{r.overhang, r.overhang}}
                     .contains(p)) {
                hits.push_back(r.id);
                break;
            }
    }
    for (const auto& f : doc().platforms)
        if (f.levelId == level && nearEdges(f.area.corners(), tol * 1.5)) hits.push_back(f.id);
    for (const auto& r : doc().rooms)
        if (r.levelId == level && contains(r.outline, p)) hits.push_back(r.id);
    return hits;
}

ObjectId PlanView::pick(Vec2 p) const {
    const auto hits = pickAll(p);
    return hits.empty() ? kNoId : hits.front();
}

void PlanView::selectFromStack(const std::vector<ObjectId>& hits, ObjectId id) {
    stack_ = hits;
    stackHint_.clear();
    if (hits.size() > 1) {
        const auto it = std::find(hits.begin(), hits.end(), id);
        stackHint_ = QString("%1 (%2 of %3 here). Click again or press Tab for the next one, or right-click for a list.")
                         .arg(ws_->describe(id))
                         .arg(it - hits.begin() + 1)
                         .arg(hits.size());
    }
    ws_->select(id);
    update();
}

void PlanView::cycleStack() {
    if (stack_.size() < 2) return;
    auto it = std::find(stack_.begin(), stack_.end(), ws_->selection());
    const size_t next = it == stack_.end() ? 0 : (static_cast<size_t>(it - stack_.begin()) + 1) % stack_.size();
    selectFromStack(stack_, stack_[next]);
}

void PlanView::showStackMenu(QPointF screenPos, const std::vector<ObjectId>& hits) {
    QMenu menu(this);
    menu.addSection("Select");
    for (ObjectId id : hits) {
        QAction* a = menu.addAction(ws_->describe(id));
        a->setCheckable(true);
        a->setChecked(id == ws_->selection());
        connect(a, &QAction::triggered, this, [this, hits, id] { selectFromStack(hits, id); });
    }
    menu.exec(mapToGlobal(screenPos.toPoint()));
}

// ---- Box selection and dimension nodes -------------------------------------

bool PlanView::toolAccepts(ObjectId id) const {
    const Document& d = doc();
    switch (tool_) {
    case Tool::Select: return true;
    case Tool::Wall: return d.wall(id) && !d.wall(id)->roomDivider;
    case Tool::RoomDivider: return d.wall(id) && d.wall(id)->roomDivider;
    case Tool::Door: return d.opening(id) && d.opening(id)->kind == OpeningKind::Door;
    case Tool::Window: return d.opening(id) && d.opening(id)->kind == OpeningKind::Window;
    case Tool::Beam: return d.beam(id) != nullptr;
    case Tool::Post: return d.post(id) != nullptr;
    case Tool::Floor: return Document::findIn(d.platforms, id) != nullptr;
    case Tool::Roof: return d.roof(id) != nullptr;
    case Tool::RoofPlane:
    case Tool::JoinPlanes: return d.roofPlane(id) != nullptr;
    case Tool::Dimension: return d.dimension(id) != nullptr;
    case Tool::Elevation: return d.elevationPoint(id) != nullptr;
    }
    return false;
}

std::vector<ObjectId> PlanView::boxPick(QPointF a, QPointF b) const {
    const Vec2 p = toWorld(a), q = toWorld(b);
    const Rect box{{std::min(p.x, q.x), std::min(p.y, q.y)}, {std::max(p.x, q.x), std::max(p.y, q.y)}};
    auto inside = [&](std::initializer_list<Vec2> pts) {
        return std::all_of(pts.begin(), pts.end(), [&](Vec2 v) { return box.contains(v); });
    };
    auto polyInside = [&](const Polygon& poly) {
        return !poly.empty() && std::all_of(poly.begin(), poly.end(), [&](Vec2 v) { return box.contains(v); });
    };
    const Document& d = doc();
    const ObjectId level = ws_->currentLevel();
    std::vector<ObjectId> out;
    auto take = [&](ObjectId id, bool in) {
        if (in && toolAccepts(id)) out.push_back(id);
    };
    for (const auto& w : d.walls)
        if (w.levelId == level) take(w.id, inside({w.a, w.b}));
    for (const auto& o : d.openings) {
        const Wall* w = d.wall(o.wallId);
        if (!w || w->levelId != level || w->length() < 1e-6) continue;
        const Vec2 dir = (w->b - w->a).normalized();
        take(o.id, inside({w->a + dir * (o.offset - o.width / 2.0), w->a + dir * (o.offset + o.width / 2.0)}));
    }
    for (const auto& b : d.beams)
        if (b.levelId == level) take(b.id, inside({b.a, b.b}));
    for (const auto& post : d.posts)
        if (post.levelId == level) take(post.id, inside({post.position}));
    for (const auto& r : d.roofPlanes)
        if (r.levelId == level) take(r.id, polyInside(r.outline));
    for (const auto& r : d.roofs)
        if (r.levelId == level) take(r.id, inside({r.footprint.min, r.footprint.max}));
    for (const auto& f : d.platforms)
        if (f.levelId == level) take(f.id, inside({f.area.min, f.area.max}));
    for (const auto& r : d.rooms)
        if (r.levelId == level) take(r.id, polyInside(r.outline));
    for (const auto& x : d.dimensions) {
        if (x.levelId != level || x.length() < 1e-6) continue;
        const Vec2 n = (x.b - x.a).normalized().perp() * x.offset;
        take(x.id, inside({x.a, x.b, x.a + n, x.b + n}));
    }
    if (const Level* ground = d.groundLevel(); ground && ground->id == level)
        for (const auto& e : d.terrain.points) take(e.id, inside({e.position}));
    return out;
}

void PlanView::finishBox(QPointF a, QPointF b, bool add) {
    const bool click = QLineF(a, b).length() < 4.0;
    std::vector<ObjectId> ids = add || click ? ws_->selectedIds() : std::vector<ObjectId>{};
    if (click) {
        // Shift-click adds or removes the topmost object here the tool accepts.
        for (ObjectId id : pickAll(toWorld(b))) {
            if (!toolAccepts(id)) continue;
            if (auto it = std::find(ids.begin(), ids.end(), id); it != ids.end()) ids.erase(it);
            else ids.push_back(id);
            break;
        }
    } else {
        for (ObjectId id : boxPick(a, b))
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    stack_.clear();
    stackHint_.clear();
    ws_->selectMany(ids);
    if (ids.size() > 1)
        emit statusMessage(QString("%1 selected. Edit them together in Properties, drag one to move them all, or press "
                                   "Delete.").arg(ids.size()));
    else if (ids.empty() && !click)
        emit statusMessage(tool_ == Tool::Select ? QString("Nothing lies entirely inside the box.")
                                                 : QString("Nothing of this tool's kind lies entirely inside the box."));
}

std::optional<Vec2> PlanView::dimensionSnap(Vec2 p, Qt::KeyboardModifiers mods) const {
    if (mods & Qt::ControlModifier) return std::nullopt;
    if (!dimensionSnapsValid_) {
        dimensionSnaps_ = dimensionSnapPoints(ws_->doc(), ws_->currentLevel());
        dimensionSnapsValid_ = true;
    }
    double best = kSnapPixels / scale_;
    std::optional<Vec2> hit;
    for (Vec2 q : dimensionSnaps_)
        if (const double dist = distance(p, q); dist < best) {
            best = dist;
            hit = q;
        }
    return hit;
}

std::optional<PlanView::Drag> PlanView::dimensionHandleAt(QPointF pos) const {
    const Dimension* x = doc().dimension(ws_->selection());
    if (!x || ws_->selectedIds().size() != 1 || x->levelId != ws_->currentLevel()) return std::nullopt;
    if (QLineF(toScreen(x->a), pos).length() <= kPickPixels) return Drag::DimA;
    if (QLineF(toScreen(x->b), pos).length() <= kPickPixels) return Drag::DimB;
    return std::nullopt;
}

void PlanView::drawBox(QPainter& g) const {
    if (drag_ != Drag::Box) return;
    const QRectF r = QRectF(boxStart_, boxEnd_).normalized();
    g.setPen(QPen(QColor(30, 110, 220), 1, Qt::DashLine));
    g.setBrush(QColor(30, 110, 220, 30));
    g.drawRect(r);
    if (tool_ != Tool::Select) {
        static const char* kinds[] = {"", "walls", "doors", "windows", "beams", "floor framing", "roofs", "room dividers",
                                      "roof planes", "roof planes", "posts", "dimensions", "elevation points"};
        g.setPen(QColor(30, 110, 220));
        g.drawText(boxEnd_ + QPointF(12, -10), QString("Only %1").arg(kinds[static_cast<int>(tool_)]));
    }
}

// ---- Painting --------------------------------------------------------------

void PlanView::paintEvent(QPaintEvent*) {
    if (!fitted_ && width() > 0) {
        fitted_ = true;
        zoomToFit();
    }
    QPainter g(this);
    g.fillRect(rect(), Qt::white);
    valueHits_.clear();
    g.setRenderHint(QPainter::Antialiasing);

    drawGrid(g);
    drawTerrain(g);
    drawLevelBelow(g);
    drawRooms(g);
    drawPlatforms(g);
    drawWalls(g);
    drawOpenings(g);
    drawBeams(g);
    drawRoofs(g);
    drawRoofPlanes(g);
    drawPosts(g);
    drawDimensions(g);
    drawTemporaryDimensions(g);
    if (showLoads_) drawLoads(g);
    drawToolPreview(g);
    drawTyped(g);
    drawBox(g);

    const Level* level = doc().level(ws_->currentLevel());
    g.setPen(QColor(90, 90, 90));
    g.drawText(QRectF(10, 6, width() - 20, 20), Qt::AlignLeft,
               level ? QString::fromStdString(level->name) : QString());
    if (!stackHint_.isEmpty()) {
        // At the bottom, clear of the dimensions that usually run along the top.
        const QRectF box(10, height() - 32, std::min(width() - 20.0, 640.0), 22);
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(30, 110, 220, 230));
        g.drawRoundedRect(box, 4, 4);
        g.setPen(Qt::white);
        g.drawText(box.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, stackHint_);
    }
}

void PlanView::drawGrid(QPainter& g) const {
    double step = 12.0; // one foot
    while (pixels(step) < 10.0) step *= 4.0;
    const Vec2 lo = toWorld(QPointF(0, height()));
    const Vec2 hi = toWorld(QPointF(width(), 0));
    const QPen minor(QColor(238, 238, 238), 0), major(QColor(220, 220, 220), 0);
    for (double x = std::floor(lo.x / step) * step; x <= hi.x; x += step) {
        g.setPen(std::fmod(std::abs(x), step * 4) < 1e-6 ? major : minor);
        g.drawLine(toScreen({x, lo.y}), toScreen({x, hi.y}));
    }
    for (double y = std::floor(lo.y / step) * step; y <= hi.y; y += step) {
        g.setPen(std::fmod(std::abs(y), step * 4) < 1e-6 ? major : minor);
        g.drawLine(toScreen({lo.x, y}), toScreen({hi.x, y}));
    }
}

void PlanView::drawLevelBelow(QPainter& g) const {
    const Level* below = doc().levelBelow(ws_->currentLevel());
    if (!below) return;
    QPen pen(QColor(190, 190, 190), 0, Qt::DashLine);
    g.setPen(pen);
    g.setBrush(Qt::NoBrush);
    for (const auto& w : doc().walls) {
        if (w.levelId != below->id) continue;
        const Vec2 n = (w.b - w.a).normalized().perp() * (w.thickness / 2.0);
        g.drawLine(toScreen(w.a + n), toScreen(w.b + n));
        g.drawLine(toScreen(w.a - n), toScreen(w.b - n));
    }
}

void PlanView::drawRooms(QPainter& g) const {
    for (const auto& r : doc().rooms) {
        if (r.levelId != ws_->currentLevel() || r.outline.size() < 3) continue;
        QPolygonF poly;
        for (auto p : r.outline) poly << toScreen(p);
        const bool selected = ws_->isSelected(r.id);
        g.setPen(selected ? QPen(QColor(30, 110, 220), 2) : Qt::NoPen);
        g.setBrush(roomColor(r.type));
        g.drawPolygon(poly);

        const bool ownFloor = r.floor.kind != FloorSystemKind::LevelFraming;
        const QPointF c = toScreen(centroid(r.outline)) - QPointF(0, ownFloor ? 9 : 0);
        const double sqft = area(r.outline) / kSqInPerSqFt;
        g.setPen(QColor(60, 60, 60));
        g.drawText(QRectF(c.x() - 80, c.y() - 18, 160, 18), Qt::AlignCenter, QString::fromStdString(r.name));
        g.setPen(QColor(120, 120, 120));
        g.drawText(QRectF(c.x() - 80, c.y(), 160, 18), Qt::AlignCenter, QString("%1 sq ft").arg(sqft, 0, 'f', 0));
        if (ownFloor) {
            if (r.floor.kind == FloorSystemKind::Slab) {
                g.setPen(Qt::NoPen);
                g.setBrush(QBrush(QColor(150, 150, 150, 110), Qt::Dense6Pattern));
                g.drawPolygon(poly);
            }
            QString floor = r.floor.kind == FloorSystemKind::Slab
                                ? QString("%1\" slab").arg(r.floor.slabThickness)
                                : QString::fromStdString(r.floor.joists);
            if (r.floor.drop > 1e-6)
                floor += QString(", %1\" down").arg(r.floor.drop);
            g.setPen(QColor(90, 90, 90));
            QFont italic = g.font();
            italic.setItalic(true);
            g.save();
            g.setFont(italic);
            g.drawText(QRectF(c.x() - 100, c.y() + 16, 200, 18), Qt::AlignCenter, floor);
            g.restore();
        }
    }
}

void PlanView::drawPlatforms(QPainter& g) const {
    for (const auto& f : doc().platforms) {
        if (f.levelId != ws_->currentLevel()) continue;
        const bool selected = ws_->isSelected(f.id);
        QPen pen(selected ? QColor(30, 110, 220) : QColor(90, 140, 200), selected ? 2 : 1, Qt::DashDotLine);
        g.setPen(pen);
        g.setBrush(Qt::NoBrush);
        g.drawRect(QRectF(toScreen({f.area.min.x, f.area.max.y}), toScreen({f.area.max.x, f.area.min.y})));

        // Joist direction arrow near the corner.
        const Vec2 c = f.area.min + Vec2{36, 36} / std::max(scale_ / 2.0, 1.0);
        const Vec2 dir = f.joistAxis == Axis::Y ? Vec2{0, 1} : Vec2{1, 0};
        const QPointF a = toScreen(c), b = toScreen(c + dir * (48.0 / std::max(scale_, 0.25)));
        g.drawLine(a, b);
        g.drawText(b + QPointF(6, -4), QString("Floor joists, %1 psf D + %2 psf L").arg(f.deadPsf).arg(f.livePsf));
    }
}

void PlanView::drawWalls(QPainter& g) const {
    const auto& d = doc();
    auto connected = [&](const Wall& self, Vec2 end) {
        for (const auto& w : d.walls)
            if (w.id != self.id && w.levelId == self.levelId && (distance(w.a, end) < 1.0 || distance(w.b, end) < 1.0))
                return true;
        return false;
    };
    for (const auto& w : d.walls) {
        if (w.levelId != ws_->currentLevel() || w.length() < 1e-6) continue;
        if (w.roomDivider) {
            g.setPen(QPen(QColor(120, 140, 170), 1.5, Qt::DashLine));
            g.drawLine(toScreen(w.a), toScreen(w.b));
            continue;
        }
        const Vec2 dir = (w.b - w.a).normalized();
        const Vec2 n = dir.perp() * (w.thickness / 2.0);
        // Extend into corners so joined walls read as one solid outline.
        const Vec2 a = connected(w, w.a) ? w.a - dir * (w.thickness / 2.0) : w.a;
        const Vec2 b = connected(w, w.b) ? w.b + dir * (w.thickness / 2.0) : w.b;
        QPolygonF poly;
        poly << toScreen(a + n) << toScreen(b + n) << toScreen(b - n) << toScreen(a - n);
        const QColor fill = w.bearing ? QColor(70, 70, 70) : QColor(150, 150, 150);
        g.setPen(QPen(fill, 0));
        g.setBrush(fill);
        g.drawPolygon(poly);
    }
    // Selection drawn on top so it is not hidden by a neighbor.
    const bool single = ws_->selectedIds().size() == 1;
    for (ObjectId id : ws_->selectedIds()) {
        const Wall* w = d.wall(id);
        if (!w || w->levelId != ws_->currentLevel()) continue;
        const Vec2 n = (w->b - w->a).normalized().perp() * (std::max(w->thickness, 2.0) / 2.0 + 1.0 / scale_);
        QPolygonF poly;
        poly << toScreen(w->a + n) << toScreen(w->b + n) << toScreen(w->b - n) << toScreen(w->a - n);
        g.setPen(QPen(QColor(30, 110, 220), 2));
        g.setBrush(QColor(30, 110, 220, 60));
        g.drawPolygon(poly);
        if (!single) continue;
        for (Vec2 end : {w->a, w->b}) g.drawEllipse(toScreen(end), 4, 4);
        const QRectF label = drawLengthLabel(g, w->a + n * 4.0, w->b + n * 4.0);
        if (tool_ == Tool::Select) {
            const ObjectId wallId = w->id;
            Workspace* ws = ws_;
            addValueHit(label, w->length(), "Change wall length", [wallId, ws](Document& doc, double v) {
                if (v >= 1.0) setWallLength(doc, wallId, v, ws->wallLengthAnchor());
            });
        }
    }
}

void PlanView::drawOpenings(QPainter& g) const {
    for (const auto& o : doc().openings) {
        const Wall* w = doc().wall(o.wallId);
        if (!w || w->levelId != ws_->currentLevel() || w->length() < 1e-6) continue;
        const Vec2 dir = (w->b - w->a).normalized();
        const Vec2 n = dir.perp() * (w->thickness / 2.0 + 0.5 / scale_);
        const Vec2 s0 = w->a + dir * (o.offset - o.width / 2.0);
        const Vec2 s1 = w->a + dir * (o.offset + o.width / 2.0);

        QPolygonF gap;
        gap << toScreen(s0 + n) << toScreen(s1 + n) << toScreen(s1 - n) << toScreen(s0 - n);
        g.setPen(Qt::NoPen);
        g.setBrush(Qt::white);
        g.drawPolygon(gap);

        const bool selected = ws_->isSelected(o.id);
        g.setPen(QPen(selected ? QColor(30, 110, 220) : QColor(40, 40, 40), selected ? 2 : 1));
        g.setBrush(Qt::NoBrush);
        g.drawLine(toScreen(s0 + n), toScreen(s0 - n));
        g.drawLine(toScreen(s1 + n), toScreen(s1 - n));
        if (o.kind == OpeningKind::Window) {
            g.drawLine(toScreen(s0), toScreen(s1));
            g.drawLine(toScreen(s0 + n * 0.5), toScreen(s1 + n * 0.5));
            g.drawLine(toScreen(s0 - n * 0.5), toScreen(s1 - n * 0.5));
        } else {
            // Leaf drawn open 90 degrees to the interior side (left of the wall direction).
            const Vec2 hinge = s0 + n;
            const Vec2 leaf = hinge + dir.perp() * o.width;
            g.drawLine(toScreen(hinge), toScreen(leaf));
            const double r = pixels(o.width);
            const QPointF c = toScreen(hinge);
            const double startDeg = std::atan2(dir.y, dir.x) * 180.0 / kPi;
            g.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), int(startDeg * 16), int(90 * 16));
        }
        if (selected && ws_->selectedIds().size() == 1) drawLengthLabel(g, s0 + n * 3.0, s1 + n * 3.0);
        if (showOpeningSizes_ && !(selected && ws_->selectedIds().size() == 1)) {
            // On the outside of the wall, clear of the door swing.
            const Vec2 outside = wallOutside(doc(), *w);
            const Vec2 mid = (s0 + s1) * 0.5 + outside * (w->thickness / 2.0);
            g.setPen(QColor(40, 40, 120));
            QFont f = g.font();
            const QFont old = f;
            f.setPointSizeF(std::max(6.5, old.pointSizeF() - 1.0));
            g.setFont(f);
            const QPointF a = toScreen(mid), b = toScreen(mid + dir);
            QPointF out = toScreen(mid + outside) - a;
            const double len = std::hypot(out.x(), out.y());
            if (len > 1e-9) out /= len;
            drawAlong(g, a + out * 10.0, b - a, sizeCallout(o.width, o.height), -8.0);
            g.setFont(old);
        }
    }
}

void PlanView::drawBeams(QPainter& g) const {
    Vec2 lo{1e30, 1e30}, hi{-1e30, -1e30};
    for (const auto& w : doc().walls)
        if (w.levelId == ws_->currentLevel())
            for (Vec2 p : {w.a, w.b}) {
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
            }
    const Vec2 middle = lo.x <= hi.x ? (lo + hi) * 0.5 : Vec2{};
    for (const auto& b : doc().beams) {
        if (b.levelId != ws_->currentLevel()) continue;
        const bool selected = ws_->isSelected(b.id);
        QPen pen(selected ? QColor(30, 110, 220) : QColor(170, 100, 30), std::max(3.0, pixels(3.5)), Qt::DashLine);
        pen.setCapStyle(Qt::FlatCap);
        g.setPen(pen);
        g.drawLine(toScreen(b.a), toScreen(b.b));

        const auto* member = structural::findMember(b.memberId);
        const QString label = member ? QString::fromStdString(member->name) : QString("Beam, not sized");
        // The label goes on the side of the beam away from the middle of the
        // level, so a porch beam's label sits outside the porch.
        const QPointF mid = toScreen((b.a + b.b) * 0.5);
        const bool below = mid.y() > toScreen(middle).y();
        g.setPen(QColor(140, 80, 20));
        g.drawText(mid + QPointF(6, below ? 18 : -8), label);
        if (selected && ws_->selectedIds().size() == 1) drawLengthLabel(g, b.a, b.b);
    }
}

void PlanView::drawRoofs(QPainter& g) const {
    for (const auto& r : doc().roofs) {
        if (r.levelId != ws_->currentLevel()) continue;
        const bool selected = ws_->isSelected(r.id);
        const QColor color = selected ? QColor(30, 110, 220) : QColor(150, 60, 60);
        const Rect& fp = r.footprint;
        const Rect eaves{fp.min - Vec2{r.overhang, r.overhang}, fp.max + Vec2{r.overhang, r.overhang}};
        auto rectOf = [this](const Rect& rc) { return QRectF(toScreen({rc.min.x, rc.max.y}), toScreen({rc.max.x, rc.min.y})); };

        g.setBrush(Qt::NoBrush);
        g.setPen(QPen(color, selected ? 2 : 1, Qt::DashLine));
        g.drawRect(rectOf(eaves));

        g.setPen(QPen(color, selected ? 2 : 1));
        const Vec2 c = eaves.center();
        const bool ridgeX = r.ridgeAxis == Axis::X;
        const double half = (ridgeX ? eaves.height() : eaves.width()) / 2.0;
        if (r.style == RoofStyle::Gable) {
            if (ridgeX) g.drawLine(toScreen({eaves.min.x, c.y}), toScreen({eaves.max.x, c.y}));
            else g.drawLine(toScreen({c.x, eaves.min.y}), toScreen({c.x, eaves.max.y}));
        } else {
            Vec2 r0, r1;
            if (ridgeX) {
                r0 = {std::min(c.x, eaves.min.x + half), c.y};
                r1 = {std::max(c.x, eaves.max.x - half), c.y};
            } else {
                r0 = {c.x, std::min(c.y, eaves.min.y + half)};
                r1 = {c.x, std::max(c.y, eaves.max.y - half)};
            }
            g.drawLine(toScreen(r0), toScreen(r1));
            const Polygon corners = eaves.corners();
            for (const Vec2& k : corners) {
                const Vec2 end = distance(k, r0) < distance(k, r1) ? r0 : r1;
                g.drawLine(toScreen(k), toScreen(end));
            }
        }
        const QPointF labelAt = toScreen({eaves.min.x, eaves.max.y});
        g.drawText(labelAt + QPointF(4, -6),
                   QString("%1 roof, %2/12").arg(r.style == RoofStyle::Gable ? "Gable" : "Hip").arg(r.pitch));
    }
}

void PlanView::drawRoofPlanes(QPainter& g) const {
    // What each exterior wall tells Build Roof, like Chief's pitch
    // indicators: its pitch, or that it is a gable or the high side of a
    // shed. Shown once the level has an automatic roof or a wall has roof
    // settings of its own.
    const Document& d = doc();
    const ObjectId level = ws_->currentLevel();
    const bool hasAuto = std::any_of(d.roofPlanes.begin(), d.roofPlanes.end(),
                                     [&](const RoofPlane& r) { return r.automatic && r.levelId == level; });
    const bool hasSettings = std::any_of(d.walls.begin(), d.walls.end(), [&](const Wall& w) {
        return w.levelId == level && (w.roof.kind != WallRoof::Sloped || w.roof.pitch > 0 || w.roof.overhang >= 0);
    });
    if (hasAuto || hasSettings) {
        QFont small = g.font();
        small.setPointSizeF(small.pointSizeF() * 0.85);
        g.save();
        g.setFont(small);
        g.setPen(QColor(150, 60, 60));
        for (const auto& e : exteriorWallLoop(d, level)) {
            const Wall* w = d.wall(e.wallId);
            if (!w) continue;
            const double pitch = w->roof.pitch > 0 ? w->roof.pitch : d.roofDefaults.pitch;
            const QString text = w->roof.kind == WallRoof::FullGable  ? QString("Gable")
                                 : w->roof.kind == WallRoof::HighShed ? QString("High shed")
                                                                      : QString("%1/12").arg(pitch);
            // Just inside the wall, clear of the dimensions outside.
            const Vec2 inside = (e.b - e.a).normalized().perp() * (w->thickness / 2.0 + 14.0 / scale_);
            const QPointF a = toScreen(e.a + inside), b = toScreen(e.b + inside);
            drawAlong(g, (a + b) / 2.0, b - a, text, -8.0); // centered on the line
        }
        g.restore();
    }

    for (const auto& r : doc().roofPlanes) {
        if (r.levelId != ws_->currentLevel() || r.outline.size() < 3) continue;
        const bool selected = ws_->isSelected(r.id);
        const QColor color = selected ? QColor(30, 110, 220) : QColor(150, 60, 60);
        QPolygonF poly;
        for (Vec2 p : r.outline) poly << toScreen(p);
        g.setPen(QPen(color, selected ? 2 : 1, Qt::DashLine));
        g.setBrush(selected ? QColor(30, 110, 220, 30) : QColor(150, 60, 60, 18));
        g.drawPolygon(poly);
        // The bearing line along the eave wall or beam.
        g.setPen(QPen(color, selected ? 2 : 1));
        g.drawLine(toScreen(r.a), toScreen(r.b));

        // Arrow pointing downhill from the middle of the plane.
        const Vec2 up = r.upslope();
        const Vec2 mid = centroid(r.outline);
        const double arrow = std::min(r.runLength() * 0.3, 48.0);
        const Vec2 tip = mid - up * arrow;
        g.drawLine(toScreen(mid + up * arrow), toScreen(tip));
        const Vec2 along = r.along();
        const double head = std::min(arrow * 0.4, 10.0 / scale_ * 1.5);
        QPolygonF arrowHead;
        arrowHead << toScreen(tip) << toScreen(tip + up * head + along * head * 0.6) << toScreen(tip + up * head - along * head * 0.6);
        g.setBrush(color);
        g.drawPolygon(arrowHead);
        g.drawText(toScreen(mid + up * arrow) + QPointF(6, 0),
                   QString("%1/12%2").arg(r.pitch).arg(r.highBearing ? "" : ", to ridge"));

        if (!selected || ws_->selectedIds().size() != 1) continue;
        // Handles: squares at the corners, diamonds mid-edge, with each edge's length.
        const size_t n = r.outline.size();
        for (size_t i = 0; i < n; ++i) {
            const Vec2 p = r.outline[i], q = r.outline[(i + 1) % n];
            const Vec2 out = (q - p).normalized().perp() * (-16.0 / scale_);
            drawLengthLabel(g, p + out, q + out);
        }
        g.setPen(QPen(QColor(30, 110, 220), 1.5));
        g.setBrush(Qt::white);
        for (size_t i = 0; i < n; ++i) {
            const QPointF c = toScreen(r.outline[i]);
            g.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
            const QPointF m = toScreen((r.outline[i] + r.outline[(i + 1) % n]) * 0.5);
            QPolygonF diamond;
            diamond << m + QPointF(0, -5) << m + QPointF(5, 0) << m + QPointF(0, 5) << m + QPointF(-5, 0);
            g.drawPolygon(diamond);
        }
    }
}

void PlanView::drawPosts(QPainter& g) const {
    for (const auto& p : doc().posts) {
        if (p.levelId != ws_->currentLevel()) continue;
        const auto* m = structural::findPost(p.memberId);
        const double bx = (m ? m->b : 3.5) / 2.0, by = (m ? m->d : 3.5) / 2.0;
        const QRectF box(toScreen(p.position + Vec2{-bx, by}), toScreen(p.position + Vec2{bx, -by}));
        const QRectF shown = box.width() < 8 ? QRectF(box.center() - QPointF(4, 4), QSizeF(8, 8)) : box;
        const bool selected = ws_->isSelected(p.id);
        const QColor color = selected ? QColor(30, 110, 220) : QColor(140, 80, 20);
        g.setPen(QPen(color, selected ? 2 : 1.5));
        g.setBrush(QColor(235, 200, 150));
        g.drawRect(shown);
        g.drawLine(shown.topLeft(), shown.bottomRight());
        g.drawLine(shown.topRight(), shown.bottomLeft());
        if (selected || scale_ > 1.5)
            g.drawText(shown.topRight() + QPointF(4, -2), m ? QString::fromStdString(m->name).section(' ', 0, 0) : "Post");
    }
}

QRectF PlanView::drawDimension(QPainter& g, Vec2 a, Vec2 b, double offset, bool selected, bool temporary) const {
    if (distance(a, b) < 1e-6) return {};
    const Vec2 dir = (b - a).normalized(), n = dir.perp();
    const Vec2 da = a + n * offset, db = b + n * offset;
    const QColor color = temporary ? QColor(0, 128, 110) : selected ? QColor(30, 110, 220) : QColor(50, 50, 50);
    g.setPen(QPen(color, selected ? 1.5 : 1.0));
    // Extension lines start a little off the object and run just past the dimension line.
    const double gap = 3.0 / scale_, over = 4.0 / scale_;
    const double side = offset >= 0 ? 1.0 : -1.0;
    if (std::abs(offset) > gap) {
        g.drawLine(toScreen(a + n * side * gap), toScreen(da + n * side * over));
        g.drawLine(toScreen(b + n * side * gap), toScreen(db + n * side * over));
    }
    g.drawLine(toScreen(da), toScreen(db));
    // Architectural ticks: short slashes at 45 degrees.
    const Vec2 tick = (dir + n).normalized() * (4.0 / scale_);
    for (Vec2 p : {da, db}) g.drawLine(toScreen(p - tick), toScreen(p + tick));
    const QPointF sa = toScreen(da), sb = toScreen(db);
    const QRectF text = alongRect((sa + sb) / 2.0, sb - sa, 2.0);
    if (temporary) {
        // A backing so the number reads over room names and fills.
        g.save();
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(255, 255, 255, 220));
        g.drawRoundedRect(text, 3, 3);
        g.restore();
    }
    drawAlong(g, (sa + sb) / 2.0, sb - sa, lengthText(distance(a, b)), 2.0);
    return text;
}

void PlanView::drawDimensions(QPainter& g) const {
    // With something other than a dimension selected, clicking a dimension's
    // number and typing a new one moves the selection to suit.
    const auto& ids = ws_->selectedIds();
    const bool movesSelection =
        tool_ == Tool::Select && !ids.empty() &&
        std::none_of(ids.begin(), ids.end(), [this](ObjectId id) { return doc().dimension(id) != nullptr; });
    for (const auto& d : doc().dimensions) {
        if (d.levelId != ws_->currentLevel()) continue;
        const QRectF text = drawDimension(g, d.a, d.b, d.offset, ws_->isSelected(d.id));
        if (!movesSelection || d.length() < 1e-6) continue;
        const ObjectId dimId = d.id;
        const std::vector<ObjectId> moving = ids;
        addValueHit(text, d.length(), "Move to dimension", [dimId, moving](Document& doc, double v) {
            Dimension* x = doc.dimension(dimId);
            if (!x) return;
            // The end nearer the selection moves; the other stays put.
            double toA = 1e30, toB = 1e30;
            for (ObjectId id : moving) {
                toA = std::min(toA, distanceToObject(doc, id, x->a));
                toB = std::min(toB, distanceToObject(doc, id, x->b));
            }
            const bool movesA = toA < toB;
            const Vec2 fixed = movesA ? x->b : x->a, free = movesA ? x->a : x->b;
            const Vec2 delta = (free - fixed).normalized() * (v - x->length());
            // An automatic dimension stays automatic: it is redone to match.
            (movesA ? x->a : x->b) += delta;
            shiftObjects(doc, moving, delta);
        });
    }
    // Nodes of a selected dimension: squares on the points it measures, which
    // drag to other points, and a diamond on the line, which drags the string in or out.
    const Dimension* x = doc().dimension(ws_->selection());
    if (!x || ws_->selectedIds().size() != 1 || x->levelId != ws_->currentLevel() || x->length() < 1e-6) return;
    g.setPen(QPen(QColor(30, 110, 220), 1.5));
    g.setBrush(Qt::white);
    for (Vec2 p : {x->a, x->b}) {
        const QPointF c = toScreen(p);
        g.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
    }
    const Vec2 n = (x->b - x->a).normalized().perp() * x->offset;
    const QPointF m = toScreen((x->a + x->b) * 0.5 + n);
    QPolygonF diamond;
    diamond << m + QPointF(0, -6) << m + QPointF(6, 0) << m + QPointF(0, 6) << m + QPointF(-6, 0);
    g.drawPolygon(diamond);
}

void PlanView::drawTemporaryDimensions(QPainter& g) const {
    if (tool_ != Tool::Select || ws_->selectedIds().size() != 1) return;
    const Document& d = doc();
    const ObjectId sel = ws_->selection();
    if (const Wall* w = d.wall(sel); w && w->levelId == ws_->currentLevel() && w->length() > 1.0) {
        // To the nearest parallel wall on each side that runs alongside it.
        const Vec2 dir = (w->b - w->a).normalized(), n = dir.perp();
        const double len = w->length();
        struct Near {
            const Wall* wall = nullptr;
            double side = 0, along = 0;
        } nearest[2];
        for (const auto& v : d.walls) {
            if (v.id == w->id || v.levelId != w->levelId || v.length() < 1.0) continue;
            if (std::abs(cross((v.b - v.a).normalized(), dir)) > 1e-3) continue;
            const double t0 = dot(v.a - w->a, dir), t1 = dot(v.b - w->a, dir);
            const double lo = std::max(0.0, std::min(t0, t1)), hi = std::min(len, std::max(t0, t1));
            const double side = dot(v.a - w->a, n);
            if (hi - lo < 1.0 || std::abs(side) < 1.0) continue;
            Near& slot = nearest[side > 0 ? 0 : 1];
            // Near one end of the overlap, clear of the length label in the middle.
            if (!slot.wall || std::abs(side) < std::abs(slot.side)) slot = {&v, side, lo + std::min(36.0, (hi - lo) / 4)};
        }
        const Segment mine = wallMeasureLine(d, *w, d.dimensionTo);
        for (const Near& k : nearest) {
            if (!k.wall) continue;
            const Segment theirs = wallMeasureLine(d, *k.wall, d.dimensionTo);
            const Vec2 at = w->a + dir * k.along;
            const Vec2 pa = mine.a + dir * dot(at - mine.a, dir);
            const Vec2 pb = theirs.a + dir * dot(at - theirs.a, dir);
            const double value = std::abs(dot(pb - pa, n));
            if (value < 0.5) continue;
            const QRectF text = drawDimension(g, pa, pb, 0.0, false, true);
            // Moving away from that wall makes the number bigger.
            const double away = k.side > 0 ? -1.0 : 1.0;
            const ObjectId id = w->id;
            addValueHit(text, value, "Move wall", [id, away, value](Document& doc, double v) {
                slideWall(doc, id, away * (v - value));
            });
        }
        return;
    }
    if (const Opening* o = d.opening(sel)) {
        // Along its wall, from each end of the wall to the opening's center.
        const Wall* w = d.wall(o->wallId);
        if (!w || w->levelId != ws_->currentLevel() || w->length() < 1.0) return;
        const Vec2 dir = (w->b - w->a).normalized();
        const Segment m = wallMeasureLine(d, *w, d.dimensionTo);
        const double tA = dot(m.a - w->a, dir), tB = dot(m.b - w->a, dir);
        const Vec2 center = m.a + dir * (o->offset - tA);
        // Inside the building, clear of the wall.
        const double inside = dot(-wallOutside(d, *w), dir.perp()) >= 0 ? 1.0 : -1.0;
        const double offset = inside * (w->thickness + 24.0);
        const ObjectId id = o->id;
        const double width = o->width, wallLength = w->length();
        auto place = [id, width, wallLength](Document& doc, double offset) {
            if (Opening* x = doc.opening(id)) x->offset = std::clamp(offset, width / 2, wallLength - width / 2);
        };
        const double fromA = o->offset - tA, toB = tB - o->offset;
        if (fromA > 0.5) {
            const QRectF text = drawDimension(g, m.a, center, offset, false, true);
            addValueHit(text, fromA, "Move opening", [place, tA](Document& doc, double v) { place(doc, v + tA); });
        }
        if (toB > 0.5) {
            const QRectF text = drawDimension(g, center, m.b, offset, false, true);
            addValueHit(text, toB, "Move opening", [place, tB](Document& doc, double v) { place(doc, tB - v); });
        }
    }
}

void PlanView::addValueHit(QRectF rect, double value, const QString& undoText,
                           std::function<void(Document&, double)> apply) const {
    if (!preview_ && rect.isValid()) valueHits_.push_back({rect, value, undoText, std::move(apply)});
}

void PlanView::openValueEditor(const ValueHit& hit) {
    editing_ = hit;
    const QPointF c = hit.rect.center();
    valueEdit_->setGeometry(QRect(int(c.x()) - 55, int(c.y()) - 12, 110, 24));
    valueEdit_->setText(lengthText(hit.value));
    valueEdit_->show();
    valueEdit_->raise();
    valueEdit_->setFocus();
    valueEdit_->selectAll();
    emit statusMessage("Type the new length and press Enter. Escape cancels.");
}

void PlanView::commitValue() {
    if (!editing_) return;
    const auto v = parseLength(valueEdit_->text().toStdString());
    if (!v || *v < 0) {
        emit statusMessage(QString("\"%1\" isn't a length. Try 12' 6\" or 150.").arg(valueEdit_->text()));
        return;
    }
    const ValueHit hit = *editing_;
    editing_.reset();
    valueEdit_->hide();
    setFocus();
    if (std::abs(*v - hit.value) < 1e-6) return;
    ws_->edit(hit.undoText, [&](Document& d) { hit.apply(d, *v); });
}

void PlanView::nudge(Vec2 delta) {
    const auto ids = ws_->selectedIds();
    if (ids.empty()) return;
    ws_->edit("Nudge", [&](Document& d) { shiftObjects(d, ids, delta); });
}

void PlanView::askLengthAngle() {
    if (!anchor_) return;
    const Vec2 from = *anchor_;
    const Vec2 toward = cursor_ - from;
    QDialog dialog(this);
    dialog.setWindowTitle(tool_ == Tool::Beam ? "Beam length and angle" : "Wall length and angle");
    auto* form = new QFormLayout(&dialog);
    auto* length = new QLineEdit(lengthText(std::max(toward.length(), 12.0)));
    length->selectAll();
    auto* angle = new QDoubleSpinBox;
    angle->setRange(-360, 360);
    angle->setDecimals(2);
    angle->setSuffix(QString::fromUtf8("°"));
    angle->setValue(toward.length() < 1e-6 ? 0.0 : std::atan2(toward.y, toward.x) * 180.0 / kPi);
    angle->setToolTip("Counterclockwise from east: 0 runs right, 90 runs up the screen");
    form->addRow("Length", length);
    form->addRow("Angle", angle);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto v = parseLength(length->text().toStdString());
    if (!v || *v < 1.0) {
        emit statusMessage(QString("\"%1\" isn't a length. Try 12' 6\" or 150.").arg(length->text()));
        return;
    }
    const double rad = angle->value() * kPi / 180.0;
    const Vec2 end = from + Vec2{std::cos(rad), std::sin(rad)} * *v;
    toolClick(end, end);
    cursor_ = end;
    update();
}

void PlanView::drawTerrain(QPainter& g) const {
    const Level* ground = doc().groundLevel();
    if (!ground || ground->id != ws_->currentLevel()) return;
    const Terrain& t = doc().terrain;
    std::vector<double> labeled;
    for (const auto& s : contourLines(t)) {
        g.setPen(QPen(QColor(150, 110, 60, s.major ? 220 : 170), s.major ? 1.8 : 1.0));
        g.drawLine(toScreen(s.a), toScreen(s.b));
        // Label each contour once, on its first piece long enough to hold the text.
        if (distance(s.a, s.b) * scale_ < 50) continue;
        if (std::find_if(labeled.begin(), labeled.end(), [&](double z) { return std::abs(z - s.elevation) < 1e-6; }) !=
            labeled.end())
            continue;
        labeled.push_back(s.elevation);
        g.setPen(QColor(120, 80, 30));
        const QPointF a = toScreen(s.a), b = toScreen(s.b);
        drawAlong(g, (a + b) / 2.0, b - a, lengthText(s.elevation), 1.0);
    }
    for (const auto& p : t.points) {
        const bool selected = ws_->isSelected(p.id);
        const QPointF c = toScreen(p.position);
        g.setPen(QPen(selected ? QColor(30, 110, 220) : QColor(120, 80, 30), selected ? 2 : 1.2));
        g.setBrush(Qt::NoBrush);
        g.drawLine(c + QPointF(-5, -5), c + QPointF(5, 5));
        g.drawLine(c + QPointF(-5, 5), c + QPointF(5, -5));
        g.drawEllipse(c, 6, 6);
        g.drawText(c + QPointF(8, -6), lengthText(p.elevation));
    }
}

void PlanView::drawTyped(QPainter& g) const {
    if (typed_.isEmpty()) return;
    const QPointF c = toScreen(cursor_) + QPointF(16, 16);
    const QString text = QString("Length: %1   Enter to place, Esc to cancel").arg(typed_);
    const QRectF box(c, QSizeF(g.fontMetrics().horizontalAdvance(text) + 16, 22));
    g.setPen(QColor(30, 110, 220));
    g.setBrush(QColor(255, 255, 255, 240));
    g.drawRoundedRect(box, 4, 4);
    g.setPen(QColor(20, 20, 20));
    g.drawText(box.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

void PlanView::drawLoads(QPainter& g) const {
    const auto& result = ws_->loads();
    double maxPlf = 1.0;
    for (const auto& s : result.supports)
        if (s.levelId == ws_->currentLevel()) maxPlf = std::max(maxPlf, s.topLoads().governingPlf);

    QFont bold = g.font();
    bold.setBold(true);
    g.setFont(bold);
    for (const auto& s : result.supports) {
        if (s.levelId != ws_->currentLevel()) continue;
        const auto summary = s.topLoads();
        const Vec2 dir = s.line.direction();
        if (summary.governingPlf > 0.5) {
            QPen pen(loadColor(summary.governingPlf / maxPlf), 6);
            pen.setCapStyle(Qt::FlatCap);
            g.setPen(pen);
            g.drawLine(toScreen(s.line.a), toScreen(s.line.b));
            const QPointF mid = toScreen((s.line.a + s.line.b) * 0.5 + dir.perp() * (14.0 / scale_));
            const QString text = QString("%1 plf").arg(summary.governingPlf, 0, 'f', 0);
            const QRectF box(mid.x() - 40, mid.y() - 9, 80, 18);
            g.setPen(Qt::NoPen);
            g.setBrush(QColor(255, 255, 255, 220));
            g.drawRoundedRect(box, 4, 4);
            g.setPen(QColor(30, 30, 30));
            g.drawText(box, Qt::AlignCenter, text);
        }
        for (const auto& p : s.pointLoads) {
            const QPointF at = toScreen(s.line.a + dir * p.position);
            QPolygonF tri;
            tri << at << at + QPointF(-6, -12) << at + QPointF(6, -12);
            g.setPen(Qt::NoPen);
            g.setBrush(QColor(214, 40, 40));
            g.drawPolygon(tri);
            double total = 0.0;
            for (double v : p.p.v) total += v;
            g.setPen(QColor(150, 20, 20));
            g.drawText(at + QPointF(8, -12), QString("%1 lb").arg(total, 0, 'f', 0));
        }
    }
    g.setFont(QFont());
}

QRectF PlanView::drawLengthLabel(QPainter& g, Vec2 a, Vec2 b) const {
    const QPointF pa = toScreen(a), pb = toScreen(b);
    const QPointF mid = (pa + pb) / 2.0;
    const QString text = lengthText(distance(a, b));
    const QRectF box(mid.x() - 45, mid.y() - 10, 90, 20);
    g.save();
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(255, 255, 255, 230));
    g.drawRoundedRect(box, 4, 4);
    g.setPen(QColor(20, 70, 160));
    g.drawText(box, Qt::AlignCenter, text);
    g.restore();
    return box;
}

void PlanView::drawToolPreview(QPainter& g) const {
    g.setPen(QPen(QColor(30, 110, 220), 1, Qt::DashLine));
    g.setBrush(Qt::NoBrush);
    switch (tool_) {
    case Tool::Wall:
    case Tool::Beam:
    case Tool::RoomDivider:
        if (anchor_) {
            if (tool_ == Tool::RoomDivider) {
                g.setPen(QPen(QColor(30, 110, 220, 200), 2, Qt::DashLine));
            } else {
                const WallType* t = doc().wallType(wallTypeId_);
                const double width = tool_ == Tool::Wall ? (t ? t->thickness() : 5.5) : 3.5;
                g.setPen(QPen(QColor(30, 110, 220, 160), std::max(2.0, pixels(width))));
            }
            // While a length is being typed, show the piece at that length.
            Vec2 end = cursor_;
            if (const auto typed = parseLength(typed_.toStdString()); typed && *typed >= 1.0) {
                const Vec2 d = cursor_ - *anchor_;
                end = *anchor_ + (d.length() < 1e-6 ? Vec2{1, 0} : d.normalized()) * *typed;
            }
            g.drawLine(toScreen(*anchor_), toScreen(end));
            drawLengthLabel(g, *anchor_, end);
        }
        break;
    case Tool::Dimension:
        if (anchor_ && !anchor2_) {
            drawDimension(g, *anchor_, cursor_, 0.0, true);
        } else if (anchor_ && anchor2_) {
            const Vec2 dir = (*anchor2_ - *anchor_).normalized();
            drawDimension(g, *anchor_, *anchor2_, dot(cursor_ - *anchor_, dir.perp()), true);
        }
        break;
    case Tool::Elevation:
        break;
    case Tool::Post: {
        const QPointF c = toScreen(cursor_);
        const double half = std::max(4.0, pixels(1.75));
        g.setBrush(QColor(30, 110, 220, 80));
        g.drawRect(QRectF(c.x() - half, c.y() - half, 2 * half, 2 * half));
        break;
    }
    case Tool::JoinPlanes: {
        auto edgeOf = [this](const std::pair<ObjectId, int>& e) -> std::optional<Segment> {
            const RoofPlane* r = doc().roofPlane(e.first);
            if (!r || e.second >= static_cast<int>(r->outline.size())) return std::nullopt;
            return Segment{r->outline[e.second], r->outline[(e.second + 1) % r->outline.size()]};
        };
        if (joinEdge_) {
            if (auto s = edgeOf(*joinEdge_)) {
                g.setPen(QPen(QColor(30, 110, 220), 4));
                g.drawLine(toScreen(s->a), toScreen(s->b));
            }
        } else if (auto near = roofEdgeNear(cursor_)) {
            if (auto s = edgeOf(*near)) {
                g.setPen(QPen(QColor(30, 110, 220, 140), 4));
                g.drawLine(toScreen(s->a), toScreen(s->b));
            }
        }
        break;
    }
    case Tool::Floor:
    case Tool::Roof:
        if (anchor_) {
            const Rect r = Rect::fromPoints(*anchor_, cursor_);
            g.drawRect(QRectF(toScreen({r.min.x, r.max.y}), toScreen({r.max.x, r.min.y})));
            drawLengthLabel(g, {r.min.x, r.max.y + 12 / scale_}, {r.max.x, r.max.y + 12 / scale_});
            drawLengthLabel(g, {r.max.x + 30 / scale_, r.min.y}, {r.max.x + 30 / scale_, r.max.y});
        }
        break;
    case Tool::Door:
    case Tool::Window:
        if (const Wall* w = wallNear(cursor_, kPickPixels, true)) {
            const double width = std::min((tool_ == Tool::Door ? doorSize_ : windowSize_).width, w->length());
            const double along = std::clamp(w->centerline().project(cursor_) * w->length(), width / 2, w->length() - width / 2);
            const Vec2 dir = (w->b - w->a).normalized();
            const Vec2 n = dir.perp() * (w->thickness / 2.0 + 2.0 / scale_);
            const Vec2 s0 = w->a + dir * (along - width / 2), s1 = w->a + dir * (along + width / 2);
            QPolygonF poly;
            poly << toScreen(s0 + n) << toScreen(s1 + n) << toScreen(s1 - n) << toScreen(s0 - n);
            g.setBrush(QColor(30, 110, 220, 80));
            g.drawPolygon(poly);
        }
        break;
    case Tool::RoofPlane:
        if (anchor_ && !anchor2_) {
            g.setPen(QPen(QColor(150, 60, 60, 200), 2));
            // While a length is being typed, show the piece at that length.
            Vec2 end = cursor_;
            if (const auto typed = parseLength(typed_.toStdString()); typed && *typed >= 1.0) {
                const Vec2 d = cursor_ - *anchor_;
                end = *anchor_ + (d.length() < 1e-6 ? Vec2{1, 0} : d.normalized()) * *typed;
            }
            g.drawLine(toScreen(*anchor_), toScreen(end));
            drawLengthLabel(g, *anchor_, end);
        } else if (anchor_ && anchor2_) {
            Vec2 a = *anchor_, b = *anchor2_;
            const double run = cross((b - a).normalized(), cursor_ - a);
            if (run < 0) std::swap(a, b);
            const RoofPlane defaults;
            QPolygonF poly;
            for (Vec2 p : RoofPlane::rectangle(a, b, std::abs(run), defaults.overhang)) poly << toScreen(p);
            g.setBrush(QColor(150, 60, 60, 40));
            g.drawPolygon(poly);
            g.setPen(QPen(QColor(150, 60, 60), 2));
            g.drawLine(toScreen(a), toScreen(b));
            const Vec2 up = (b - a).normalized().perp() * std::abs(run);
            drawLengthLabel(g, (a + b) * 0.5, (a + b) * 0.5 + up);
        }
        break;
    case Tool::Select:
        break;
    }
    if (tool_ != Tool::Select) {
        const QPointF c = toScreen(cursor_);
        g.setPen(QPen(QColor(30, 110, 220), 1));
        g.drawLine(c + QPointF(-6, 0), c + QPointF(6, 0));
        g.drawLine(c + QPointF(0, -6), c + QPointF(0, 6));
    }
}

// ---- Editing ---------------------------------------------------------------

void PlanView::placeOpening(Vec2 p, OpeningKind kind) {
    const Wall* w = wallNear(p, kPickPixels, true);
    if (!w) {
        emit statusMessage("Click on a wall to place it. Room dividers can't have doors or windows.");
        return;
    }
    const ObjectId wallId = w->id;
    const OpeningSize size = kind == OpeningKind::Door ? doorSize_ : windowSize_;
    const double width = std::min(size.width, w->length());
    const double along = std::round(std::clamp(w->centerline().project(p) * w->length(), width / 2, w->length() - width / 2));
    ObjectId created = kNoId;
    ws_->edit(kind == OpeningKind::Door ? "Add door" : "Add window", [&](Document& d) {
        Opening o;
        o.id = d.newId();
        o.wallId = wallId;
        o.kind = kind;
        o.offset = along;
        o.width = width;
        o.height = size.height;
        o.sillHeight = size.sill;
        d.openings.push_back(o);
        created = o.id;
    });
    ws_->select(created);
}

void PlanView::finishRect(Vec2 a, Vec2 b) {
    const Rect r = Rect::fromPoints(a, b);
    if (r.width() < 12.0 || r.height() < 12.0) return;
    const ObjectId level = ws_->currentLevel();
    ObjectId created = kNoId;
    switch (tool_) {
    case Tool::Floor:
        ws_->edit("Add floor framing", [&](Document& d) {
            FloorPlatform f;
            f.id = d.newId();
            f.levelId = level;
            f.area = r;
            f.joistAxis = r.width() > r.height() ? Axis::Y : Axis::X;
            d.platforms.push_back(f);
            created = f.id;
        });
        break;
    case Tool::Roof:
        ws_->edit("Add roof", [&](Document& d) {
            Roof roof;
            roof.id = d.newId();
            roof.levelId = level;
            roof.footprint = r;
            roof.ridgeAxis = r.width() >= r.height() ? Axis::X : Axis::Y;
            d.roofs.push_back(roof);
            created = roof.id;
        });
        break;
    default:
        return;
    }
    ws_->select(created);
}

void PlanView::finishRoofPlane(Vec2 a, Vec2 b, Vec2 high) {
    const double run = cross((b - a).normalized(), high - a);
    if (std::abs(run) < 12.0) {
        emit statusMessage("Click farther from the eave to give the roof plane some run.");
        return;
    }
    if (run < 0) std::swap(a, b); // the plane always rises to the left of a to b
    const ObjectId level = ws_->currentLevel();
    ObjectId created = kNoId;
    ws_->edit("Add roof plane", [&](Document& d) {
        RoofPlane r;
        r.id = d.newId();
        r.levelId = level;
        r.a = a;
        r.b = b;
        r.outline = RoofPlane::rectangle(a, b, std::round(std::abs(run)), r.overhang);
        d.roofPlanes.push_back(r);
        created = r.id;
    });
    ws_->select(created);
}

void PlanView::addWall(Vec2 a, Vec2 b) {
    const ObjectId level = ws_->currentLevel();
    const Document& current = ws_->doc();
    const WallType* type = tool_ == Tool::RoomDivider ? current.firstWallType(WallKind::RoomDivider)
                                                      : current.wallType(wallTypeId_);
    if (!type && tool_ == Tool::Wall) type = current.firstWallType(WallKind::Exterior);
    const ObjectId typeId = type ? type->id : kNoId;
    const bool divider = tool_ == Tool::RoomDivider;
    ObjectId created = kNoId;
    ws_->edit(divider ? "Add room divider" : "Add wall", [&](Document& d) {
        Wall w;
        w.id = d.newId();
        w.levelId = level;
        w.a = a;
        w.b = b;
        w.wallTypeId = typeId;
        w.roomDivider = divider;
        if (divider) {
            w.bearing = false;
            w.thickness = 0.0;
        }
        d.walls.push_back(w);
        created = w.id;
    });
    ws_->select(created);
}

void PlanView::placePost(Vec2 p) {
    const ObjectId level = ws_->currentLevel();
    ObjectId created = kNoId;
    ws_->edit("Add post", [&](Document& d) {
        Post post;
        post.id = d.newId();
        post.levelId = level;
        post.position = p;
        d.posts.push_back(post);
        created = post.id;
    });
    ws_->select(created);
}

void PlanView::finishDimension(Vec2 a, Vec2 b, Vec2 at) {
    const double offset = std::round(dot(at - a, (b - a).normalized().perp()));
    const ObjectId level = ws_->currentLevel();
    ObjectId created = kNoId;
    ws_->edit("Add dimension", [&](Document& d) {
        Dimension x;
        x.id = d.newId();
        x.levelId = level;
        x.a = a;
        x.b = b;
        x.offset = offset;
        d.dimensions.push_back(x);
        created = x.id;
    });
    ws_->select(created);
}

void PlanView::placeElevation(Vec2 p) {
    const Level* ground = doc().groundLevel();
    if (!ground || ground->id != ws_->currentLevel()) {
        emit statusMessage("The terrain is on the lowest level. Switch to it to place elevation points.");
        return;
    }
    // Start from the height the terrain already has there, or the last point's.
    double z = doc().terrain.points.empty() ? -12.0 : doc().terrain.points.back().elevation;
    if (auto h = terrainHeightAt(doc().terrain, p)) z = std::round(*h);
    ObjectId created = kNoId;
    ws_->edit("Add elevation point", [&](Document& d) {
        ElevationPoint e;
        e.id = d.newId();
        e.position = p;
        e.elevation = z;
        d.terrain.points.push_back(e);
        created = e.id;
    });
    ws_->select(created);
    emit statusMessage("Set the point's elevation in Properties. Elevations are relative to the first floor.");
}

void PlanView::joinClick(Vec2 p) {
    if (!joinEdge_) {
        joinEdge_ = roofEdgeNear(p);
        emit statusMessage(joinEdge_ ? "Now click the roof plane this edge should meet."
                                     : "Click right on an edge of a roof plane.");
        if (joinEdge_) ws_->select(joinEdge_->first);
        return;
    }
    const auto [planeId, edge] = *joinEdge_;
    const RoofPlane* other = nullptr;
    for (const auto& r : doc().roofPlanes)
        if (r.id != planeId && r.levelId == ws_->currentLevel() && r.outline.size() >= 3 &&
            (contains(r.outline, p) || roofEdgeNear(p).value_or(std::pair<ObjectId, int>{kNoId, 0}).first == r.id))
            other = &r;
    const RoofPlane* plane = doc().roofPlane(planeId);
    if (!other || !plane) {
        emit statusMessage("Click inside another roof plane to join to it, or Esc to start over.");
        return;
    }
    const Polygon joined = joinRoofPlanes(doc(), *plane, edge, *other);
    if (joined.size() < 3 || area(joined) < 144.0) {
        emit statusMessage("Those planes don't meet along that edge. Try another edge, or check the pitches.");
        joinEdge_.reset();
        return;
    }
    ws_->edit("Join roof planes", [&](Document& d) { d.roofPlane(planeId)->outline = joined; });
    joinEdge_.reset();
    emit statusMessage("Joined. Click another edge to keep going.");
}

void PlanView::deleteSelection() {
    const std::vector<ObjectId> ids = ws_->selectedIds();
    if (ids.empty()) return;
    ws_->edit(ids.size() == 1 ? "Delete" : QString("Delete %1 objects").arg(ids.size()), [ids](Document& d) {
        for (ObjectId id : ids)
            if (d.contains(id)) d.remove(id);
    });
}

void PlanView::mousePressEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        panStart_ = pos;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton) {
        if (tool_ == Tool::Select) {
            const auto hits = pickAll(toWorld(pos));
            if (!hits.empty()) showStackMenu(pos, hits);
            return;
        }
        anchor_.reset();
        anchor2_.reset();
        joinEdge_.reset();
        update();
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (valueEdit_->isVisible()) {
        valueEdit_->hide();
        editing_.reset();
    }

    // A number that takes a typed value: a temporary dimension, the length
    // of the selected wall, or a dimension that moves the selection.
    if (tool_ == Tool::Select && !(e->modifiers() & Qt::ShiftModifier))
        for (auto it = valueHits_.rbegin(); it != valueHits_.rend(); ++it)
            if (it->rect.contains(pos)) {
                const ValueHit hit = *it;
                openValueEditor(hit);
                return;
            }

    // Shift starts a selection box with any tool; the box picks only what
    // the tool draws.
    if (e->modifiers() & Qt::ShiftModifier) {
        drag_ = Drag::Box;
        boxStart_ = boxEnd_ = pos;
        boxAdds_ = e->modifiers() & Qt::ControlModifier;
        update();
        return;
    }

    const Vec2 raw = toWorld(pos);
    Vec2 p = snap(raw, anchor_, e->modifiers());
    if (tool_ == Tool::Dimension && !anchor2_)
        if (auto hit = dimensionSnap(raw, e->modifiers())) p = *hit;

    switch (tool_) {
    case Tool::Select: {
        const ObjectId sel = ws_->selection();
        const bool several = ws_->selectedIds().size() > 1;
        // Grab a node of a selected dimension, or a wall or beam end of the
        // current selection first.
        if (auto node = dimensionHandleAt(pos)) drag_ = *node;
        const Wall* sw = several ? nullptr : doc().wall(sel);
        const Beam* sb = several ? nullptr : doc().beam(sel);
        if (drag_ == Drag::None && (sw || sb)) {
            const Vec2 a = sw ? sw->a : sb->a, b = sw ? sw->b : sb->b;
            if (QLineF(toScreen(a), pos).length() < kPickPixels) drag_ = Drag::EndA;
            else if (QLineF(toScreen(b), pos).length() < kPickPixels) drag_ = Drag::EndB;
        }
        // Or a corner or edge handle of a selected roof plane.
        if (auto v = roofVertexAt(pos)) {
            drag_ = Drag::Vertex;
            dragIndex_ = *v;
        } else if (auto edge = roofEdgeHandleAt(pos)) {
            drag_ = Drag::Edge;
            dragIndex_ = *edge;
        }
        cycleOnRelease_ = false;
        if (drag_ == Drag::None && several) {
            // Pressing on one of several selected objects drags them all.
            const auto hits = pickAll(raw);
            if (std::any_of(hits.begin(), hits.end(), [this](ObjectId id) { return ws_->isSelected(id); }))
                drag_ = Drag::Move;
        }
        if (drag_ == Drag::None) {
            const auto hits = pickAll(raw);
            ObjectId hit = hits.empty() ? kNoId : hits.front();
            if (std::find(hits.begin(), hits.end(), sel) != hits.end() && sel != kNoId) {
                // Clicking what is already selected keeps it, so it can be dragged;
                // a click without a drag then moves on to the next object below.
                hit = sel;
                cycleOnRelease_ = hits.size() > 1 && hits == stack_;
            }
            selectFromStack(hits, hit);
            if (doc().wall(hit) || doc().beam(hit) || doc().opening(hit) || doc().roofPlane(hit) || doc().post(hit) ||
                doc().dimension(hit) || doc().elevationPoint(hit))
                drag_ = Drag::Move;
        }
        if (drag_ != Drag::None) {
            dragStart_ = raw;
            preview_ = ws_->doc();
        }
        break;
    }
    default:
        typed_.clear();
        toolClick(p, raw);
        break;
    }
    update();
}

// What a click does with a drawing tool at the snapped point `p`. Typing a
// length and pressing Enter clicks too, at that distance from the anchor.
void PlanView::toolClick(Vec2 p, Vec2 raw) {
    const ObjectId level = ws_->currentLevel();
    switch (tool_) {
    case Tool::Wall:
    case Tool::RoomDivider:
    case Tool::Beam:
        if (!anchor_) {
            anchor_ = p;
        } else if (distance(*anchor_, p) >= 1.0) {
            const Vec2 a = *anchor_;
            ObjectId created = kNoId;
            if (tool_ != Tool::Beam) {
                addWall(a, p);
                anchor_ = p; // keep chaining
            } else {
                ws_->edit("Add beam", [&](Document& d) {
                    Beam b;
                    b.id = d.newId();
                    b.levelId = level;
                    b.a = a;
                    b.b = p;
                    d.beams.push_back(b);
                    created = b.id;
                });
                anchor_.reset();
                ws_->select(created);
            }
        }
        break;
    case Tool::Post:
        placePost(p);
        break;
    case Tool::JoinPlanes:
        joinClick(raw);
        break;
    case Tool::Door:
        placeOpening(raw, OpeningKind::Door);
        break;
    case Tool::Window:
        placeOpening(raw, OpeningKind::Window);
        break;
    case Tool::Floor:
    case Tool::Roof:
        anchor_ = p;
        break;
    case Tool::RoofPlane:
        if (!anchor_) {
            anchor_ = p;
        } else if (!anchor2_) {
            if (distance(*anchor_, p) >= 12.0) anchor2_ = p;
        } else {
            const Vec2 a = *anchor_, b = *anchor2_;
            anchor_.reset();
            anchor2_.reset();
            finishRoofPlane(a, b, p);
        }
        break;
    case Tool::Dimension:
        if (!anchor_) {
            anchor_ = p;
        } else if (!anchor2_) {
            if (distance(*anchor_, p) >= 1.0) anchor2_ = p;
        } else {
            const Vec2 a = *anchor_, b = *anchor2_;
            anchor_.reset();
            anchor2_.reset();
            finishDimension(a, b, p);
        }
        break;
    case Tool::Elevation:
        placeElevation(p);
        break;
    case Tool::Select:
        break;
    }
    update();
}

void PlanView::mouseMoveEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (panning_) {
        offset_ += pos - panStart_;
        panStart_ = pos;
        update();
        return;
    }
    if (drag_ == Drag::Box) {
        boxEnd_ = pos;
        update();
        return;
    }
    const Vec2 raw = toWorld(pos);
    const bool twoPoint = tool_ == Tool::Wall || tool_ == Tool::RoomDivider || tool_ == Tool::Beam ||
                          ((tool_ == Tool::RoofPlane || tool_ == Tool::Dimension) && !anchor2_);
    cursor_ = snap(raw, twoPoint ? anchor_ : std::nullopt, e->modifiers());
    if (tool_ == Tool::Dimension && !anchor2_)
        if (auto hit = dimensionSnap(raw, e->modifiers())) cursor_ = *hit;

    if (drag_ != Drag::None && preview_) {
        Document d = ws_->doc();
        const ObjectId sel = ws_->selection();
        const Vec2 delta = snap(raw, std::nullopt, e->modifiers()) - snap(dragStart_, std::nullopt, e->modifiers());
        if (ws_->selectedIds().size() > 1) {
            moveObjects(d, ws_->selectedIds(), delta);
        } else if (drag_ == Drag::DimA || drag_ == Drag::DimB) {
            // Move one end of the dimension, and the matching end of its
            // neighbors in the string so the string stays joined.
            const Dimension& before = *ws_->doc().dimension(sel);
            const Vec2 old = drag_ == Drag::DimA ? before.a : before.b;
            const auto hit = dimensionSnap(raw, e->modifiers());
            const Vec2 to = hit ? *hit : snap(raw, std::nullopt, e->modifiers());
            for (ObjectId id : dimensionString(ws_->doc(), sel)) {
                Dimension* x = d.dimension(id);
                const bool isSel = id == sel;
                if ((isSel && drag_ == Drag::DimA) || (!isSel && distance(x->a, old) < 0.5)) x->a = to;
                if ((isSel && drag_ == Drag::DimB) || (!isSel && distance(x->b, old) < 0.5)) x->b = to;
                x->automatic = false;
            }
        } else if (const Wall* w = d.wall(sel)) {
            // Walls joined at the ends stretch to follow, as in Chief; a
            // joined wall moves square to itself.
            if (drag_ == Drag::Move) {
                shiftObjects(d, {sel}, delta);
            } else {
                const Vec2 to = snap(raw, drag_ == Drag::EndA ? w->b : w->a, e->modifiers());
                setWallEnds(d, sel, drag_ == Drag::EndA ? to : w->a, drag_ == Drag::EndA ? w->b : to);
            }
            d.refresh(); // rooms and automatic dimensions follow while dragging
        } else if (Beam* b = d.beam(sel)) {
            if (drag_ == Drag::Move) {
                b->a += delta;
                b->b += delta;
            } else {
                (drag_ == Drag::EndA ? b->a : b->b) = snap(raw, drag_ == Drag::EndA ? b->b : b->a, e->modifiers());
            }
        } else if (RoofPlane* r = d.roofPlane(sel)) {
            const int n = static_cast<int>(r->outline.size());
            if (drag_ == Drag::Vertex && dragIndex_ < n) {
                r->outline[dragIndex_] = snap(raw, std::nullopt, e->modifiers());
            } else if (drag_ == Drag::Edge && dragIndex_ < n) {
                // Slide the edge square to itself; its neighbors keep their directions.
                const Vec2 p0 = r->outline[dragIndex_], p1 = r->outline[(dragIndex_ + 1) % n];
                const Vec2 normal = (p1 - p0).normalized().perp();
                double offset = dot(raw - dragStart_, normal);
                if (!(e->modifiers() & Qt::ControlModifier)) offset = std::round(offset);
                const Polygon moved = moveEdge(r->outline, dragIndex_, offset);
                if (moved.size() == r->outline.size() && area(moved) > 144.0) r->outline = moved;
            } else {
                r->a += delta;
                r->b += delta;
                for (Vec2& p : r->outline) p += delta;
            }
        } else if (Post* post = d.post(sel)) {
            post->position = snap(raw, std::nullopt, e->modifiers());
        } else if (ElevationPoint* ep = d.elevationPoint(sel)) {
            ep->position = snap(raw, std::nullopt, e->modifiers());
        } else if (d.dimension(sel)) {
            // Dragging a dimension slides its whole string toward or away from what it measures.
            const Dimension& before = *ws_->doc().dimension(sel);
            const Vec2 n = (before.b - before.a).normalized().perp();
            const double target = std::round(before.offset + dot(raw - dragStart_, n));
            const Vec2 shift = n * (target - before.offset);
            for (ObjectId id : dimensionString(ws_->doc(), sel)) {
                Dimension* x = d.dimension(id);
                x->offset += dot(shift, (x->b - x->a).normalized().perp());
                x->automatic = false;
            }
        } else if (Opening* o = d.opening(sel)) {
            if (const Wall* w = d.wall(o->wallId))
                o->offset = std::round(std::clamp(w->centerline().project(raw) * w->length(), o->width / 2,
                                                  w->length() - o->width / 2));
        }
        preview_ = std::move(d);
    }

    emit statusMessage(QString("X %1   Y %2").arg(lengthText(cursor_.x), lengthText(cursor_.y)));
    update();
}

void PlanView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        panning_ = false;
        setCursor(tool_ == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    if (drag_ == Drag::Box) {
        drag_ = Drag::None;
        finishBox(boxStart_, e->position(), boxAdds_);
        update();
        return;
    }
    if (drag_ == Drag::None && cycleOnRelease_) {
        cycleOnRelease_ = false;
        cycleStack();
        return;
    }
    if (drag_ != Drag::None) {
        const Drag kind = drag_;
        drag_ = Drag::None;
        if (preview_ && distance(toWorld(e->position()), dragStart_) * scale_ > 2.0) {
            Document result = std::move(*preview_);
            preview_.reset();
            const char* text = kind == Drag::Move                             ? "Move"
                               : kind == Drag::Vertex || kind == Drag::Edge ? "Reshape roof plane"
                               : kind == Drag::DimA || kind == Drag::DimB   ? "Move dimension end"
                                                                            : "Stretch";
            ws_->edit(text, [&](Document& d) { d = std::move(result); });
        } else {
            preview_.reset();
            if (cycleOnRelease_) cycleStack();
        }
        cycleOnRelease_ = false;
        update();
        return;
    }
    if ((tool_ == Tool::Floor || tool_ == Tool::Roof) && anchor_) {
        const Vec2 a = *anchor_;
        anchor_.reset();
        finishRect(a, cursor_);
    }
    update();
}

void PlanView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (tool_ == Tool::Wall || tool_ == Tool::RoomDivider) anchor_.reset();
    if (tool_ == Tool::Select && e->button() == Qt::LeftButton && ws_->selection() != kNoId &&
        !pickAll(toWorld(e->position())).empty())
        emit openObjectRequested();
    QWidget::mouseDoubleClickEvent(e);
}

void PlanView::wheelEvent(QWheelEvent* e) {
    const QPointF pos = e->position();
    const Vec2 before = toWorld(pos);
    const double factor = std::pow(1.2, e->angleDelta().y() / 120.0);
    scale_ = std::clamp(scale_ * factor, 0.02, 40.0);
    offset_ = QPointF(pos.x() - before.x * scale_, pos.y() + before.y * scale_);
    update();
}

bool PlanView::takesTypedLength() const {
    switch (tool_) {
    case Tool::Wall:
    case Tool::RoomDivider:
    case Tool::Beam:
        return anchor_.has_value();
    case Tool::RoofPlane:
    case Tool::Dimension:
        return anchor_ && !anchor2_;
    default:
        return false;
    }
}

namespace {
// Keys that type a length: digits, feet and inch marks, fractions, spaces.
bool lengthKey(const QKeyEvent* e) {
    const QString t = e->text();
    return t.size() == 1 && QString("0123456789'\" ./-").contains(t[0]);
}
} // namespace

// While a length is being typed, keys that are also shortcuts (like a
// space or a letter) go to the plan instead of the menus.
bool PlanView::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride && takesTypedLength()) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (lengthKey(k) || (!typed_.isEmpty() && (k->key() == Qt::Key_Backspace || k->key() == Qt::Key_Return ||
                                                   k->key() == Qt::Key_Enter || k->key() == Qt::Key_Escape ||
                                                   !k->text().isEmpty()))) {
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

void PlanView::commitTyped() {
    const auto length = parseLength(typed_.toStdString());
    if (!length || *length < 1.0 || !anchor_) {
        emit statusMessage(QString("\"%1\" isn't a length. Try 12' 6\" or 150.").arg(typed_));
        return;
    }
    // The direction comes from the mouse, snapped to 15 degree steps.
    Vec2 dir = cursor_ - *anchor_;
    dir = dir.length() < 1e-6 ? Vec2{1, 0} : dir.normalized();
    const Vec2 end = *anchor_ + dir * *length;
    typed_.clear();
    toolClick(end, end);
    cursor_ = end;
    update();
}

void PlanView::keyPressEvent(QKeyEvent* e) {
    if (takesTypedLength()) {
        if (lengthKey(e) && !(typed_.isEmpty() && e->text() == " ")) {
            typed_ += e->text();
            update();
            return;
        }
        if (!typed_.isEmpty()) {
            switch (e->key()) {
            case Qt::Key_Backspace:
                typed_.chop(1);
                break;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                commitTyped();
                break;
            case Qt::Key_Escape:
                typed_.clear();
                break;
            default:
                break;
            }
            update();
            return;
        }
    } else {
        typed_.clear();
    }
    switch (e->key()) {
    case Qt::Key_Escape:
        if (anchor_ || joinEdge_ || drag_ != Drag::None) {
            anchor_.reset();
            anchor2_.reset();
            joinEdge_.reset();
            drag_ = Drag::None;
            preview_.reset();
            update();
        } else if (tool_ != Tool::Select) {
            setTool(Tool::Select);
        } else {
            ws_->select(kNoId);
        }
        break;
    case Qt::Key_Tab:
        if (anchor_ && (tool_ == Tool::Wall || tool_ == Tool::RoomDivider || tool_ == Tool::Beam)) askLengthAngle();
        else cycleStack();
        break;
    case Qt::Key_Space:
        // Spacebar returns to selecting, as in Chief.
        if (tool_ != Tool::Select) setTool(Tool::Select);
        break;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (anchor_ || drag_ != Drag::None || ws_->selectedIds().empty()) {
            QWidget::keyPressEvent(e);
            break;
        }
        // 1" a press, a foot with Shift.
        const double step = e->modifiers() & Qt::ShiftModifier ? 12.0 : 1.0;
        const Vec2 dir = e->key() == Qt::Key_Left    ? Vec2{-1, 0}
                         : e->key() == Qt::Key_Right ? Vec2{1, 0}
                         : e->key() == Qt::Key_Up    ? Vec2{0, 1}
                                                     : Vec2{0, -1};
        nudge(dir * step);
        break;
    }
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        break;
    case Qt::Key_F:
    case Qt::Key_Home:
        zoomToFit();
        break;
    default:
        QWidget::keyPressEvent(e);
    }
}
