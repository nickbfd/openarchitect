#pragma once

#include "oa/core/document.h"

#include <QWidget>

class QListWidget;
class QTableWidget;
class Workspace;

// Every wall and beam that carries load, with peak plf per case and the
// governing combination, plus the takedown's warnings about missing supports.
class LoadsPanel : public QWidget {
    Q_OBJECT
public:
    explicit LoadsPanel(Workspace* ws, QWidget* parent = nullptr);

signals:
    void sizeRequested(oa::ObjectId id);

private:
    void scheduleRefresh();
    void refresh();

    Workspace* ws_;
    QTableWidget* table_;
    QListWidget* warnings_;
    bool pending_ = false;
};
