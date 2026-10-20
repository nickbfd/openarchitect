#include "PropertiesPanel.h"

#include "Workspace.h"

#include "oa/core/dimensions.h"
#include "oa/core/units.h"
#include "oa/structural/members.h"
#include "oa/structural/posts.h"
#include "oa/structural/snow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

using namespace oa;
using namespace oa::structural;

namespace {
QString feetInches(double inches) { return QString::fromStdString(formatFeetInches(inches, 16)); }

QString plf(double v) { return v > 0.5 ? QString::number(v, 'f', 0) : QString("&ndash;"); }

// Plan direction a vector points, with north up the screen.
QString compass(Vec2 v) {
    static const char* names[] = {"east", "north-east", "north", "north-west", "west", "south-west", "south", "south-east"};
    double deg = std::atan2(v.y, v.x) * 180.0 / kPi;
    if (deg < 0) deg += 360.0;
    return names[static_cast<int>(std::lround(deg / 45.0)) % 8];
}

// Moves the outline edges that run parallel to the baseline at its low
// side (the eaves) or high side so they sit `target` inches downhill or
// uphill from the baseline.
Polygon setParallelEdges(const RoofPlane& p, bool high, double target) {
    Polygon out = p.outline;
    const int n = static_cast<int>(out.size());
    const double extreme = high ? p.runLength() : -p.eaveOverhang();
    for (int i = 0; i < n; ++i) {
        const Vec2 a = out[i], b = out[(i + 1) % n];
        if (std::abs(cross((b - a).normalized(), p.along())) > 1e-3) continue;
        const double s = p.upFrom(a);
        if (std::abs(s - extreme) > 0.5) continue;
        const double wanted = high ? target : -target;
        const double sign = dot((b - a).normalized().perp(), p.upslope());
        Polygon moved = moveEdge(out, i, sign * (wanted - s));
        if (moved.size() == out.size()) out = std::move(moved);
    }
    return out;
}
} // namespace

PropertiesPanel::PropertiesPanel(Workspace* ws, QWidget* parent) : QWidget(parent), ws_(ws) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(scroll);
    body_ = new QWidget;
    scroll->setWidget(body_);
    form_ = new QFormLayout(body_);
    form_->setRowWrapPolicy(QFormLayout::WrapLongRows);

    connect(ws_, &Workspace::documentChanged, this, &PropertiesPanel::scheduleRebuild);
    connect(ws_, &Workspace::selectionChanged, this, &PropertiesPanel::scheduleRebuild);
    connect(ws_, &Workspace::currentLevelChanged, this, &PropertiesPanel::scheduleRebuild);
    rebuild();
}

// Rebuilding replaces the widgets, so never do it inside one of their own
// signal handlers.
void PropertiesPanel::scheduleRebuild() {
    if (rebuildPending_) return;
    rebuildPending_ = true;
    QTimer::singleShot(0, this, [this] {
        rebuildPending_ = false;
        rebuild();
    });
}

void PropertiesPanel::apply(const QString& what, std::function<void(Document&)> change) {
    ws_->edit(what, change);
}

void PropertiesPanel::rebuild() {
    while (form_->rowCount() > 0) form_->removeRow(0);

    const Document& d = ws_->doc();
    const ObjectId id = ws_->selection();
    if (ws_->selectedIds().size() > 1) showMany(ws_->selectedIds());
    else if (const Wall* w = d.wall(id)) showWall(*w);
    else if (const Opening* o = d.opening(id)) showOpening(*o);
    else if (const Beam* b = d.beam(id)) showBeam(*b);
    else if (const Post* p = d.post(id)) showPost(*p);
    else if (const Roof* r = d.roof(id)) showRoof(*r);
    else if (const RoofPlane* p = d.roofPlane(id)) showRoofPlane(*p);
    else if (const FloorPlatform* f = Document::findIn(d.platforms, id)) showPlatform(*f);
    else if (const Room* rm = Document::findIn(d.rooms, id)) showRoom(*rm);
    else if (const Dimension* dm = d.dimension(id)) showDimension(*dm);
    else if (const ElevationPoint* e = d.elevationPoint(id)) showElevationPoint(*e);
    else showPlan();
}

// ---- Row helpers -----------------------------------------------------------

void PropertiesPanel::heading(const QString& text) {
    auto* label = new QLabel("<b>" + text.toHtmlEscaped() + "</b>");
    label->setContentsMargins(0, 8, 0, 2);
    form_->addRow(label);
}

void PropertiesPanel::info(const QString& label, const QString& value) {
    auto* v = new QLabel(value);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form_->addRow(label, v);
}

void PropertiesPanel::length(const QString& label, double value, std::function<void(Document&, double)> set,
                             double minimum) {
    auto* edit = new QLineEdit(feetInches(value));
    edit->setToolTip("Type feet and inches, like 12' 6 1/2\" or 150\"");
    connect(edit, &QLineEdit::editingFinished, this, [this, edit, value, set, minimum, label] {
        auto parsed = parseLength(edit->text().toStdString());
        if (!parsed || *parsed < minimum) {
            edit->setText(feetInches(value));
            return;
        }
        if (std::abs(*parsed - value) < 1e-9) return;
        const double v = *parsed;
        apply("Change " + label.toLower(), [set, v](Document& d) { set(d, v); });
    });
    form_->addRow(label, edit);
}

void PropertiesPanel::number(const QString& label, double value, double min, double max, int decimals,
                             const QString& suffix, std::function<void(Document&, double)> set) {
    auto* spin = new QDoubleSpinBox;
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setSuffix(suffix);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    connect(spin, &QDoubleSpinBox::editingFinished, this, [this, spin, value, set, label] {
        const double v = spin->value();
        if (std::abs(v - value) < 1e-9) return;
        apply("Change " + label.toLower(), [set, v](Document& d) { set(d, v); });
    });
    form_->addRow(label, spin);
}

