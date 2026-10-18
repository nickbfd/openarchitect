#pragma once

#include "oa/core/document.h"
#include "oa/core/undo.h"
#include "oa/structural/takedown.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

// The open plan as the UI sees it: the document, its undo history, the file
// it came from, the current level and selection, and cached load results.
// Every change goes through edit() so undo and the views stay in step.
class Workspace : public QObject {
    Q_OBJECT
public:
    explicit Workspace(QObject* parent = nullptr);

    const oa::Document& doc() const { return doc_; }

    // Applies `change` to a copy of the document and records it for undo.
    void edit(const QString& text, const std::function<void(oa::Document&)>& change);

    void undo();
    void redo();
    bool canUndo() const { return undo_.canUndo(); }
    bool canRedo() const { return undo_.canRedo(); }
    QString undoText() const { return QString::fromStdString(undo_.undoText()); }
    QString redoText() const { return QString::fromStdString(undo_.redoText()); }

    void newDocument(oa::Document doc = oa::Document::createDefault());
    bool open(const QString& path, QString* error);
    bool save(const QString& path, QString* error);
    QString filePath() const { return filePath_; }
    bool isModified() const { return !undo_.isClean(); }

    oa::ObjectId currentLevel() const { return currentLevel_; }
    void setCurrentLevel(oa::ObjectId id);

    oa::ObjectId selection() const { return selection_; }
    void select(oa::ObjectId id);

    // Load takedown for the current document, recomputed after any change.
    const oa::structural::TakedownResult& loads() const;

signals:
    void documentChanged();
    void selectionChanged();
    void currentLevelChanged();
    void modifiedChanged(bool modified);

private:
    void replaceDocument(const oa::Document& doc);
    void validateState();

    oa::Document doc_;
    oa::UndoStack undo_;
    QString filePath_;
    oa::ObjectId currentLevel_ = oa::kNoId;
    oa::ObjectId selection_ = oa::kNoId;
    mutable std::optional<oa::structural::TakedownResult> loads_;
};
