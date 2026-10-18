#include "PropertiesPanel.h"

#include "Workspace.h"

#include "oa/core/units.h"
#include "oa/structural/members.h"
#include "oa/structural/snow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

using namespace oa;
using namespace oa::structural;

namespace {
QString feetInches(double inches) { return QString::fromStdString(formatFeetInches(inches, 16)); }
} // namespace

PropertiesPanel::PropertiesPanel(Workspace* ws, QWidget* parent) : QWidget(parent), ws_(ws) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scroll);
    body_ = new QWidget;
    scroll->setWidget(body_);
    form_ = new QFormLayout(body_);

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
    if (const Wall* w = d.wall(id)) showWall(*w);
    else if (const Opening* o = d.opening(id)) showOpening(*o);
    else if (const Beam* b = d.beam(id)) showBeam(*b);
    else if (const Roof* r = d.roof(id)) showRoof(*r);
    else if (const FloorPlatform* f = Document::findIn(d.platforms, id)) showPlatform(*f);
    else if (const Room* rm = Document::findIn(d.rooms, id)) showRoom(*rm);
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

void PropertiesPanel::button(const QString& caption, std::function<void()> onClick) {
    auto* b = new QPushButton(caption);
    connect(b, &QPushButton::clicked, this, [onClick] { onClick(); });
    form_->addRow(b);
}

// Peak loads on top of a wall or beam from the current takedown.
void PropertiesPanel::showSupportLoads(int kind, ObjectId id) {
    const SupportLoads* s = ws_->loads().find(static_cast<SupportKind>(kind), id);
    heading("Loads on top");
    if (!s || (s->lineLoads.empty() && s->pointLoads.empty())) {
        info("", "Nothing lands on it.");
        return;
    }
    const auto sum = s->topLoads();
    for (LoadCase c : kAllCases)
        if (sum.peakPlf[c] > 0.5) info(QString("%1, peak").arg(shortName(c)), QString("%1 plf").arg(sum.peakPlf[c], 0, 'f', 0));
    if (sum.governingPlf > 0.5)
        info("Governing", QString("%1 plf (%2)").arg(sum.governingPlf, 0, 'f', 0).arg(QString::fromStdString(sum.governingCombo)));
    for (const auto& p : s->pointLoads) {
        double total = 0.0;
        for (double v : p.p.v) total += v;
        info("Point load", QString("%1 lb at %2 from %3")
                               .arg(total, 0, 'f', 0)
                               .arg(feetInches(p.position), QString::fromStdString(p.source)));
    }
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
}

void PropertiesPanel::showWall(const Wall& w) {
    const ObjectId id = w.id;
    heading(QString("Wall %1").arg(id));
    length("Length", w.length(), [id](Document& d, double v) {
        Wall* x = d.wall(id);
        x->b = x->a + (x->b - x->a).normalized() * v;
    }, 1.0);
    length("Thickness", w.thickness, [id](Document& d, double v) { d.wall(id)->thickness = v; }, 1.0);
    length("Height (0 = level)", w.height, [id](Document& d, double v) { d.wall(id)->height = v; });
    check("Bearing", w.bearing, [id](Document& d, bool v) { d.wall(id)->bearing = v; });
    number("Self weight", w.selfWeightPsf, 0, 200, 1, " psf", [id](Document& d, double v) { d.wall(id)->selfWeightPsf = v; });
    if (w.bearing) showSupportLoads(static_cast<int>(SupportKind::Wall), id);
}

void PropertiesPanel::showOpening(const Opening& o) {
    const ObjectId id = o.id;
    heading(o.kind == OpeningKind::Door ? "Door" : "Window");
    choice("Type", {"Door", "Window"}, o.kind == OpeningKind::Door ? 0 : 1,
           [id](Document& d, int v) { d.opening(id)->kind = v == 0 ? OpeningKind::Door : OpeningKind::Window; });
    length("Width", o.width, [id](Document& d, double v) { d.opening(id)->width = v; }, 1.0);
    length("Height", o.height, [id](Document& d, double v) { d.opening(id)->height = v; }, 1.0);
    length("Sill height", o.sillHeight, [id](Document& d, double v) { d.opening(id)->sillHeight = v; });
    length("Center from wall start", o.offset, [id](Document& d, double v) { d.opening(id)->offset = v; });
    if (const Wall* w = ws_->doc().wall(o.wallId); w && w->bearing)
        button("Size header…", [this, id] { emit sizeRequested(id); });
}

