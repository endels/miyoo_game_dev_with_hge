#ifndef BBL_MODE_SELECT_H
#define BBL_MODE_SELECT_H

namespace bbl {

class BubbleApp;

// One row per ring slot. The slot number decides the position on the ring
// (angle = -90 + slot*60), so adding a mode only means filling one EMPTY row:
// no layout, state machine or main.cpp change is needed.
enum class ModeId : int { kNone = 0, kDuel, kStage, kCount };

struct ModeInfo {
    int         slot;      // 0..5, position on the ring (fixed)
    ModeId      id;        // kNone = empty slot
    const char* name;      // ASCII only (the built-in font is 32..127)
    const char* tag;       // short corner chip ("VS" / "1P" / "LOCK")
    const char* blurb;     // bottom bar line, ASCII
    const char* iconKey;   // pak texture key; nullptr = empty slot / built-in
    void      (*enter)(BubbleApp&);   // nullptr for empty slots
};

const int kModeSlots = 6;

#define BBL_EMPTY_SLOT(s) { s, ModeId::kNone, "COMING SOON", "LOCK", \
                            "A NEW MODE IS IN THE WORKS", nullptr, nullptr }

// Implemented in bubble_app.cpp; referenced by the table so the UI module stays
// free of game rules.
void enterDuelMode(BubbleApp& app);
void enterStageMode(BubbleApp& app);

void ModeSlotPos(int slot, float* x, float* y);
const ModeInfo* ModeAtSlot(int slot);
int  ModeDefaultSlot();
int  ModeNextSlot(int slot, int dir);
bool ModeTableSelfCheck(const char** why);

} // namespace bbl

#endif // BBL_MODE_SELECT_H
