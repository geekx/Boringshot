#include <memory>

#include "boringshot/command_stack.h"
#include "bstest.h"

using namespace bs;

namespace {
struct Add : Command {
    int& v;
    int d;
    Add(int& value, int delta) : v(value), d(delta) {}
    void apply() override { v += d; }
    void revert() override { v -= d; }
};
}  // namespace

TEST(command_undo_redo_roundtrip) {
    int v = 0;
    CommandStack s;
    CHECK(!s.undo());
    CHECK(!s.redo());
    s.execute(std::make_unique<Add>(v, 1));
    s.execute(std::make_unique<Add>(v, 10));
    CHECK(v == 11);
    CHECK(s.undo());
    CHECK(v == 1);
    CHECK(s.canRedo());
    CHECK(s.redo());
    CHECK(v == 11);
    CHECK(s.undoCount() == 2);
}

TEST(command_execute_drops_redo) {
    int v = 0;
    CommandStack s;
    s.execute(std::make_unique<Add>(v, 1));
    s.undo();
    s.execute(std::make_unique<Add>(v, 5));
    CHECK(!s.canRedo());
    CHECK(v == 5);
}

TEST(command_limit_evicts_oldest) {
    int v = 0;
    CommandStack s(2);
    for (int i = 0; i < 5; ++i) s.execute(std::make_unique<Add>(v, 1));
    CHECK(v == 5);
    CHECK(s.undoCount() == 2);
    while (s.undo()) {
    }
    CHECK(v == 3);  // only the last two are undoable
}

TEST(command_null_and_clear) {
    CommandStack s;
    s.execute(nullptr);
    CHECK(!s.canUndo());
    int v = 0;
    s.execute(std::make_unique<Add>(v, 1));
    s.clear();
    CHECK(!s.canUndo());
    CHECK(!s.canRedo());
}
