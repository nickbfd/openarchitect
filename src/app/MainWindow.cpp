#include "MainWindow.h"

#include "BeamSizerDialog.h"
#include "LoadsPanel.h"
#include "ModelView3D.h"
#include "PropertiesPanel.h"
#include "SampleHouse.h"
#include "Workspace.h"

#include "oa/core/dimensions.h"
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
#include <QToolButton>

#include <algorithm>

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
    refreshWallTypes();
    updateTitle();
    updateUndoActions();

    connect(ws_, &Workspace::documentChanged, this, [this] {
        refreshLevels();
        refreshWallTypes();
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
    // One checkable action per tool, shared by the Tools menu and the
    // toolbar's category menus.
    auto toolAction = [&](PlanView::Tool tool, const QString& name, const char* key) {
        QAction* a = new QAction(name, this);
        a->setCheckable(true);
        if (key) a->setShortcut(QKeySequence(key));
        a->setChecked(tool == PlanView::Tool::Select);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, tool] { useTool(tool); });
        connect(plan_, &PlanView::toolChanged, a, [a, tool](PlanView::Tool t) {
            if (t == tool) a->setChecked(true);
        });
        return a;
    };
    QAction* select = toolAction(PlanView::Tool::Select, "Select", "S");
    QAction* wall = toolAction(PlanView::Tool::Wall, "Wall", "W");
    QAction* divider = toolAction(PlanView::Tool::RoomDivider, "Room divider", "M");
    QAction* door = toolAction(PlanView::Tool::Door, "Door", "D");
    QAction* window = toolAction(PlanView::Tool::Window, "Window", "N");
    QAction* roof = toolAction(PlanView::Tool::Roof, "Roof", "R");
    QAction* plane = toolAction(PlanView::Tool::RoofPlane, "Roof plane", "P");
    QAction* join = toolAction(PlanView::Tool::JoinPlanes, "Join roof planes", "K");
    QAction* beam = toolAction(PlanView::Tool::Beam, "Beam", "B");
    QAction* post = toolAction(PlanView::Tool::Post, "Post", "O");
    QAction* floor = toolAction(PlanView::Tool::Floor, "Floor framing", "J");
    QAction* dimension = toolAction(PlanView::Tool::Dimension, "Dimension", "X");
    QAction* elevation = toolAction(PlanView::Tool::Elevation, "Elevation point", "E");
    QAction* autoRoofAction = new QAction("Roof over this level's walls", this);
    connect(autoRoofAction, &QAction::triggered, this, &MainWindow::autoRoof);
    QAction* autoDims = new QAction("Dimension exterior walls", this);
    connect(autoDims, &QAction::triggered, this, &MainWindow::autoDimensions);
    QAction* interior = new QAction("Interior wall", this);
    interior->setShortcut(QKeySequence("I"));
    connect(interior, &QAction::triggered, this, [this] {
        if (const WallType* t = ws_->doc().firstWallType(WallKind::Interior)) plan_->setWallType(t->id);
        useTool(PlanView::Tool::Wall);
    });
    addAction(interior);

    tools->addAction(select);
    tools->addSection("Walls");
    tools->addActions({wall, interior, divider});
    tools->addSection("Doors and windows");
    tools->addActions({door, window});
    tools->addSection("Roofs");
    tools->addActions({roof, plane, join, autoRoofAction});
    tools->addSection("Framing");
    tools->addActions({beam, post, floor});
    tools->addSection("Dimensions and site");
    tools->addActions({dimension, autoDims, elevation});
    tools->addSeparator();
    tools->addAction("Add level above", this, &MainWindow::addLevel);

    // The toolbar groups the tools by category, each a button with a menu.
    bar->addAction(select);
    auto category = [&](const QString& name, std::initializer_list<PlanView::Tool> members) {
        auto* button = new QToolButton;
        button->setText(name + " ");
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setCheckable(true);
        auto* menu = new QMenu(button);
        button->setMenu(menu);
        bar->addWidget(button);
        std::vector<PlanView::Tool> list(members);
        connect(plan_, &PlanView::toolChanged, button, [button, list](PlanView::Tool t) {
            button->setChecked(std::find(list.begin(), list.end(), t) != list.end());
        });
        return menu;
    };
    wallMenu_ = category("Walls", {PlanView::Tool::Wall, PlanView::Tool::RoomDivider});
    wallMenu_->addAction(divider); // the wall type entries go above it, see refreshWallTypes
    QMenu* doors = category("Doors", {PlanView::Tool::Door});
    QMenu* windows = category("Windows", {PlanView::Tool::Window});
    struct Size {
        const char* name;
        double width, height, sill;
    };
    const Size doorSizes[] = {{"Door 2'-6\" x 6'-8\"  (2668)", 30, 80, 0},
                              {"Door 2'-8\" x 6'-8\"  (2868)", 32, 80, 0},
                              {"Door 3'-0\" x 6'-8\"  (3068)", 36, 80, 0},
                              {"Double door 6'-0\" x 6'-8\"  (6068)", 72, 80, 0},
                              {"Garage door 16'-0\" x 7'-0\"  (16070)", 192, 84, 0}};
    const Size windowSizes[] = {{"Window 2'-0\" x 3'-0\"  (2030)", 24, 36, 44},
                                {"Window 3'-0\" x 4'-0\"  (3040)", 36, 48, 32},
                                {"Window 4'-0\" x 4'-0\"  (4040)", 48, 48, 32},
                                {"Window 6'-0\" x 4'-0\"  (6040)", 72, 48, 32},
                                {"Picture window 6'-0\" x 5'-0\"  (6050)", 72, 60, 20}};
    for (const Size& z : doorSizes)
        doors->addAction(z.name, this, [this, z] {
            plan_->setOpeningSize(OpeningKind::Door, z.width, z.height, z.sill);
            useTool(PlanView::Tool::Door);
        });
    for (const Size& z : windowSizes)
        windows->addAction(z.name, this, [this, z] {
            plan_->setOpeningSize(OpeningKind::Window, z.width, z.height, z.sill);
            useTool(PlanView::Tool::Window);
        });
    QMenu* roofs = category("Roofs", {PlanView::Tool::Roof, PlanView::Tool::RoofPlane, PlanView::Tool::JoinPlanes});
    roofs->addActions({roof, plane, join});
    roofs->addSeparator();
    roofs->addAction(autoRoofAction);
    QMenu* framing = category("Framing", {PlanView::Tool::Beam, PlanView::Tool::Post, PlanView::Tool::Floor});
    framing->addActions({beam, post, floor});
    QMenu* dims = category("Dimensions", {PlanView::Tool::Dimension});
    dims->addAction(dimension);
    dims->addAction(autoDims);
    QMenu* site = category("Terrain", {PlanView::Tool::Elevation});
    site->addAction(elevation);
    site->addAction("Clear the terrain", this, [this] {
        ws_->edit("Clear terrain", [](Document& d) { d.terrain.points.clear(); });
    });

    // View
    bar->addSeparator();
    QMenu* view = menuBar()->addMenu("&View");
    QAction* showLoads = view->addAction("Show loads on plan");
    showLoads->setCheckable(true);
    showLoads->setShortcut(QKeySequence("L"));
    connect(showLoads, &QAction::toggled, plan_, &PlanView::setShowLoads);
    bar->addAction(showLoads);
    QAction* showFraming = view->addAction("Show wall framing in 3D");
    showFraming->setCheckable(true);
    showFraming->setShortcut(QKeySequence("G"));
    connect(showFraming, &QAction::toggled, this, [this](bool on) {
        model_->setShowFraming(on);
        if (on) tabs_->setCurrentWidget(model_);
    });
    bar->addAction(showFraming);
    QAction* showSizes = view->addAction("Show door and window sizes");
    showSizes->setCheckable(true);
    showSizes->setChecked(true);
    connect(showSizes, &QAction::toggled, plan_, &PlanView::setShowOpeningSizes);
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

