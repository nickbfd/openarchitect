#include "PlanView.h"

#include "Workspace.h"

#include "oa/core/units.h"
#include "oa/structural/members.h"
#include "oa/structural/posts.h"

#include <QKeyEvent>
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

PlanView::PlanView(Workspace* ws, QWidget* parent) : QWidget(parent), ws_(ws) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    connect(ws_, &Workspace::documentChanged, this, qOverload<>(&QWidget::update));
    connect(ws_, &Workspace::selectionChanged, this, [this] {
        // A selection made elsewhere (the 3D view, the loads table) ends the stack.
        if (std::find(stack_.begin(), stack_.end(), ws_->selection()) == stack_.end()) {
            stack_.clear();
            stackHint_.clear();
        }
        update();
    });
    connect(ws_, &Workspace::currentLevelChanged, this, [this] {
        anchor_.reset();
        anchor2_.reset();
        update();
    });
}

const Document& PlanView::doc() const { return preview_ ? *preview_ : ws_->doc(); }

void PlanView::setTool(Tool tool) {
    tool_ = tool;
    anchor_.reset();
    anchor2_.reset();
    joinEdge_.reset();
    drag_ = Drag::None;
    preview_.reset();
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    emit toolChanged(tool);
    static const char* hints[] = {
        "Click to select; click the same spot again, press Tab or right-click to pick an object underneath. "
        "Drag to move; drag a wall end to stretch it, or a roof plane's corner or edge handle to reshape it.",
        "Click to start a wall, click again to end it. Keep clicking to chain walls; right-click or Esc to stop. Ctrl turns off snapping.",
        "Click on a wall to place a door.",
        "Click on a wall to place a window.",
        "Click the two ends of a beam. It carries what sits on it from the level above.",
        "Drag a rectangle for the floor framing of this level. Joists run the short way.",
        "Drag a rectangle over the bearing walls for a roof. The ridge runs the long way.",
        "Click to start a room divider, an invisible wall that only separates rooms. Rooms fill the areas walls enclose.",
        "Click the two ends of the eave bearing line (usually the wall), then click on the high side to set how far the plane runs uphill.",
        "Click the edge of a roof plane to change, then click the roof plane it should meet. The edge moves to the hip, valley or ridge line.",
        "Click to place a post, for example under a porch beam end. It snaps to beam and wall ends.",
    };
    emit statusMessage(hints[static_cast<int>(tool)]);
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
    if (!r) return std::nullopt;
    for (size_t i = 0; i < r->outline.size(); ++i)
        if (QLineF(toScreen(r->outline[i]), pos).length() <= kPickPixels) return static_cast<int>(i);
    return std::nullopt;
}

