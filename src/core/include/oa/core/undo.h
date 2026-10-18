#pragma once

#include "oa/core/document.h"

#include <string>
#include <vector>

namespace oa {

// Undo history shared by every editor. Each step stores the document before
// and after an edit. Plans are small enough that whole-document snapshots are
// simple and reliable; finer-grained commands can replace them later without
// changing callers.
class UndoStack {
public:
    // Records an edit that has already been applied, turning `before` into `after`.
    void push(std::string text, Document before, Document after);

    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ < steps_.size(); }
    const std::string& undoText() const;
    const std::string& redoText() const;

    // Return the document state to restore.
    const Document& undo();
    const Document& redo();

    void clear();
    void setClean() { clean_ = index_; }
    bool isClean() const { return clean_ == index_; }

    size_t limit = 200;

private:
    struct Step {
        std::string text;
        Document before;
        Document after;
    };
    std::vector<Step> steps_;
    size_t index_ = 0;           // number of applied steps
    size_t clean_ = 0;           // index at last save; SIZE_MAX when unreachable
};

} // namespace oa
