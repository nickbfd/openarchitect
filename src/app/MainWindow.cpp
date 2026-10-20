#include "MainWindow.h"

#include "BeamSizerDialog.h"
#include "LoadsPanel.h"
#include "ModelView3D.h"
#include "PropertiesPanel.h"
#include "SampleHouse.h"
#include "Workspace.h"

#include "oa/core/serialization.h"
#include "oa/structural/takedown.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

using namespace oa;
using namespace oa::structural;

namespace {
const char* kFileFilter = "OpenArchitect plan (*.oaplan);;All files (*)";
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ws_(new Workspace(this)) {
    resize(1400, 900);

    plan_ = new PlanView(ws_);
    model_ = new ModelView3D(ws_);
    tabs_ = new QTabWidget;
    tabs_->addTab(plan_, "Plan");
    tabs_->addTab(model_, "3D");
    setCentralWidget(tabs_);

    auto* props = new PropertiesPanel(ws_);
    auto* propsDock = new QDockWidget("Properties", this);
    propsDock->setObjectName("properties");
    propsDock->setWidget(props);
    propsDock->setMinimumWidth(340);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);

    auto* loads = new LoadsPanel(ws_);
    auto* loadsDock = new QDockWidget("Structural loads", this);
    loadsDock->setObjectName("loads");
    loadsDock->setWidget(loads);
    addDockWidget(Qt::BottomDockWidgetArea, loadsDock);

    connect(props, &PropertiesPanel::sizeRequested, this, &MainWindow::openSizer);
    connect(loads, &LoadsPanel::sizeRequested, this, &MainWindow::openSizer);
    connect(plan_, &PlanView::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m); });

    createActions();
    refreshLevels();
    updateTitle();
    updateUndoActions();

    connect(ws_, &Workspace::documentChanged, this, [this] {
        refreshLevels();
        updateUndoActions();
    });
    connect(ws_, &Workspace::currentLevelChanged, this, &MainWindow::refreshLevels);
    connect(ws_, &Workspace::modifiedChanged, this, &MainWindow::updateTitle);
    statusBar()->showMessage("Pick a tool to start drawing, or open the sample house from the Help menu.");
}

