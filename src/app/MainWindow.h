#pragma once

#include "PlanView.h"

#include "oa/core/document.h"

#include <QMainWindow>

class QAction;
class QComboBox;
class QTabWidget;
class ModelView3D;
class Workspace;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void createActions();
    void refreshLevels();
    void refreshWallTypes();
    void updateTitle();
    void updateUndoActions();
    bool maybeSave();
    bool save();
    bool saveAs();
    void openSizer(oa::ObjectId id);
    void addLevel();
    void autoRoof();

    Workspace* ws_;
    PlanView* plan_;
    ModelView3D* model_;
    QTabWidget* tabs_;
    QComboBox* levelBox_;
    QComboBox* wallTypeBox_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
};
