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

    void showPlan();
    void showWall(const oa::Wall& w);
    void showOpening(const oa::Opening& o);
    void showBeam(const oa::Beam& b);
    void showRoof(const oa::Roof& r);
    void showRoofPlane(const oa::RoofPlane& p);
    void showRoofSettings(oa::ObjectId id, const oa::RoofSettings& r);
    void showPlatform(const oa::FloorPlatform& f);
    void showRoom(const oa::Room& r);
    void showLoadBreakdown(oa::ObjectId id);

    void apply(const QString& what, std::function<void(oa::Document&)> change);

    Workspace* ws_;
    QWidget* body_ = nullptr;
    QFormLayout* form_ = nullptr;
    bool rebuildPending_ = false;
};
