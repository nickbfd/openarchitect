#include "oa/core/undo.h"

#include <cstdint>
#include <stdexcept>

namespace oa {
namespace {
const std::string kEmpty;
}

void UndoStack::push(std::string text, Document before, Document after) {
    if (clean_ > index_) clean_ = SIZE_MAX;
    steps_.resize(index_);
    steps_.push_back({std::move(text), std::move(before), std::move(after)});
    ++index_;
    if (steps_.size() > limit) {
        steps_.erase(steps_.begin());
        --index_;
        clean_ = clean_ == 0 || clean_ == SIZE_MAX ? SIZE_MAX : clean_ - 1;
    }
}

const std::string& UndoStack::undoText() const { return canUndo() ? steps_[index_ - 1].text : kEmpty; }
const std::string& UndoStack::redoText() const { return canRedo() ? steps_[index_].text : kEmpty; }

const Document& UndoStack::undo() {
    if (!canUndo()) throw std::logic_error("nothing to undo");
    return steps_[--index_].before;
}

const Document& UndoStack::redo() {
    if (!canRedo()) throw std::logic_error("nothing to redo");
    return steps_[index_++].after;
}

void UndoStack::clear() {
    steps_.clear();
    index_ = 0;
    clean_ = 0;
}

} // namespace oa
