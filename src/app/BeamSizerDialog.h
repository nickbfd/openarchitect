#pragma once

#include "oa/structural/sizing.h"

#include <QDialog>

#include <string>
#include <vector>

class QCheckBox;
class QTableWidget;

// Shows what a beam or header carries and every catalog member checked
// against it, passing members first and shallowest first.
class BeamSizerDialog : public QDialog {
    Q_OBJECT
public:
    BeamSizerDialog(const QString& title, oa::structural::BeamLoads loads,
                    oa::structural::DesignCriteria criteria, bool canAssign, QWidget* parent = nullptr);

    // The member picked with "Use selected member", if any.
    std::string chosenMemberId() const { return chosen_; }

private:
    void fillResults();
    QString report() const;

    oa::structural::BeamLoads loads_;
    oa::structural::DesignCriteria criteria_;
    std::vector<oa::structural::MemberCheck> checks_;
    std::vector<const oa::structural::MemberCheck*> shown_;
    QTableWidget* results_ = nullptr;
    QCheckBox* showFailing_ = nullptr;
    std::string chosen_;
    QString title_;
};