void MainWindow::useTool(PlanView::Tool tool) {
    tabs_->setCurrentWidget(plan_);
    plan_->setTool(tool);
    plan_->setFocus();
}

// The Walls menu lists every wall type, exterior then interior, above the room divider.
void MainWindow::refreshWallTypes() {
    const Document& d = ws_->doc();
    if (!d.wallType(plan_->wallType())) {
        const WallType* t = d.firstWallType(WallKind::Exterior);
        plan_->setWallType(t ? t->id : kNoId);
    }
    QList<QAction*> actions = wallMenu_->actions();
    QAction* divider = actions.isEmpty() ? nullptr : actions.back();
    for (QAction* a : actions)
        if (a != divider) {
            wallMenu_->removeAction(a);
            a->deleteLater();
        }
    for (WallKind kind : {WallKind::Exterior, WallKind::Interior}) {
        QAction* section = new QAction(kind == WallKind::Exterior ? "Exterior walls" : "Interior walls", wallMenu_);
        section->setSeparator(true);
        wallMenu_->insertAction(divider, section);
        for (const auto& t : d.wallTypes) {
            if (t.kind != kind) continue;
            QAction* a = new QAction(QString::fromStdString(t.name), wallMenu_);
            a->setCheckable(true);
            a->setChecked(t.id == plan_->wallType());
            const ObjectId id = t.id;
            connect(a, &QAction::triggered, this, [this, id] {
                plan_->setWallType(id);
                useTool(PlanView::Tool::Wall);
                refreshWallTypes();
            });
            wallMenu_->insertAction(divider, a);
        }
    }
    QAction* sep = new QAction(wallMenu_);
    sep->setSeparator(true);
    wallMenu_->insertAction(divider, sep);
}

void MainWindow::autoDimensions() {
    const ObjectId level = ws_->currentLevel();
    const auto dims = exteriorDimensions(ws_->doc(), level);
    if (dims.empty()) {
        statusBar()->showMessage("Draw exterior walls on this level first.");
        return;
    }
    ws_->edit("Dimension exterior walls", [&](Document& d) {
        std::erase_if(d.dimensions, [level](const Dimension& x) { return x.automatic && x.levelId == level; });
        for (Dimension x : dims) {
            x.id = d.newId();
            d.dimensions.push_back(x);
        }
    });
    statusBar()->showMessage(QString("Added %1 dimensions. Run it again after changing walls to redo them.").arg(dims.size()));
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
        assignable = true;
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
    if (const Opening* o = doc.opening(id))
        if (const Wall* w = doc.wall(o->wallId))
            if (const WallType* t = doc.wallType(w->wallTypeId); t && !t->isDivider())
                dialog.limitWidth(t->studDepth, "the wall's studs");
    if (dialog.exec() == QDialog::Accepted && assignable && !dialog.chosenMemberId().empty()) {
        const std::string member = dialog.chosenMemberId();
        if (ws_->doc().opening(id))
            ws_->edit("Pick header", [id, member](Document& d) { d.opening(id)->headerMemberId = member; });
        else
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