void PropertiesPanel::check(const QString& label, bool value, std::function<void(Document&, bool)> set) {
    auto* box = new QCheckBox;
    box->setChecked(value);
    connect(box, &QCheckBox::toggled, this, [this, set, label](bool v) {
        apply("Change " + label.toLower(), [set, v](Document& d) { set(d, v); });
    });
    form_->addRow(label, box);
}

void PropertiesPanel::choice(const QString& label, const QStringList& items, int index,
                             std::function<void(Document&, int)> set) {
    auto* combo = new QComboBox;
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(12);
    combo->addItems(items);
    combo->setCurrentIndex(index);
    connect(combo, &QComboBox::activated, this, [this, set, label, index](int v) {
        if (v == index) return;
        apply("Change " + label.toLower(), [set, v](Document& d) { set(d, v); });
    });
    form_->addRow(label, combo);
}

void PropertiesPanel::text(const QString& label, const QString& value,
                           std::function<void(Document&, const std::string&)> set) {
    auto* edit = new QLineEdit(value);
    connect(edit, &QLineEdit::editingFinished, this, [this, edit, value, set, label] {
        if (edit->text() == value) return;
        const std::string v = edit->text().toStdString();
        apply("Change " + label.toLower(), [set, v](Document& d) { set(d, v); });
    });
    form_->addRow(label, edit);
}

void PropertiesPanel::note(const QString& html) {
    auto* label = new QLabel(html);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form_->addRow(label);
}

void PropertiesPanel::button(const QString& caption, std::function<void()> onClick) {
    auto* b = new QPushButton(caption);
    connect(b, &QPushButton::clicked, this, [onClick] { onClick(); });
    form_->addRow(b);
}

void PropertiesPanel::lengthAll(const QString& label, const Ids& ids,
                                std::function<double(const Document&, ObjectId)> get,
                                std::function<void(Document&, ObjectId, double)> set, double minimum) {
    const Document& doc = ws_->doc();
    const double first = get(doc, ids.front());
    const bool same = std::all_of(ids.begin(), ids.end(), [&](ObjectId id) { return std::abs(get(doc, id) - first) < 1e-6; });
    auto* edit = new QLineEdit(same ? feetInches(first) : QString());
    edit->setPlaceholderText("varies");
    edit->setToolTip("Type feet and inches, like 12' 6 1/2\" or 150\". Applies to all of them.");
    const QString shown = edit->text();
    connect(edit, &QLineEdit::editingFinished, this, [this, edit, shown, set, minimum, label, ids] {
        if (edit->text() == shown) return;
        auto parsed = parseLength(edit->text().toStdString());
        if (!parsed || *parsed < minimum) {
            edit->setText(shown);
            return;
        }
        const double v = *parsed;
        apply("Change " + label.toLower(), [set, v, ids](Document& d) {
            for (ObjectId id : ids) set(d, id, v);
        });
    });
    form_->addRow(label, edit);
}

void PropertiesPanel::choiceAll(const QString& label, const QStringList& items, const Ids& ids,
                                std::function<int(const Document&, ObjectId)> get,
                                std::function<void(Document&, ObjectId, int)> set) {
    const Document& doc = ws_->doc();
    const int first = get(doc, ids.front());
    const bool same = std::all_of(ids.begin(), ids.end(), [&](ObjectId id) { return get(doc, id) == first; });
    auto* combo = new QComboBox;
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(12);
    combo->addItems(items);
    if (!same) combo->insertItem(0, "Varies");
    combo->setCurrentIndex(same ? first : 0);
    connect(combo, &QComboBox::activated, this, [this, set, label, ids, same, first](int v) {
        if (!same) {
            if (v == 0) return;
            --v;
        } else if (v == first) {
            return;
        }
        apply("Change " + label.toLower(), [set, v, ids](Document& d) {
            for (ObjectId id : ids) set(d, id, v);
        });
    });
    form_->addRow(label, combo);
}

