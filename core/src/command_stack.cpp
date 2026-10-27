#include "boringshot/command_stack.h"

namespace bs {

void CommandStack::execute(std::unique_ptr<Command> cmd) {
    if (!cmd) return;
    cmd->apply();
    done_.push_back(std::move(cmd));
    undone_.clear();
    if (limit_ != 0 && done_.size() > limit_) done_.pop_front();
}

bool CommandStack::undo() {
    if (done_.empty()) return false;
    auto cmd = std::move(done_.back());
    done_.pop_back();
    cmd->revert();
    undone_.push_back(std::move(cmd));
    return true;
}

bool CommandStack::redo() {
    if (undone_.empty()) return false;
    auto cmd = std::move(undone_.back());
    undone_.pop_back();
    cmd->apply();
    done_.push_back(std::move(cmd));
    return true;
}

void CommandStack::clear() {
    done_.clear();
    undone_.clear();
}

}  // namespace bs