void PropertiesPanel::showBeam(const Beam& b) {
    const ObjectId id = b.id;
    heading(QString("Beam %1").arg(id));
    info("Span", feetInches(b.length()));
    const Member* m = findMember(b.memberId);
    info("Member", m ? QString::fromStdString(m->name) : QString("Not sized yet"));
    button("Size beam…", [this, id] { emit sizeRequested(id); });
    showSupportLoads(static_cast<int>(SupportKind::Beam), id);
}

void PropertiesPanel::showRoof(const Roof& r) {
    const ObjectId id = r.id;
    heading(QString("Roof %1").arg(id));
    choice("Style", {"Gable", "Hip"}, r.style == RoofStyle::Gable ? 0 : 1,
           [id](Document& d, int v) { d.roof(id)->style = v == 0 ? RoofStyle::Gable : RoofStyle::Hip; });
    choice("Ridge runs", {"East-west (X)", "North-south (Y)"}, r.ridgeAxis == Axis::X ? 0 : 1,
           [id](Document& d, int v) { d.roof(id)->ridgeAxis = v == 0 ? Axis::X : Axis::Y; });
    number("Pitch", r.pitch, 0.25, 24, 2, " in 12", [id](Document& d, double v) { d.roof(id)->pitch = v; });
    length("Overhang", r.overhang, [id](Document& d, double v) { d.roof(id)->overhang = v; });
    length("Width", r.footprint.width(), [id](Document& d, double v) {
        Roof* x = d.roof(id);
        x->footprint.max.x = x->footprint.min.x + v;
    }, 12.0);
    length("Depth", r.footprint.height(), [id](Document& d, double v) {
        Roof* x = d.roof(id);
        x->footprint.max.y = x->footprint.min.y + v;
    }, 12.0);
    number("Dead load", r.deadPsf, 0, 100, 1, " psf", [id](Document& d, double v) { d.roof(id)->deadPsf = v; });
    check("Structural ridge beam", r.structuralRidge, [id](Document& d, bool v) { d.roof(id)->structuralRidge = v; });
    check("Ceiling joists", r.hasCeiling, [id](Document& d, bool v) { d.roof(id)->hasCeiling = v; });
    if (r.hasCeiling) {
        number("Ceiling dead load", r.ceilingDeadPsf, 0, 50, 1, " psf", [id](Document& d, double v) { d.roof(id)->ceilingDeadPsf = v; });
        choice("Attic", {"No storage (10 psf)", "Limited storage (20 psf)"}, r.atticLivePsf > 15 ? 1 : 0,
               [id](Document& d, int v) { d.roof(id)->atticLivePsf = v == 0 ? 10.0 : 20.0; });
    }
    info("Snow on roof", QString("%1 psf").arg(roofSnowPsf(ws_->doc().site, r.slopeRadians()), 0, 'f', 1));
    if (r.structuralRidge && r.style == RoofStyle::Gable)
        button("Size ridge beam…", [this, id] { emit sizeRequested(id); });
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
    text("Name", QString::fromStdString(r.name), [id](Document& d, const std::string& v) { d.room(id)->name = v; });
    QStringList types;
    for (int t = 0; t <= static_cast<int>(RoomType::Other); ++t)
        types << QString("%1 (%2 psf live)").arg(toString(static_cast<RoomType>(t))).arg(defaultLiveLoadPsf(static_cast<RoomType>(t)));
    choice("Type", types, static_cast<int>(r.type), [id](Document& d, int v) { d.room(id)->type = static_cast<RoomType>(v); });
    info("Area", QString("%1 sq ft").arg(area(r.outline) / kSqInPerSqFt, 0, 'f', 0));
}