// Several objects selected with a Shift box or Shift-click. When they are
// all one kind, the rows they share edit them all together.
void PropertiesPanel::showMany(const Ids& ids) {
    const Document& doc = ws_->doc();
    auto all = [&](auto test) { return std::all_of(ids.begin(), ids.end(), [&](ObjectId id) { return test(id); }); };
    const auto isDoor = [&](ObjectId id) { return doc.opening(id) && doc.opening(id)->kind == OpeningKind::Door; };
    const auto isWindow = [&](ObjectId id) { return doc.opening(id) && doc.opening(id)->kind == OpeningKind::Window; };

    if (all([&](ObjectId id) { return doc.opening(id) != nullptr; })) {
        heading(QString("%1 %2").arg(ids.size()).arg(all(isDoor) ? "doors" : all(isWindow) ? "windows" : "doors and windows"));
        lengthAll("Width", ids, [](const Document& d, ObjectId id) { return d.opening(id)->width; },
                  [](Document& d, ObjectId id, double v) { d.opening(id)->width = v; }, 1.0);
        lengthAll("Height", ids, [](const Document& d, ObjectId id) { return d.opening(id)->height; },
                  [](Document& d, ObjectId id, double v) { d.opening(id)->height = v; }, 1.0);
        lengthAll("Sill height", ids, [](const Document& d, ObjectId id) { return d.opening(id)->sillHeight; },
                  [](Document& d, ObjectId id, double v) { d.opening(id)->sillHeight = v; });
        lengthAll("Top height", ids, [](const Document& d, ObjectId id) { return d.opening(id)->topHeight(); },
                  [](Document& d, ObjectId id, double v) {
                      Opening* x = d.opening(id);
                      x->height = std::max(1.0, v - x->sillHeight);
                  }, 1.0);
        button("Clear headers", [this, ids] {
            apply("Clear headers", [ids](Document& d) {
                for (ObjectId id : ids) d.opening(id)->headerMemberId.clear();
            });
        });
    } else if (all([&](ObjectId id) { return doc.wall(id) != nullptr; })) {
        heading(QString("%1 walls").arg(ids.size()));
        QStringList types{"Custom (set by hand)"};
        std::vector<ObjectId> typeIds{kNoId};
        for (const auto& t : doc.wallTypes) {
            types << QString::fromStdString(t.name);
            typeIds.push_back(t.id);
        }
        choiceAll("Wall type", types, ids,
                  [typeIds](const Document& d, ObjectId id) {
                      const auto it = std::find(typeIds.begin(), typeIds.end(), d.wall(id)->wallTypeId);
                      return it == typeIds.end() ? 0 : static_cast<int>(it - typeIds.begin());
                  },
                  [typeIds](Document& d, ObjectId id, int v) {
                      Wall* x = d.wall(id);
                      const bool wasDivider = x->roomDivider;
                      x->wallTypeId = typeIds[v];
                      const WallType* t = d.wallType(x->wallTypeId);
                      x->roomDivider = t && t->isDivider();
                      if (wasDivider && !x->roomDivider) {
                          x->bearing = !t || t->kind == WallKind::Exterior;
                          if (!t) x->thickness = 5.5;
                      }
                  });
        lengthAll("Height (0 = level)", ids, [](const Document& d, ObjectId id) { return d.wall(id)->height; },
                  [](Document& d, ObjectId id, double v) { d.wall(id)->height = v; });
        choiceAll("Bearing", {"No", "Yes"}, ids, [](const Document& d, ObjectId id) { return d.wall(id)->bearing ? 1 : 0; },
                  [](Document& d, ObjectId id, int v) { d.wall(id)->bearing = v == 1; });
    } else if (all([&](ObjectId id) { return doc.post(id) != nullptr; })) {
        heading(QString("%1 posts").arg(ids.size()));
        QStringList sizes;
        std::vector<std::string> memberIds;
        for (const auto& m : postCatalog()) {
            sizes << QString::fromStdString(m.name);
            memberIds.push_back(m.id);
        }
        choiceAll("Size", sizes, ids,
                  [memberIds](const Document& d, ObjectId id) {
                      const auto it = std::find(memberIds.begin(), memberIds.end(), d.post(id)->memberId);
                      return it == memberIds.end() ? 0 : static_cast<int>(it - memberIds.begin());
                  },
                  [memberIds](Document& d, ObjectId id, int v) { d.post(id)->memberId = memberIds[v]; });
        lengthAll("Height (0 = level)", ids, [](const Document& d, ObjectId id) { return d.post(id)->height; },
                  [](Document& d, ObjectId id, double v) { d.post(id)->height = v; });
    } else if (all([&](ObjectId id) { return doc.dimension(id) != nullptr; })) {
        heading(QString("%1 dimensions").arg(ids.size()));
        lengthAll("Offset from the points", ids, [](const Document& d, ObjectId id) { return std::abs(d.dimension(id)->offset); },
                  [](Document& d, ObjectId id, double v) {
                      Dimension* x = d.dimension(id);
                      x->offset = x->offset < 0 ? -v : v;
                      x->automatic = false;
                  });
    } else if (all([&](ObjectId id) { return doc.elevationPoint(id) != nullptr; })) {
        heading(QString("%1 elevation points").arg(ids.size()));
        lengthAll("Elevation", ids, [](const Document& d, ObjectId id) { return d.elevationPoint(id)->elevation; },
                  [](Document& d, ObjectId id, double v) { d.elevationPoint(id)->elevation = v; }, -1e6);
    } else if (all([&](ObjectId id) { return Document::findIn(doc.rooms, id) != nullptr; })) {
        heading(QString("%1 rooms").arg(ids.size()));
        QStringList types;
        for (int t = 0; t <= static_cast<int>(RoomType::Other); ++t) types << toString(static_cast<RoomType>(t));
        choiceAll("Type", types, ids, [](const Document& d, ObjectId id) { return static_cast<int>(Document::findIn(d.rooms, id)->type); },
                  [](Document& d, ObjectId id, int v) { d.room(id)->type = static_cast<RoomType>(v); });
        choiceAll("Floor", {"Level's floor framing", "Own joists", "Concrete slab"}, ids,
                  [](const Document& d, ObjectId id) { return static_cast<int>(Document::findIn(d.rooms, id)->floor.kind); },
                  [](Document& d, ObjectId id, int v) { d.room(id)->floor.kind = static_cast<FloorSystemKind>(v); });
    } else {
        heading(QString("%1 objects").arg(ids.size()));
        // Count them by kind, in the order they were picked.
        std::vector<std::pair<QString, int>> counts;
        for (ObjectId id : ids) {
            QString kind = ws_->describe(id);
            if (Document::findIn(doc.rooms, id)) kind = "Room";
            else kind.remove(QRegularExpression(" \\d+$"));
            auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == kind; });
            if (it == counts.end()) counts.push_back({kind, 1});
            else ++it->second;
        }
        for (const auto& [kind, n] : counts) info(kind, QString::number(n));
        note("<span style='color:#666'>Pick a tool before Shift-dragging to select only that kind, like Windows "
             "to select every window. Then their shared properties can be edited together here.</span>");
    }
    note("<span style='color:#666'>Drag any of them to move them all, or press Delete to delete them.</span>");
}

