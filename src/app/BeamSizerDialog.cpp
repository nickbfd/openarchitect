#include "BeamSizerDialog.h"

#include "oa/core/units.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

using namespace oa;
using namespace oa::structural;

namespace {

QString num(double v, int decimals = 0) { return QString::number(v, 'f', decimals); }
QString pct(double ratio) { return QString("%1%").arg(ratio * 100.0, 0, 'f', 0); }

QString casesText(const CaseLoads& c, const char* unit) {
    QStringList parts;
    for (LoadCase lc : kAllCases)
        if (std::abs(c[lc]) > 0.5) parts << QString("%1 %2").arg(shortName(lc)).arg(num(c[lc]));
    return parts.isEmpty() ? QString("none") : parts.join(", ") + " " + unit;
}

} // namespace

BeamSizerDialog::BeamSizerDialog(const QString& title, BeamLoads loads, DesignCriteria criteria, bool canAssign,
                                 QWidget* parent)
    : QDialog(parent), loads_(std::move(loads)), criteria_(criteria), title_(title) {
    setWindowTitle(title);
    resize(900, 640);
    auto* layout = new QVBoxLayout(this);

    const Reactions r = reactionsByCase(loads_);
    auto* summary = new QLabel(this);
    summary->setTextFormat(Qt::RichText);
    summary->setWordWrap(true);
    summary->setText(QString("<b>Span</b> %1 &nbsp; <b>Total load</b> %2<br>"
                             "<b>Reactions</b> left %3; right %4<br>"
                             "<b>Deflection limits</b> live L/%5, total L/%6. Beam self weight %7.")
                         .arg(QString::fromStdString(formatFeetInches(loads_.span, 8)),
                              casesText(loads_.totalLoad(), "lb"), casesText(r.left, "lb"), casesText(r.right, "lb"),
                              num(criteria_.liveDeflectionLimit), num(criteria_.totalDeflectionLimit),
                              criteria_.includeSelfWeight ? "included" : "not included"));
    layout->addWidget(summary);

    auto* loadsTable = new QTableWidget(static_cast<int>(loads_.line.size() + loads_.point.size()), 4, this);
    loadsTable->setHorizontalHeaderLabels({"Load from", "Where", "Start", "End"});
    loadsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    loadsTable->verticalHeader()->hide();
    loadsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    loadsTable->setMaximumHeight(160);
    int row = 0;
    for (const auto& l : loads_.line) {
        loadsTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(l.source)));
        loadsTable->setItem(row, 1, new QTableWidgetItem(QString("%1 to %2").arg(
            QString::fromStdString(formatFeetInches(l.start, 8)), QString::fromStdString(formatFeetInches(l.end, 8)))));
        loadsTable->setItem(row, 2, new QTableWidgetItem(casesText(l.wStart, "plf")));
        loadsTable->setItem(row, 3, new QTableWidgetItem(casesText(l.wEnd, "plf")));
        ++row;
    }
    for (const auto& p : loads_.point) {
        loadsTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(p.source)));
        loadsTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(formatFeetInches(p.position, 8))));
        loadsTable->setItem(row, 2, new QTableWidgetItem(casesText(p.p, "lb")));
        loadsTable->setItem(row, 3, new QTableWidgetItem(QString()));
        ++row;
    }
    layout->addWidget(loadsTable);

    showFailing_ = new QCheckBox("Show members that fail", this);
    layout->addWidget(showFailing_);

    results_ = new QTableWidget(0, 9, this);
    results_->setHorizontalHeaderLabels(
        {"Member", "Used", "Governs", "Bending", "Shear", "Live defl.", "Total defl.", "Bearing", "Studs each end"});
    results_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    results_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    results_->verticalHeader()->hide();
    results_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(results_, 1);

    auto* note = new QLabel(
        "Preliminary design per NDS (ASD): dry service, normal temperature, compression edge braced (CL = 1), "
        "simple span, short-term deflection. LVL values are generic; confirm with the manufacturer. "
        "Have a licensed engineer review where your jurisdiction requires it.",
        this);
    note->setWordWrap(true);
    note->setStyleSheet("color: #666;");
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* copy = buttons->addButton("Copy report", QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(report()); });
    if (canAssign) {
        auto* use = buttons->addButton("Use selected member", QDialogButtonBox::AcceptRole);
        use->setEnabled(false);
        connect(results_, &QTableWidget::itemSelectionChanged, this,
                [this, use] { use->setEnabled(!results_->selectedItems().isEmpty()); });
        connect(use, &QPushButton::clicked, this, [this] {
            const int r = results_->currentRow();
            if (r >= 0 && r < static_cast<int>(shown_.size())) chosen_ = shown_[r]->member->id;
            accept();
        });
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    for (const auto& m : defaultCatalog()) checks_.push_back(checkMember(m, loads_, criteria_));
    std::stable_sort(checks_.begin(), checks_.end(), [](const MemberCheck& a, const MemberCheck& b) {
        if (a.passes != b.passes) return a.passes;
        if (std::abs(a.member->d - b.member->d) > 1e-6) return a.member->d < b.member->d;
        if (std::abs(a.member->b - b.member->b) > 1e-6) return a.member->b < b.member->b;
        return a.maxRatio < b.maxRatio;
    });
    connect(showFailing_, &QCheckBox::toggled, this, &BeamSizerDialog::fillResults);
    fillResults();
}

