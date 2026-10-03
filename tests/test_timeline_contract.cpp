// Timeline intent is independent of metadata length and segment-list shape.
#include "VirtualDub/VDQtTimeline.h"
#include <QCoreApplication>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool cases() {
    VDQtTimeline timeline;
    timeline.reset(0, false);
    if (!check(timeline.isIdentity() && timeline.mapOutputToSource(0) == 0
               && timeline.mapOutputToSource(25) == 25,
               "unknown identity sources permit frame-zero and tail discovery")) return false;
    timeline.reset(10, false);
    if (!check(timeline.mapOutputToSource(10) == 10 && timeline.mapOutputToSource(30) == 30,
               "estimated identity lengths are not edit boundaries")) return false;
    VDQtTimeline explicitIdentity;
    explicitIdentity.reset(10, true);
    if (!explicitIdentity.replaceSegments({{0, 10}})) return false;
    if (!check(!explicitIdentity.isIdentity() && !explicitIdentity.isModified(),
               "verified full-source edits retain recompression/copy eligibility")) return false;
    explicitIdentity.setSourceFrameCount(15, false);
    if (!check(explicitIdentity.isModified() && explicitIdentity.mapOutputToSource(10) == -1,
               "explicit identity remains bounded after metadata refinement")) return false;
    timeline.setSourceFrameCount(32, true);
    if (!check(timeline.mapOutputToSource(31) == 31 && timeline.mapOutputToSource(32) == -1,
               "verified identity lengths remain bounded")) return false;

    if (!timeline.deleteRange(0, 32)) return false;
    if (!check(timeline.isEmpty() && timeline.isModified() && timeline.mapOutputToSource(0) == -1,
               "deleting all frames makes an explicit empty edit")) return false;
    timeline.setSourceFrameCount(0, false);
    timeline.setSourceFrameCount(40, true);
    if (!check(timeline.isEmpty() && timeline.isModified(),
               "metadata refinement cannot turn empty edits into source identity")) return false;
    if (!check(timeline.undo() && timeline.isIdentity() && timeline.frameCount() == 40
               && timeline.redo() && timeline.isEmpty() && timeline.isModified(),
               "undo/redo restore intent and use current identity metadata")) return false;
    if (!timeline.resetEdits()) return false;
    if (!check(timeline.isIdentity() && timeline.frameCount() == 40,
               "reset explicitly restores source identity")) return false;

    timeline.reset(0, false);
    if (!timeline.clear()) return false;
    if (!check(timeline.isModified() && timeline.mapOutputToSource(0) == -1
               && timeline.undo() && timeline.mapOutputToSource(0) == 0,
               "empty unknown identity and explicitly empty edits are distinct")) return false;
    timeline.reset(10, false);
    if (!timeline.cropToRange(2, 6)) return false;
    timeline.setSourceFrameCount(25, false);
    if (!check(timeline.frameCount() == 4 && timeline.mapOutputToSource(4) == -1,
               "edited estimated timelines stay bounded")) return false;
    if (!timeline.undo()) return false;
    timeline.setSourceFrameCount(25, false);
    return check(timeline.canRedo() && timeline.redo() && timeline.frameCount() == 4,
                 "unchanged metadata does not discard redo history");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (!cases()) return 1;
    std::cout << "Timeline contracts passed\n";
    return 0;
}