// Loads on a wall or beam split by where they come from; for a wall, also
// its own weight and the total it passes to what is below.
void PropertiesPanel::showLoadBreakdown(ObjectId id) {
    const SupportLoads* s = ws_->loads().wall(id);
    const bool isWall = s != nullptr;
    if (!s) s = ws_->loads().beam(id);
    heading("Loads (peak plf)");
    if (!s) return;

    QString html = "<table cellspacing='0' cellpadding='3' width='100%'>"
                   "<tr><th align='left'>From</th><th align='right'>D</th><th align='right'>L</th>"
                   "<th align='right'>Lr</th><th align='right'>S</th></tr>";
    auto row = [&html](const QString& name, const CaseLoads& c, bool bold = false) {
        const QString b0 = bold ? "<b>" : "", b1 = bold ? "</b>" : "";
        html += QString("<tr><td>%1%2%3</td>").arg(b0, name, b1);
        for (LoadCase lc : kAllCases) html += QString("<td align='right'>%1%2%3</td>").arg(b0, plf(c[lc]), b1);
        html += "</tr>";
    };
    for (const auto& g : summarizeByOrigin(s->lineLoads)) row(toString(g.origin), g.summary.peakPlf);
    if (s->lineLoads.empty()) html += "<tr><td colspan='5'><i>Nothing lands on top.</i></td></tr>";
    const auto top = s->topLoads();
    if (!s->lineLoads.empty()) row("Total on top", top.peakPlf, true);
    const auto bottom = s->bottomLoads();
    if (isWall) {
        row("This wall's weight", CaseLoads::of(s->selfWeight.wStart[LoadCase::Dead]));
        row("Total at the bottom", bottom.peakPlf, true);
    }
    html += "</table>";
    note(html);

    if (top.governingPlf > 0.5)
        info("Governing on top", QString("%1 plf (%2)").arg(top.governingPlf, 0, 'f', 0).arg(QString::fromStdString(top.governingCombo)));
    if (isWall)
        info("Governing at bottom", QString("%1 plf (%2)").arg(bottom.governingPlf, 0, 'f', 0).arg(QString::fromStdString(bottom.governingCombo)));
    for (const auto& p : s->pointLoads) {
        double total = 0.0;
        for (double v : p.p.v) total += v;
        info("Point load", QString("%1 lb at %2 from %3")
                               .arg(total, 0, 'f', 0)
                               .arg(feetInches(p.position), QString::fromStdString(p.source)));
    }
    if (const Wall* w = ws_->doc().wall(id); w && isWall)
        note(QString("<span style='color:#666'>The wall's weight is %1 psf over its %2 height. Each row is "
                     "a peak, and peaks can sit at different points along the wall, so rows may not add up "
                     "exactly to the totals.</span>")
                 .arg(w->selfWeightPsf)
                 .arg(feetInches(ws_->doc().wallHeight(*w))));
}

// ---- Object pages ----------------------------------------------------------

void PropertiesPanel::showPlan() {
    const Document& d = ws_->doc();
    heading("Plan");
    text("Name", QString::fromStdString(d.name), [](Document& doc, const std::string& v) { doc.name = v; });

    if (const Level* l = d.level(ws_->currentLevel())) {
        const ObjectId id = l->id;
        heading("Level");
        text("Name", QString::fromStdString(l->name), [id](Document& doc, const std::string& v) { doc.level(id)->name = v; });
        length("Floor elevation", l->elevation, [id](Document& doc, double v) { doc.level(id)->elevation = v; }, -1e6);
        length("Wall height", l->wallHeight, [id](Document& doc, double v) { doc.level(id)->wallHeight = v; }, 12);
        length("Floor framing depth", l->floorThickness,
               [id](Document& doc, double v) { doc.level(id)->floorThickness = v; });
    }

    heading("Site snow (ASCE 7)");
    number("Ground snow pg", d.site.groundSnowPsf, 0, 400, 1, " psf", [](Document& doc, double v) { doc.site.groundSnowPsf = v; });
    number("Exposure Ce", d.site.exposureCe, 0.7, 1.3, 2, "", [](Document& doc, double v) { doc.site.exposureCe = v; });
    number("Thermal Ct", d.site.thermalCt, 0.85, 1.3, 2, "", [](Document& doc, double v) { doc.site.thermalCt = v; });
    number("Importance Is", d.site.importanceIs, 0.8, 1.2, 2, "", [](Document& doc, double v) { doc.site.importanceIs = v; });
    info("Flat roof snow pf", QString("%1 psf").arg(flatRoofSnowPsf(d.site), 0, 'f', 1));
    showTerrain();
}

void PropertiesPanel::showTerrain() {
    const Document& d = ws_->doc();
    heading("Terrain");
    info("Elevation points", QString::number(d.terrain.points.size()));
    length("Contour interval", d.terrain.contourInterval,
           [](Document& doc, double v) { doc.terrain.contourInterval = v; }, 1.0);
    note("<span style='color:#666'>Use the Elevation point tool on the ground level to add spot elevations. "
         "Contours are drawn between three or more points. Elevations are measured from the lowest level's "
         "floor.</span>");
}