void BeamSizerDialog::fillResults() {
    shown_.clear();
    for (const auto& c : checks_)
        if (c.passes || showFailing_->isChecked()) shown_.push_back(&c);

    results_->setRowCount(static_cast<int>(shown_.size()));
    for (int row = 0; row < static_cast<int>(shown_.size()); ++row) {
        const MemberCheck& c = *shown_[row];
        const QString cells[] = {
            QString::fromStdString(c.member->name),
            pct(c.maxRatio),
            QString::fromStdString(c.governingCheck),
            pct(c.bendingRatio),
            pct(c.shearRatio),
            QString("%1\"").arg(num(c.liveDeflection, 2)),
            QString("%1\"").arg(num(c.totalDeflection, 2)),
            QString("%1\"").arg(num(c.requiredBearing, 2)),
            QString::number(c.jackStuds),
        };
        for (int col = 0; col < 9; ++col) {
            auto* item = new QTableWidgetItem(cells[col]);
            if (!c.passes) item->setForeground(QColor(180, 40, 40));
            results_->setItem(row, col, item);
        }
    }
    if (!shown_.empty()) results_->selectRow(0);
}

QString BeamSizerDialog::report() const {
    QStringList lines;
    lines << title_;
    lines << QString("Span: %1").arg(QString::fromStdString(formatFeetInches(loads_.span, 8)));
    lines << "Loads (unfactored):";
    for (const auto& l : loads_.line)
        lines << QString("  %1, %2 to %3: %4 to %5")
                     .arg(QString::fromStdString(l.source), QString::fromStdString(formatFeetInches(l.start, 8)),
                          QString::fromStdString(formatFeetInches(l.end, 8)), casesText(l.wStart, "plf"),
                          casesText(l.wEnd, "plf"));
    for (const auto& p : loads_.point)
        lines << QString("  %1 at %2: %3")
                     .arg(QString::fromStdString(p.source), QString::fromStdString(formatFeetInches(p.position, 8)),
                          casesText(p.p, "lb"));
    lines << QString("Deflection limits: live L/%1, total L/%2")
                 .arg(num(criteria_.liveDeflectionLimit), num(criteria_.totalDeflectionLimit));
    lines << "";
    lines << "Member\tUsed\tGoverns\tBending (combo)\tShear (combo)\tLive defl.\tTotal defl.\tBearing\tStuds";
    for (const auto* c : shown_)
        lines << QString("%1\t%2\t%3\t%4 (%5)\t%6 (%7)\t%8\"\t%9\"\t%10\"\t%11")
                     .arg(QString::fromStdString(c->member->name), pct(c->maxRatio),
                          QString::fromStdString(c->governingCheck), pct(c->bendingRatio),
                          QString::fromStdString(c->bendingCombo), pct(c->shearRatio),
                          QString::fromStdString(c->shearCombo), num(c->liveDeflection, 2), num(c->totalDeflection, 2))
                     .arg(num(c->requiredBearing, 2))
                     .arg(c->jackStuds);
    lines << "";
    lines << "Preliminary design per NDS (ASD); assumptions: dry service, normal temperature, CL = 1, simple span.";
    return lines.join('\n');
}
