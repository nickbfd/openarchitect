#include "Workspace.h"

#include "oa/core/serialization.h"

#include <filesystem>

Workspace::Workspace(QObject* parent) : QObject(parent) { newDocument(); }

void Workspace::edit(const QString& text, const std::function<void(oa::Document&)>& change) {
    oa::Document before = doc_;
    change(doc_);
    const bool wasModified = isModified();
    undo_.push(text.toStdString(), std::move(before), doc_);
    loads_.reset();
    validateState();
    emit documentChanged();
    if (wasModified != isModified()) emit modifiedChanged(isModified());
}

void Workspace::undo() {
    if (canUndo()) replaceDocument(undo_.undo());
}

void Workspace::redo() {
    if (canRedo()) replaceDocument(undo_.redo());
}

void Workspace::replaceDocument(const oa::Document& doc) {
    const bool wasModified = isModified();
    doc_ = doc;
    loads_.reset();
    validateState();
    emit documentChanged();
    if (wasModified != isModified()) emit modifiedChanged(isModified());
}

void Workspace::newDocument(oa::Document doc) {
    doc_ = std::move(doc);
    undo_.clear();
    filePath_.clear();
    loads_.reset();
    selection_ = oa::kNoId;
    currentLevel_ = doc_.levels.empty() ? oa::kNoId : doc_.levelsByElevation().front()->id;
    validateState();
    emit documentChanged();
    emit currentLevelChanged();
    emit selectionChanged();
    emit modifiedChanged(false);
}

bool Workspace::open(const QString& path, QString* error) {
    try {
        oa::Document doc = oa::loadDocument(std::filesystem::path(path.toStdWString()));
        newDocument(std::move(doc));
        filePath_ = path;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
}

bool Workspace::save(const QString& path, QString* error) {
    try {
        oa::saveDocument(doc_, std::filesystem::path(path.toStdWString()));
        filePath_ = path;
        undo_.setClean();
        emit modifiedChanged(false);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
}

void Workspace::setCurrentLevel(oa::ObjectId id) {
    if (id == currentLevel_ || !doc_.level(id)) return;
    currentLevel_ = id;
    select(oa::kNoId);
    emit currentLevelChanged();
}

void Workspace::select(oa::ObjectId id) {
    if (id == selection_) return;
    selection_ = id;
    emit selectionChanged();
}

QString Workspace::describe(oa::ObjectId id) const {
    const auto& d = doc_;
    if (d.wall(id)) return QString("Wall %1").arg(id);
    if (const oa::Opening* o = d.opening(id))
        return QString("%1 %2").arg(o->kind == oa::OpeningKind::Door ? "Door" : "Window").arg(id);
    if (d.beam(id)) return QString("Beam %1").arg(id);
    if (d.roof(id)) return QString("Roof %1").arg(id);
    if (d.roofPlane(id)) return QString("Roof plane %1").arg(id);
    if (oa::Document::findIn(d.platforms, id)) return QString("Floor framing %1").arg(id);
    if (const oa::Room* r = oa::Document::findIn(d.rooms, id))
        return QString("Room \"%1\"").arg(QString::fromStdString(r->name));
    return QString();
}

const oa::structural::TakedownResult& Workspace::loads() const {
    if (!loads_) loads_ = oa::structural::runTakedown(doc_);
    return *loads_;
}

// Keeps the current level and selection pointing at objects that exist.
void Workspace::validateState() {
    if (!doc_.level(currentLevel_)) {
        auto levels = doc_.levelsByElevation();
        currentLevel_ = levels.empty() ? oa::kNoId : levels.front()->id;
        emit currentLevelChanged();
    }
    if (selection_ == oa::kNoId) return;
    if (!doc_.contains(selection_) || doc_.level(selection_)) {
        selection_ = oa::kNoId;
        emit selectionChanged();
    }
}