void PropertiesPanel::showDimension(const Dimension& dim) {
    const ObjectId id = dim.id;
    const Document& doc = ws_->doc();
    heading(ws_->describe(id));
    info("Measures", feetInches(dim.length()));
    length("Offset from the points", std::abs(dim.offset), [id](Document& d, double v) {
        Dimension* x = d.dimension(id);
        x->offset = x->offset < 0 ? -v : v;
        x->automatic = false;
    });
    check("Flip to the other side", dim.offset < 0, [id](Document& d, bool v) {
        Dimension* x = d.dimension(id);
        x->offset = v ? -std::abs(x->offset) : std::abs(x->offset);
        x->automatic = false;
    });
    choice("Walls measure to", {"Outside edge of framing", "Center of framing"},
           doc.dimensionTo == DimensionTo::FramingEdge ? 0 : 1, [](Document& d, int v) {
               setDimensionTo(d, v == 0 ? DimensionTo::FramingEdge : DimensionTo::FramingCenter);
           });
    note("<span style='color:#666'>Drag a square node to move that end to another point; it snaps to wall "
         "framing corners, openings, posts and beam ends. Drag the diamond, or anywhere on the line, to move the "
         "whole string in or out.</span>");
    if (dim.automatic)
        note("<span style='color:#666'>Made by Dimension exterior walls. Running that again replaces it unless "
             "you have edited it.</span>");
}

void PropertiesPanel::showElevationPoint(const ElevationPoint& e) {
    const ObjectId id = e.id;
    heading(ws_->describe(id));
    length("Elevation", e.elevation, [id](Document& d, double v) { d.elevationPoint(id)->elevation = v; }, -1e6);
    info("Position", QString("%1, %2").arg(feetInches(e.position.x), feetInches(e.position.y)));
    note("<span style='color:#666'>Measured from the lowest level's floor; negative is below it. Type a minus "
         "sign for grade below the floor, like -1' 6\".</span>");
    showTerrain();
}

void PropertiesPanel::showWall(const Wall& w) {
    const ObjectId id = w.id;
    const Document& doc = ws_->doc();
    heading(ws_->describe(id));
    length("Length", w.length(), [id](Document& d, double v) {
        Wall* x = d.wall(id);
        x->b = x->a + (x->b - x->a).normalized() * v;
    }, 1.0);

    // Wall type: a named build-up, or "set by hand".
    QStringList types{"Custom (set by hand)"};
    std::vector<ObjectId> typeIds{kNoId};
    int current = 0;
    for (const auto& t : doc.wallTypes) {
        if (t.id == w.wallTypeId) current = static_cast<int>(types.size());
        types << QString::fromStdString(t.name);
        typeIds.push_back(t.id);
    }
    choice("Wall type", types, current, [id, typeIds](Document& d, int v) {
        Wall* x = d.wall(id);
        const bool wasDivider = x->roomDivider;
        x->wallTypeId = typeIds[v];
        const WallType* t = d.wallType(x->wallTypeId);
        x->roomDivider = t && t->isDivider();
        if (wasDivider && !x->roomDivider) {
            x->bearing = !t || t->kind == WallKind::Exterior;
            if (!t) x->thickness = 5.5;
        }
    });
    if (w.roomDivider) {
        note("<span style='color:#666'>A room divider is an invisible wall: it only separates rooms. It carries "
             "no load, isn't drawn in 3D and can't take doors or windows.</span>");
        return;
    }
    const WallType* type = doc.wallType(w.wallTypeId);
    if (type) {
        info("Thickness", feetInches(w.thickness));
        info("Self weight", QString("%1 psf").arg(w.selfWeightPsf, 0, 'f', 1));
    } else {
        length("Thickness", w.thickness, [id](Document& d, double v) { d.wall(id)->thickness = v; }, 1.0);
        number("Self weight", w.selfWeightPsf, 0, 200, 1, " psf", [id](Document& d, double v) { d.wall(id)->selfWeightPsf = v; });
    }
    length("Height (0 = level)", w.height, [id](Document& d, double v) { d.wall(id)->height = v; });
    check("Bearing", w.bearing, [id](Document& d, bool v) { d.wall(id)->bearing = v; });
    if (type) showWallType(w, *type);
    if (w.bearing) showLoadBreakdown(id);
}

// The wall type's framing and layers. Changing them changes every wall of the type.
void PropertiesPanel::showWallType(const Wall& w, const WallType& t) {
    const ObjectId typeId = t.id;
    const ObjectId wallId = w.id;
    int count = 0;
    for (const auto& x : ws_->doc().walls)
        if (x.wallTypeId == typeId) ++count;
    heading("Wall type framing");
    text("Type name", QString::fromStdString(t.name), [typeId](Document& d, const std::string& v) { d.wallType(typeId)->name = v; });
    choice("Kind", {"Exterior", "Interior"}, t.kind == WallKind::Interior ? 1 : 0,
           [typeId](Document& d, int v) { d.wallType(typeId)->kind = v == 1 ? WallKind::Interior : WallKind::Exterior; });
    const double depths[] = {3.5, 5.5, 7.25};
    int depthIndex = 0;
    for (int i = 0; i < 3; ++i)
        if (std::abs(t.studDepth - depths[i]) < 0.01) depthIndex = i;
    choice("Studs", {"2x4", "2x6", "2x8"}, depthIndex, [typeId, depths](Document& d, int v) { d.wallType(typeId)->studDepth = depths[v]; });
    const double spacings[] = {12.0, 16.0, 19.2, 24.0};
    int spacingIndex = 1;
    for (int i = 0; i < 4; ++i)
        if (std::abs(t.studSpacing - spacings[i]) < 0.01) spacingIndex = i;
    choice("Stud spacing", {"12\" o.c.", "16\" o.c.", "19.2\" o.c.", "24\" o.c."}, spacingIndex,
           [typeId, spacings](Document& d, int v) { d.wallType(typeId)->studSpacing = spacings[v]; });
    check("Insulated", t.insulated, [typeId](Document& d, bool v) { d.wallType(typeId)->insulated = v; });
    if (t.insulated)
        text("Insulation", QString::fromStdString(t.insulation),
             [typeId](Document& d, const std::string& v) { d.wallType(typeId)->insulation = v; });
    length("Drywall", t.interiorFinish, [typeId](Document& d, double v) { d.wallType(typeId)->interiorFinish = v; });
    if (t.kind == WallKind::Exterior) {
        length("Sheathing", t.sheathing, [typeId](Document& d, double v) { d.wallType(typeId)->sheathing = v; });
        length("Siding", t.siding, [typeId](Document& d, double v) { d.wallType(typeId)->siding = v; });
    }
    note(QString("<span style='color:#666'>Changes here apply to all %1 wall%2 of this type.</span>")
             .arg(count)
             .arg(count == 1 ? "" : "s"));
    button("Make a new type from this one", [this, typeId, wallId] {
        apply("Add wall type", [typeId, wallId](Document& d) {
            WallType copy = *d.wallType(typeId);
            copy.id = d.newId();
            copy.name += " (copy)";
            d.wallTypes.push_back(copy);
            d.wall(wallId)->wallTypeId = copy.id;
        });
    });
}