void MainWindow::createActions() {
    // File
    QMenu* file = menuBar()->addMenu("&File");
    file->addAction("&New plan", QKeySequence::New, this, [this] {
        if (maybeSave()) ws_->newDocument();
        updateTitle();
    });
    file->addAction("&Open…", QKeySequence::Open, this, [this] {
        if (!maybeSave()) return;
        const QString path = QFileDialog::getOpenFileName(this, "Open plan", QString(), kFileFilter);
        if (!path.isEmpty()) openFile(path);
    });
    file->addAction("&Save", QKeySequence::Save, this, [this] { save(); });
    file->addAction("Save &as…", QKeySequence::SaveAs, this, [this] { saveAs(); });
    file->addSeparator();
    file->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

    // Edit
    QMenu* edit = menuBar()->addMenu("&Edit");
    undoAction_ = edit->addAction("Undo", QKeySequence::Undo, ws_, &Workspace::undo);
    redoAction_ = edit->addAction("Redo", QKeySequence::Redo, ws_, &Workspace::redo);
    edit->addSeparator();
    edit->addAction("Delete", QKeySequence::Delete, plan_, &PlanView::deleteSelection);

    // Tools
    QToolBar* bar = addToolBar("Tools");
    bar->setObjectName("tools");
    bar->addWidget(new QLabel(" Level "));
    levelBox_ = new QComboBox;
    levelBox_->setMinimumWidth(120);
    connect(levelBox_, &QComboBox::activated, this, [this](int i) {
        ws_->setCurrentLevel(levelBox_->itemData(i).toULongLong());
    });
    bar->addWidget(levelBox_);
    bar->addSeparator();

    QMenu* tools = menuBar()->addMenu("&Tools");
    auto* group = new QActionGroup(this);
    struct ToolDef {
        PlanView::Tool tool;
        const char* name;
        const char* key;
    };
    const ToolDef defs[] = {
        {PlanView::Tool::Select, "Select", "S"}, {PlanView::Tool::Wall, "Wall", "W"},
        {PlanView::Tool::Door, "Door", "D"},     {PlanView::Tool::Window, "Window", "N"},
        {PlanView::Tool::Beam, "Beam", "B"},     {PlanView::Tool::Floor, "Floor framing", "J"},
        {PlanView::Tool::Roof, "Roof", "R"},     {PlanView::Tool::RoofPlane, "Roof plane", "P"},
        {PlanView::Tool::Room, "Room", "M"},
    };
    for (const auto& def : defs) {
        QAction* a = tools->addAction(def.name);
        a->setCheckable(true);
        a->setShortcut(QKeySequence(def.key));
        a->setChecked(def.tool == PlanView::Tool::Select);
        group->addAction(a);
        bar->addAction(a);
        const PlanView::Tool tool = def.tool;
        connect(a, &QAction::triggered, this, [this, tool] {
            tabs_->setCurrentWidget(plan_);
            plan_->setTool(tool);
            plan_->setFocus();
        });
        connect(plan_, &PlanView::toolChanged, a, [a, tool](PlanView::Tool t) {
            if (t == tool) a->setChecked(true);
        });
    }
    tools->addSeparator();
    tools->addAction("Roof over this level's walls", this, &MainWindow::autoRoof);
    tools->addAction("Add level above", this, &MainWindow::addLevel);

    // View
    bar->addSeparator();
    QMenu* view = menuBar()->addMenu("&View");
    QAction* showLoads = view->addAction("Show loads on plan");
    showLoads->setCheckable(true);
    showLoads->setShortcut(QKeySequence("L"));
    connect(showLoads, &QAction::toggled, plan_, &PlanView::setShowLoads);
    bar->addAction(showLoads);
    view->addAction("Fit to window", QKeySequence("F"), this, [this] {
        if (tabs_->currentWidget() == plan_) plan_->zoomToFit();
        else model_->frameModel();
    });
    view->addAction("Plan", QKeySequence("F2"), this, [this] { tabs_->setCurrentWidget(plan_); });
    view->addAction("3D", QKeySequence("F3"), this, [this] { tabs_->setCurrentWidget(model_); });

    // Help
    QMenu* help = menuBar()->addMenu("&Help");
    help->addAction("Open sample house", this, [this] {
        if (!maybeSave()) return;
        ws_->newDocument(makeSampleHouse());
        plan_->zoomToFit();
        model_->frameModel();
        updateTitle();
    });
    help->addAction("About OpenArchitect", this, [this] {
        QMessageBox::about(this, "About OpenArchitect",
                           QString("<b>OpenArchitect %1</b><p>Open source residential design with 2D and 3D editing "
                                   "and vertical structural load calculations.</p>"
                                   "<p>Licensed under the GNU GPL v3 or later.</p>")
                               .arg(OA_VERSION));
    });
}

void MainWindow::refreshLevels() {
    levelBox_->clear();
    for (const Level* l : ws_->doc().levelsByElevation())
        levelBox_->insertItem(0, QString::fromStdString(l->name), QVariant::fromValue<qulonglong>(l->id));
    const int index = levelBox_->findData(QVariant::fromValue<qulonglong>(ws_->currentLevel()));
    levelBox_->setCurrentIndex(index);
}

void MainWindow::updateTitle() {
    const QString name = ws_->filePath().isEmpty() ? QString("Untitled") : QFileInfo(ws_->filePath()).fileName();
    setWindowTitle(QString("%1%2 - OpenArchitect").arg(name, ws_->isModified() ? "*" : ""));
}

void MainWindow::updateUndoActions() {
    undoAction_->setEnabled(ws_->canUndo());
    redoAction_->setEnabled(ws_->canRedo());
    undoAction_->setText(ws_->canUndo() ? "Undo " + ws_->undoText() : QString("Undo"));
    redoAction_->setText(ws_->canRedo() ? "Redo " + ws_->redoText() : QString("Redo"));
}

bool MainWindow::openFile(const QString& path) {
    QString error;
    if (!ws_->open(path, &error)) {
        QMessageBox::warning(this, "Could not open plan", error);
        return false;
    }
    plan_->zoomToFit();
    model_->frameModel();
    updateTitle();
    return true;
}

