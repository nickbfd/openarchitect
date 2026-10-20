#pragma once

#include "oa/core/document.h"

#include <QWidget>

#include <functional>

class QFormLayout;
class Workspace;

// Editable properties of the selected object, or of the plan, site and
// current level when nothing is selected. Each change is one undo step.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(Workspace* ws, QWidget* parent = nullptr);

signals:
    // Asks for the beam sizer for a beam, an opening's header or a roof's ridge beam.
    void sizeRequested(oa::ObjectId id);

private:
    void scheduleRebuild();
    void rebuild();

    void heading(const QString& text);
    void info(const QString& label, const QString& value);
    void length(const QString& label, double value, std::function<void(oa::Document&, double)> set,
                double minimum = 0.0);
    void number(const QString& label, double value, double min, double max, int decimals, const QString& suffix,
                std::function<void(oa::Document&, double)> set);
    void check(const QString& label, bool value, std::function<void(oa::Document&, bool)> set);
    void choice(const QString& label, const QStringList& items, int index, std::function<void(oa::Document&, int)> set);
    void text(const QString& label, const QString& value, std::function<void(oa::Document&, const std::string&)> set);
    void button(const QString& text, std::function<void()> onClick);
    void note(const QString& html);

    // Rows that edit several selected objects at once. A value that differs
    // between them shows as "varies" until it is set.
    using Ids = std::vector<oa::ObjectId>;
    void lengthAll(const QString& label, const Ids& ids, std::function<double(const oa::Document&, oa::ObjectId)> get,
                   std::function<void(oa::Document&, oa::ObjectId, double)> set, double minimum = 0.0);
    void choiceAll(const QString& label, const QStringList& items, const Ids& ids,
                   std::function<int(const oa::Document&, oa::ObjectId)> get,
                   std::function<void(oa::Document&, oa::ObjectId, int)> set);
    void showMany(const Ids& ids);

    void showPlan();
    void showWall(const oa::Wall& w);
    void showOpening(const oa::Opening& o);
    void showBeam(const oa::Beam& b);
    void showRoof(const oa::Roof& r);
    void showRoofPlane(const oa::RoofPlane& p);
    void showRoofSettings(oa::ObjectId id, const oa::RoofSettings& r, bool plane = false);
    void showPost(const oa::Post& p);
    void showWallRoof(const oa::Wall& w);
    void showWallType(const oa::Wall& w, const oa::WallType& t);
    void showPlatform(const oa::FloorPlatform& f);
    void showRoom(const oa::Room& r);
    void showRoomFloor(const oa::Room& r);
    void showDimension(const oa::Dimension& dim);
    void showElevationPoint(const oa::ElevationPoint& e);
    void showTerrain();
    void showLoadBreakdown(oa::ObjectId id);

    void apply(const QString& what, std::function<void(oa::Document&)> change);

    Workspace* ws_;
    QWidget* body_ = nullptr;
    QFormLayout* form_ = nullptr;
    bool rebuildPending_ = false;
};