void PropertiesPanel::showPost(const Post& p) {
    const ObjectId id = p.id;
    const Document& doc = ws_->doc();
    heading(QString("Post %1").arg(id));
    QStringList sizes;
    int current = 0;
    std::vector<std::string> ids;
    for (const auto& m : postCatalog()) {
        if (m.id == p.memberId) current = static_cast<int>(ids.size());
        sizes << QString::fromStdString(m.name);
        ids.push_back(m.id);
    }
    choice("Size", sizes, current, [id, ids](Document& d, int v) { d.post(id)->memberId = ids[v]; });
    length("Height (0 = level)", p.height, [id](Document& d, double v) { d.post(id)->height = v; });

    heading("Load");
    const PostLoads* load = ws_->loads().post(id);
    if (!load || load->load.isZero()) {
        note("<span style='color:#666'>Nothing bears on this post yet. Put a beam end over it.</span>");
        return;
    }
    QString cases;
    for (LoadCase c : kAllCases)
        if (load->load[c] > 0.5) cases += QString("%1 %2 lb, ").arg(shortName(c)).arg(load->load[c], 0, 'f', 0);
    cases.chop(2);
    info("Carries", cases);
    for (const auto& s : load->sources) info("From", QString::fromStdString(s));
    const double height = doc.postHeight(p);
    if (const PostMember* m = findPost(p.memberId)) {
        const PostCheck check = checkPost(*m, load->load, height);
        info("Check", QString("%1 lb of %2 lb allowed (%3%), %4")
                          .arg(check.demand, 0, 'f', 0)
                          .arg(check.capacity, 0, 'f', 0)
                          .arg(check.ratio * 100, 0, 'f', 0)
                          .arg(QString::fromStdString(check.combo)));
        note(check.passes ? "<span style='color:#2a7d2a'><b>OK</b> for this load.</span>"
                          : "<span style='color:#c0392b'><b>Too small</b> for this load.</span>");
    }
    if (const PostMember* r = recommendPost(load->load, height)) {
        info("Smallest that works", QString::fromStdString(r->name));
        if (r->id != p.memberId) {
            const std::string rid = r->id;
            button("Use " + QString::fromStdString(r->name), [this, id, rid] {
                apply("Size post", [id, rid](Document& d) { d.post(id)->memberId = rid; });
            });
        }
    } else {
        note("<span style='color:#c0392b'>No post in the catalog carries this load at this height.</span>");
    }
    note("<span style='color:#666'>NDS column check, pinned ends, unbraced full height, Douglas Fir-Larch. "
         "Check the footing under it separately.</span>");
}

void PropertiesPanel::showOpening(const Opening& o) {
    const ObjectId id = o.id;
    heading(ws_->describe(id));
    choice("Type", {"Door", "Window"}, o.kind == OpeningKind::Door ? 0 : 1,
           [id](Document& d, int v) { d.opening(id)->kind = v == 0 ? OpeningKind::Door : OpeningKind::Window; });
    length("Width", o.width, [id](Document& d, double v) { d.opening(id)->width = v; }, 1.0);
    length("Height", o.height, [id](Document& d, double v) { d.opening(id)->height = v; }, 1.0);
    length("Sill height", o.sillHeight, [id](Document& d, double v) { d.opening(id)->sillHeight = v; });
    // Top height moves the top and keeps the sill, the way windows are set to line up with door heads.
    length("Top height", o.topHeight(), [id](Document& d, double v) {
        Opening* x = d.opening(id);
        x->height = std::max(1.0, v - x->sillHeight);
    }, 1.0);
    length("Center from wall start", o.offset, [id](Document& d, double v) { d.opening(id)->offset = v; });

    heading("Header");
    const Member* m = findMember(o.headerMemberId);
    info("Header", m ? QString::fromStdString(m->name) : QString("Not picked yet"));
    if (const Wall* w = ws_->doc().wall(o.wallId); w && w->bearing)
        button("Size header…", [this, id] { emit sizeRequested(id); });
    else
        note("<span style='color:#666'>The wall isn't bearing, so the header carries only the wall above.</span>");
    if (m)
        button("Clear header", [this, id] { apply("Clear header", [id](Document& d) { d.opening(id)->headerMemberId.clear(); }); });
}

void PropertiesPanel::showBeam(const Beam& b) {
    const ObjectId id = b.id;
    heading(QString("Beam %1").arg(id));
    info("Span", feetInches(b.length()));
    const Member* m = findMember(b.memberId);
    info("Member", m ? QString::fromStdString(m->name) : QString("Not sized yet"));
    button("Size beam…", [this, id] { emit sizeRequested(id); });
    showLoadBreakdown(id);
}