bool MainWindow::save() {
    if (ws_->filePath().isEmpty()) return saveAs();
    QString error;
    if (!ws_->save(ws_->filePath(), &error)) {
        QMessageBox::warning(this, "Could not save plan", error);
        return false;
    }
    updateTitle();
    return true;
}

bool MainWindow::saveAs() {
    QString path = QFileDialog::getSaveFileName(this, "Save plan", ws_->filePath(), kFileFilter);
    if (path.isEmpty()) return false;
    if (!path.endsWith(kPlanExtension, Qt::CaseInsensitive)) path += kPlanExtension;
    QString error;
    if (!ws_->save(path, &error)) {
        QMessageBox::warning(this, "Could not save plan", error);
        return false;
    }
    updateTitle();
    return true;
}

bool MainWindow::maybeSave() {
    if (!ws_->isModified()) return true;
    const auto answer = QMessageBox::question(this, "Unsaved changes", "Save changes to this plan first?",
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (maybeSave()) e->accept();
    else e->ignore();
}

void MainWindow::openSizer(ObjectId id) {
    const Document& doc = ws_->doc();
    const TakedownResult& result = ws_->loads();
    BeamLoads loads;
    QString title;
    bool assignable = false;

    if (doc.beam(id)) {
        const SupportLoads* s = result.beam(id);
        if (!s) return;
        loads = beamLoads(*s);
        title = QString::fromStdString(s->label);
        assignable = true;
    } else if (const Opening* o = doc.opening(id)) {
        loads = headerLoads(doc, result, id);
        title = QString("Header over %1 %2").arg(o->kind == OpeningKind::Door ? "door" : "window").arg(id);
    } else if (doc.roof(id)) {
        const SupportLoads* s = result.ridgeBeam(id);
        if (!s) {
            QMessageBox::information(this, "No ridge beam", "This roof uses a ridge board, so there is no ridge beam to size.");
            return;
        }
        loads = beamLoads(*s);
        title = QString::fromStdString(s->label);
    } else {
        return;
    }
    if (loads.span <= 0.0) return;

    BeamSizerDialog dialog(title, loads, criteriaFor(loads), assignable, this);
    if (dialog.exec() == QDialog::Accepted && assignable && !dialog.chosenMemberId().empty()) {
        const std::string member = dialog.chosenMemberId();
        ws_->edit("Size beam", [id, member](Document& d) { d.beam(id)->memberId = member; });
    }
}

void MainWindow::addLevel() {
    ws_->edit("Add level", [](Document& d) {
        auto levels = d.levelsByElevation();
        Level l;
        l.id = d.newId();
        if (levels.empty()) {
            l.name = "Floor 1";
        } else {
            const Level* top = levels.back();
            l.elevation = top->elevation + top->wallHeight + top->floorThickness;
            l.wallHeight = top->wallHeight;
            l.floorThickness = top->floorThickness;
            l.name = "Floor " + std::to_string(levels.size() + 1);
        }
        d.levels.push_back(l);
    });
}

void MainWindow::autoRoof() {
    const ObjectId level = ws_->currentLevel();
    Rect box{{1e30, 1e30}, {-1e30, -1e30}};
    bool any = false;
    for (const auto& w : ws_->doc().walls) {
        if (w.levelId != level || !w.bearing) continue;
        for (Vec2 p : {w.a, w.b}) {
            box.min = {std::min(box.min.x, p.x), std::min(box.min.y, p.y)};
            box.max = {std::max(box.max.x, p.x), std::max(box.max.y, p.y)};
        }
        any = true;
    }
    if (!any || box.width() < 12 || box.height() < 12) {
        statusBar()->showMessage("Draw bearing walls on this level first.");
        return;
    }
    ObjectId created = kNoId;
    ws_->edit("Add roof", [&](Document& d) {
        Roof r;
        r.id = d.newId();
        r.levelId = level;
        r.footprint = box;
        r.ridgeAxis = box.width() >= box.height() ? Axis::X : Axis::Y;
        d.roofs.push_back(r);
        created = r.id;
    });
    ws_->select(created);
}