std::optional<int> PlanView::roofEdgeHandleAt(QPointF pos) const {
    const RoofPlane* r = doc().roofPlane(ws_->selection());
    if (!r) return std::nullopt;
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

// ---- Painting --------------------------------------------------------------

void PlanView::paintEvent(QPaintEvent*) {
    if (!fitted_ && width() > 0) {
        fitted_ = true;
        zoomToFit();
    }
    QPainter g(this);
    g.fillRect(rect(), Qt::white);
    g.setRenderHint(QPainter::Antialiasing);

    drawGrid(g);
    drawLevelBelow(g);
    drawRooms(g);
    drawPlatforms(g);
    drawWalls(g);
    drawOpenings(g);
    drawBeams(g);
    drawRoofs(g);
    drawRoofPlanes(g);
    drawPosts(g);
    if (showLoads_) drawLoads(g);
    drawToolPreview(g);

    const Level* level = doc().level(ws_->currentLevel());
    g.setPen(QColor(90, 90, 90));
    g.drawText(QRectF(10, 6, width() - 20, 20), Qt::AlignLeft,
               level ? QString::fromStdString(level->name) : QString());
    if (!stackHint_.isEmpty()) {
        const QRectF box(10, 28, std::min(width() - 20.0, 640.0), 22);
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
        const bool selected = r.id == ws_->selection();
        g.setPen(selected ? QPen(QColor(30, 110, 220), 2) : Qt::NoPen);
        g.setBrush(roomColor(r.type));
        g.drawPolygon(poly);

        const QPointF c = toScreen(centroid(r.outline));
        const double sqft = area(r.outline) / kSqInPerSqFt;
        g.setPen(QColor(60, 60, 60));
        g.drawText(QRectF(c.x() - 80, c.y() - 18, 160, 18), Qt::AlignCenter, QString::fromStdString(r.name));
        g.setPen(QColor(120, 120, 120));
        g.drawText(QRectF(c.x() - 80, c.y(), 160, 18), Qt::AlignCenter, QString("%1 sq ft").arg(sqft, 0, 'f', 0));
    }
}

void PlanView::drawPlatforms(QPainter& g) const {
    for (const auto& f : doc().platforms) {
        if (f.levelId != ws_->currentLevel()) continue;
        const bool selected = f.id == ws_->selection();
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
    if (const Wall* w = d.wall(ws_->selection()); w && w->levelId == ws_->currentLevel()) {
        const Vec2 n = (w->b - w->a).normalized().perp() * (std::max(w->thickness, 2.0) / 2.0 + 1.0 / scale_);
        QPolygonF poly;
        poly << toScreen(w->a + n) << toScreen(w->b + n) << toScreen(w->b - n) << toScreen(w->a - n);
        g.setPen(QPen(QColor(30, 110, 220), 2));
        g.setBrush(QColor(30, 110, 220, 60));
        g.drawPolygon(poly);
        for (Vec2 end : {w->a, w->b}) g.drawEllipse(toScreen(end), 4, 4);
        drawLengthLabel(g, w->a + n * 4.0, w->b + n * 4.0);
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

        const bool selected = o.id == ws_->selection();
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
        if (selected) drawLengthLabel(g, s0 + n * 3.0, s1 + n * 3.0);
    }
}

void PlanView::drawBeams(QPainter& g) const {
    for (const auto& b : doc().beams) {
        if (b.levelId != ws_->currentLevel()) continue;
        const bool selected = b.id == ws_->selection();
        QPen pen(selected ? QColor(30, 110, 220) : QColor(170, 100, 30), std::max(3.0, pixels(3.5)), Qt::DashLine);
        pen.setCapStyle(Qt::FlatCap);
        g.setPen(pen);
        g.drawLine(toScreen(b.a), toScreen(b.b));

        const auto* member = structural::findMember(b.memberId);
        const QString label = member ? QString::fromStdString(member->name) : QString("Beam, not sized");
        const QPointF mid = toScreen((b.a + b.b) * 0.5);
        g.setPen(QColor(140, 80, 20));
        g.drawText(mid + QPointF(6, -8), label);
        if (selected) drawLengthLabel(g, b.a, b.b);
    }
}

void PlanView::drawRoofs(QPainter& g) const {
    for (const auto& r : doc().roofs) {
        if (r.levelId != ws_->currentLevel()) continue;
        const bool selected = r.id == ws_->selection();
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
    for (const auto& r : doc().roofPlanes) {
        if (r.levelId != ws_->currentLevel() || r.outline.size() < 3) continue;
        const bool selected = r.id == ws_->selection();
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

        if (!selected) continue;
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
        const bool selected = p.id == ws_->selection();
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

void PlanView::drawLengthLabel(QPainter& g, Vec2 a, Vec2 b) const {
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
            g.drawLine(toScreen(*anchor_), toScreen(cursor_));
            drawLengthLabel(g, *anchor_, cursor_);
        }
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
            const double width = 36.0;
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
            g.drawLine(toScreen(*anchor_), toScreen(cursor_));
            drawLengthLabel(g, *anchor_, cursor_);
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
    const double width = 36.0;
    const double along = std::round(std::clamp(w->centerline().project(p) * w->length(), width / 2, w->length() - width / 2));
    ObjectId created = kNoId;
    ws_->edit(kind == OpeningKind::Door ? "Add door" : "Add window", [&](Document& d) {
        Opening o;
        o.id = d.newId();
        o.wallId = wallId;
        o.kind = kind;
        o.offset = along;
        o.width = width;
        o.height = kind == OpeningKind::Door ? 80.0 : 48.0;
        o.sillHeight = kind == OpeningKind::Door ? 0.0 : 36.0;
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
    const ObjectId id = ws_->selection();
    if (id == kNoId) return;
    ws_->edit("Delete", [id](Document& d) { d.remove(id); });
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

    const Vec2 raw = toWorld(pos);
    const Vec2 p = snap(raw, anchor_, e->modifiers());
    const ObjectId level = ws_->currentLevel();

    switch (tool_) {
    case Tool::Select: {
        // Grab a wall or beam end of the current selection first.
        const ObjectId sel = ws_->selection();
        const Wall* sw = doc().wall(sel);
        const Beam* sb = doc().beam(sel);
        if (sw || sb) {
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
            if (doc().wall(hit) || doc().beam(hit) || doc().opening(hit) || doc().roofPlane(hit) || doc().post(hit))
                drag_ = Drag::Move;
        }
        if (drag_ != Drag::None) {
            dragStart_ = raw;
            preview_ = ws_->doc();
        }
        break;
    }
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
    const Vec2 raw = toWorld(pos);
    const bool twoPoint = tool_ == Tool::Wall || tool_ == Tool::RoomDivider || tool_ == Tool::Beam ||
                          (tool_ == Tool::RoofPlane && !anchor2_);
    cursor_ = snap(raw, twoPoint ? anchor_ : std::nullopt, e->modifiers());

    if (drag_ != Drag::None && preview_) {
        Document d = ws_->doc();
        const ObjectId sel = ws_->selection();
        const Vec2 delta = snap(raw, std::nullopt, e->modifiers()) - snap(dragStart_, std::nullopt, e->modifiers());
        if (Wall* w = d.wall(sel)) {
            if (drag_ == Drag::Move) {
                w->a += delta;
                w->b += delta;
            } else {
                (drag_ == Drag::EndA ? w->a : w->b) = snap(raw, drag_ == Drag::EndA ? w->b : w->a, e->modifiers());
            }
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
            const char* text = kind == Drag::Move ? "Move" : kind == Drag::Vertex || kind == Drag::Edge ? "Reshape roof plane" : "Stretch";
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

void PlanView::keyPressEvent(QKeyEvent* e) {
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
        cycleStack();
        break;
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