void PropertiesPanel::showRoof(const Roof& r) {
    const ObjectId id = r.id;
    heading(QString("Roof %1").arg(id));
    choice("Style", {"Gable", "Hip"}, r.style == RoofStyle::Gable ? 0 : 1,
           [id](Document& d, int v) { d.roof(id)->style = v == 0 ? RoofStyle::Gable : RoofStyle::Hip; });
    choice("Ridge runs", {"East-west (X)", "North-south (Y)"}, r.ridgeAxis == Axis::X ? 0 : 1,
           [id](Document& d, int v) { d.roof(id)->ridgeAxis = v == 0 ? Axis::X : Axis::Y; });
    length("Width", r.footprint.width(), [id](Document& d, double v) {
        Roof* x = d.roof(id);
        x->footprint.max.x = x->footprint.min.x + v;
    }, 12.0);
    length("Depth", r.footprint.height(), [id](Document& d, double v) {
        Roof* x = d.roof(id);
        x->footprint.max.y = x->footprint.min.y + v;
    }, 12.0);
    if (r.framing == RoofFraming::Rafters && r.style == RoofStyle::Gable) {
        check("Structural ridge beam", r.structuralRidge, [id](Document& d, bool v) { d.roof(id)->structuralRidge = v; });
        if (r.hasRidgeBeam()) button("Size ridge beam…", [this, id] { emit sizeRequested(id); });
    }
    showRoofSettings(id, r);
}

void PropertiesPanel::showRoofPlane(const RoofPlane& p) {
    const ObjectId id = p.id;
    heading(QString("Roof plane %1").arg(id));
    info("Slopes down toward", "the " + compass(p.upslope() * -1.0));
    info("Bearing line", feetInches(distance(p.a, p.b)));
    info("Width", feetInches(p.width()));
    choice("High side", {"Bears on a wall or beam (shed)", "Leans on other planes at a ridge"}, p.highBearing ? 0 : 1,
           [id](Document& d, int v) { d.roofPlane(id)->highBearing = v == 0; });
    length("Run (bearing line to high side)", p.runLength(), [id](Document& d, double v) {
        RoofPlane* x = d.roofPlane(id);
        x->outline = setParallelEdges(*x, true, v);
    }, 12.0);
    length("Eave overhang", p.eaveOverhang(), [id](Document& d, double v) {
        RoofPlane* x = d.roofPlane(id);
        x->outline = setParallelEdges(*x, false, v);
    });
    button("Flip the slope", [this, id] {
        apply("Flip roof plane", [id](Document& d) {
            RoofPlane* x = d.roofPlane(id);
            // The bearing line moves to the old high side, reversed so the plane rises the other way.
            const Vec2 up = x->upslope() * x->runLength();
            const Vec2 a = x->b + up, b = x->a + up;
            x->a = a;
            x->b = b;
        });
    });
    showRoofSettings(id, p, true);
    note(p.highBearing
             ? "<span style='color:#666'>Drag the corner and edge handles on the plan to reshape it. Use Join roof "
               "planes to trim an edge to a hip or valley. The bearing line and the high side each need a wall or "
               "beam beneath them.</span>"
             : "<span style='color:#666'>Drag the corner and edge handles on the plan to reshape it. Use Join roof "
               "planes to trim an edge to a hip or valley. With a ridge board, the bearing line carries the whole "
               "plane.</span>");
}

// Framing, slope and loads shared by roofs and roof planes.
void PropertiesPanel::showRoofSettings(ObjectId id, const RoofSettings& r, bool plane) {
    const Site& site = ws_->doc().site;
    const bool trusses = r.framing == RoofFraming::Trusses;
    choice("Structure", {"Rafters (stick framed)", "Manufactured trusses"}, trusses ? 1 : 0,
           [id](Document& d, int v) { d.roofSettings(id)->framing = v == 0 ? RoofFraming::Rafters : RoofFraming::Trusses; });
    number("Pitch", r.pitch, 0.25, 24, 2, " in 12", [id](Document& d, double v) { d.roofSettings(id)->pitch = v; });
    if (!plane) length("Overhang", r.overhang, [id](Document& d, double v) { d.roofSettings(id)->overhang = v; });

    heading("Roof loads");
    number("Dead load", r.deadPsf, 0, 100, 1, " psf", [id](Document& d, double v) { d.roofSettings(id)->deadPsf = v; });
    info("Roof live load", "20 psf (IRC R301.6)");
    number("Ground snow pg (whole site)", site.groundSnowPsf, 0, 400, 1, " psf",
           [](Document& d, double v) { d.site.groundSnowPsf = v; });
    const double calculated = roofSnowPsf(site, r.slopeRadians());
    choice("Snow load", {QString("Calculated, %1 psf").arg(calculated, 0, 'f', 1), "Custom"}, r.customSnow ? 1 : 0,
           [id, calculated](Document& d, int v) {
               RoofSettings* x = d.roofSettings(id);
               x->customSnow = v == 1;
               if (x->customSnow && x->snowPsf <= 0.0) x->snowPsf = std::round(calculated);
           });
    if (r.customSnow)
        number("Roof snow load", r.snowPsf, 0, 400, 1, " psf", [id](Document& d, double v) { d.roofSettings(id)->snowPsf = v; });

    heading("Ceiling");
    check(trusses ? "Ceiling on bottom chords" : "Ceiling joists", r.hasCeiling,
          [id](Document& d, bool v) { d.roofSettings(id)->hasCeiling = v; });
    if (r.hasCeiling) {
        number("Ceiling dead load", r.ceilingDeadPsf, 0, 50, 1, " psf",
               [id](Document& d, double v) { d.roofSettings(id)->ceilingDeadPsf = v; });
        choice("Attic", {"No storage (10 psf)", "Limited storage (20 psf)"}, r.atticLivePsf > 15 ? 1 : 0,
               [id](Document& d, int v) { d.roofSettings(id)->atticLivePsf = v == 0 ? 10.0 : 20.0; });
    }
    note(trusses ? "<span style='color:#666'>Trusses span from eave to eave, so interior walls carry no roof "
                   "or ceiling load. The truss maker designs the trusses themselves.</span>"
                 : "<span style='color:#666'>Rafters bear on the eave walls; ceiling joists also bear on "
                   "interior bearing walls they cross.</span>");
}

