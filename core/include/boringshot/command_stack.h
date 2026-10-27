#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <vector>

namespace bs {

class Command {
public:
    virtual ~Command() = default;
    virtual void apply() = 0;
    virtual void revert() = 0;
};

// Undo/redo stack. Executing a new command drops the redo history; the
// oldest entries are evicted once `limit` is exceeded (0 = unlimited).
class CommandStack {
public:
    explicit CommandStack(std::size_t limit = 256) : limit_(limit) {}

    void execute(std::unique_ptr<Command> cmd);
    bool undo();
    bool redo();
    void clear();

    bool canUndo() const { return !done_.empty(); }
    bool canRedo() const { return !undone_.empty(); }
    std::size_t undoCount() const { return done_.size(); }
    std::size_t redoCount() const { return undone_.size(); }

private:
    std::size_t limit_;
    std::deque<std::unique_ptr<Command>> done_;
    std::vector<std::unique_ptr<Command>> undone_;
};

}  // namespace bs
