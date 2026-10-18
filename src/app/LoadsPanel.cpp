#include "LoadsPanel.h"

#include "Workspace.h"

#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

using namespace oa;
using namespace oa::structural;

LoadsPanel::LoadsPanel(Workspace* ws, QWidget* parent) : QWidget(parent), ws_(ws) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(splitter);

    table_ = new QTableWidget(0, 9);
    table_->setHorizontalHeaderLabels(
        {"Carried by", "D plf", "L plf", "Lr plf", "S plf", "Governing plf", "Combination", "Point loads", ""});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setToolTip("Peak loads on top of each member. Double-click to select it; beams open the sizer.");
    splitter->addWidget(table_);

    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel("Load path warnings"));
    warnings_ = new QListWidget;
    warnings_->setWordWrap(true);
    rightLayout->addWidget(warnings_);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const auto* item = table_->item(row, 0);
        if (!item) return;
        const ObjectId id = item->data(Qt::UserRole).toULongLong();
        const auto kind = static_cast<SupportKind>(item->data(Qt::UserRole + 1).toInt());
        const ObjectId level = item->data(Qt::UserRole + 2).toULongLong();
        ws_->setCurrentLevel(level);
        ws_->select(id);
        if (kind != SupportKind::Wall) emit sizeRequested(id);
    });
    connect(ws_, &Workspace::documentChanged, this, &LoadsPanel::scheduleRefresh);
    refresh();
}

void LoadsPanel::scheduleRefresh() {
    if (pending_) return;
    pending_ = true;
    QTimer::singleShot(0, this, [this] {
        pending_ = false;
        refresh();
    });
}

void LoadsPanel::refresh() {
    const auto& result = ws_->loads();
    const Document& doc = ws_->doc();

    std::vector<const SupportLoads*> rows;
    for (const auto& s : result.supports)
        if (!s.lineLoads.empty() || !s.pointLoads.empty()) rows.push_back(&s);

    table_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const SupportLoads& s = *rows[r];
        const auto sum = s.topLoads();
        auto* name = new QTableWidgetItem(QString::fromStdString(s.label));
        name->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(s.objectId));
        name->setData(Qt::UserRole + 1, static_cast<int>(s.kind));
        name->setData(Qt::UserRole + 2, QVariant::fromValue<qulonglong>(s.levelId));
        table_->setItem(r, 0, name);
        int col = 1;
        for (LoadCase c : kAllCases) {
            auto* item = new QTableWidgetItem(sum.peakPlf[c] > 0.5 ? QString::number(sum.peakPlf[c], 'f', 0) : QString());
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(r, col++, item);
        }
        auto* gov = new QTableWidgetItem(QString::number(sum.governingPlf, 'f', 0));
        gov->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QFont bold = gov->font();
        bold.setBold(true);
        gov->setFont(bold);
        table_->setItem(r, 5, gov);
        table_->setItem(r, 6, new QTableWidgetItem(QString::fromStdString(sum.governingCombo)));
        table_->setItem(r, 7, new QTableWidgetItem(s.pointLoads.empty() ? QString() : QString::number(s.pointLoads.size())));
        QString note;
        if (s.kind == SupportKind::Beam) {
            const Beam* b = doc.beam(s.objectId);
            note = b && !b->memberId.empty() ? QString() : QString("needs sizing");
        }
        table_->setItem(r, 8, new QTableWidgetItem(note));
    }

    warnings_->clear();
    if (result.warnings.empty()) {
        warnings_->addItem("Every load has a path to the foundation.");
    } else {
        for (const auto& w : result.warnings) {
            auto* item = new QListWidgetItem(QString::fromStdString(w));
            item->setForeground(QColor(170, 60, 20));
            warnings_->addItem(item);
        }
    }
}