void PropertiesPanel::showPlatform(const FloorPlatform& f) {
    const ObjectId id = f.id;
    heading(QString("Floor framing %1").arg(id));
    choice("Joists run", {"East-west (X)", "North-south (Y)"}, f.joistAxis == Axis::X ? 0 : 1,
           [id](Document& d, int v) { d.platform(id)->joistAxis = v == 0 ? Axis::X : Axis::Y; });
    number("Dead load", f.deadPsf, 0, 200, 1, " psf", [id](Document& d, double v) { d.platform(id)->deadPsf = v; });
    number("Live load", f.livePsf, 0, 250, 1, " psf", [id](Document& d, double v) { d.platform(id)->livePsf = v; });
    choice("Live load from IRC", {"Choose…", "Living areas (40)", "Sleeping rooms (30)", "Deck (40)", "Balcony (60)", "Garage (50)"}, 0,
           [id](Document& d, int v) {
               const double values[] = {0, 40, 30, 40, 60, 50};
               if (v > 0) d.platform(id)->livePsf = values[v];
           });
    info("Size", QString("%1 x %2").arg(feetInches(f.area.width()), feetInches(f.area.height())));
}

void PropertiesPanel::showRoom(const Room& r) {
    const ObjectId id = r.id;
    heading("Room");
    note("<span style='color:#666'>Rooms fill the areas walls enclose. Use the Room divider tool to split an open "
         "area into rooms.</span>");
    text("Name", QString::fromStdString(r.name), [id](Document& d, const std::string& v) { d.room(id)->name = v; });
    QStringList types;
    for (int t = 0; t <= static_cast<int>(RoomType::Other); ++t)
        types << QString("%1 (%2 psf live)").arg(toString(static_cast<RoomType>(t))).arg(defaultLiveLoadPsf(static_cast<RoomType>(t)));
    choice("Type", types, static_cast<int>(r.type), [id](Document& d, int v) { d.room(id)->type = static_cast<RoomType>(v); });
    info("Area", QString("%1 sq ft").arg(area(r.outline) / kSqInPerSqFt, 0, 'f', 0));
    showRoomFloor(r);
}

// The floor system under one room: the level's framing, its own joists, or a slab.
void PropertiesPanel::showRoomFloor(const Room& r) {
    const ObjectId id = r.id;
    const Document& doc = ws_->doc();
    const RoomFloor& f = r.floor;
    heading("Floor system");
    choice("Floor", {"Level's floor framing", "Own joists", "Concrete slab"}, static_cast<int>(f.kind),
           [id](Document& d, int v) { d.room(id)->floor.kind = static_cast<FloorSystemKind>(v); });
    if (f.kind == FloorSystemKind::LevelFraming) return;

    if (f.kind == FloorSystemKind::Joists) {
        text("Joists", QString::fromStdString(f.joists), [id](Document& d, const std::string& v) { d.room(id)->floor.joists = v; });
        check("Own joist direction", f.customJoistAxis, [id](Document& d, bool v) { d.room(id)->floor.customJoistAxis = v; });
        if (f.customJoistAxis)
            choice("Joists run", {"East-west (X)", "North-south (Y)"}, f.joistAxis == Axis::X ? 0 : 1,
                   [id](Document& d, int v) { d.room(id)->floor.joistAxis = v == 0 ? Axis::X : Axis::Y; });
    } else {
        length("Slab thickness", f.slabThickness, [id](Document& d, double v) { d.room(id)->floor.slabThickness = v; }, 1.0);
        text("Slab note", QString::fromStdString(f.slabNote), [id](Document& d, const std::string& v) { d.room(id)->floor.slabNote = v; });
    }
    length("Drop below the level's floor", f.drop, [id](Document& d, double v) { d.room(id)->floor.drop = v; });
    number(f.kind == FloorSystemKind::Slab ? "Extra dead load" : "Dead load", f.deadPsf, 0, 200, 1, " psf",
           [id](Document& d, double v) { d.room(id)->floor.deadPsf = v; });
    number("Live load (0 = room type)", f.livePsf, 0, 250, 1, " psf", [id](Document& d, double v) { d.room(id)->floor.livePsf = v; });

    const bool onGround = doc.levelBelow(r.levelId) == nullptr;
    if (f.kind == FloorSystemKind::Slab) {
        const double slab = 150.0 * f.slabThickness / 12.0;
        info("Slab weight", QString("%1 psf").arg(slab, 0, 'f', 0));
        note(onGround ? "<span style='color:#666'>This slab is on the lowest level, so it bears on the ground and "
                        "puts no load on walls or beams.</span>"
                      : "<span style='color:#666'>This slab is on an upper level, so its weight and the room's live "
                        "load go to the walls and beams below in place of the level's floor framing.</span>");
    } else {
        note(onGround ? "<span style='color:#666'>The lowest level bears on the ground, so this room's joists "
                        "carry no load to walls or beams here.</span>"
                      : "<span style='color:#666'>This room's floor loads replace the level's framing over the "
                        "room and go to the walls and beams below.</span>");
    }
}
