#include "app/bubble/mode_select.h"

#include <cmath>
#include <cstring>

namespace bbl {

static const ModeInfo kSlots[kModeSlots] = {
    { 0, ModeId::kDuel,  "DUEL",  "VS", "3 SHOTS EACH, PUSH GARBAGE OVER", nullptr, enterDuelMode  },
    BBL_EMPTY_SLOT(1),
    BBL_EMPTY_SLOT(2),
    { 3, ModeId::kStage, "STAGE", "1P", "CLEAR ONE BOARD, KEEP GOING",     nullptr, enterStageMode },
    BBL_EMPTY_SLOT(4),
    BBL_EMPTY_SLOT(5),
};

void ModeSlotPos(int slot, float* x, float* y)
{
    const float rx = 300.0f, ry = 140.0f;
    const float a = (-90.0f + (float)slot * 60.0f) * 3.14159265358979f / 180.0f;
    if (x) *x = 480.0f + rx * cosf(a);
    if (y) *y = 286.0f + ry * sinf(a);
}

const ModeInfo* ModeAtSlot(int slot)
{
    // Scan by the slot field, never by array index: reordering the table must
    // not move anything on the ring.
    for (int i = 0; i < kModeSlots; ++i)
        if (kSlots[i].slot == slot) return &kSlots[i];
    return nullptr;
}

int ModeDefaultSlot()
{
    for (int i = 0; i < kModeSlots; ++i)
        if (kSlots[i].id == ModeId::kDuel) return kSlots[i].slot;
    return 0;
}

int ModeNextSlot(int slot, int dir)
{
    int s = slot + (dir < 0 ? -1 : 1);
    if (s < 0) s += kModeSlots;
    if (s >= kModeSlots) s -= kModeSlots;
    return s;
}

bool ModeTableSelfCheck(const char** why)
{
    auto fail = [why](const char* m) { if (why) *why = m; return false; };
    int seen[kModeSlots] = { 0 };
    for (int i = 0; i < kModeSlots; ++i) {
        const ModeInfo& m = kSlots[i];
        if (m.slot < 0 || m.slot >= kModeSlots) return fail("slot out of range");
        if (seen[m.slot]++) return fail("duplicate slot");
        if (!m.name || !m.name[0]) return fail("empty name");
        if (strlen(m.name) > 12) return fail("name longer than 12 chars");
        if (m.blurb && strlen(m.blurb) > 40) return fail("blurb longer than 40 chars");
        for (const char* p = m.name; *p; ++p)
            if ((unsigned char)*p < 32 || (unsigned char)*p > 127)
                return fail("non-ASCII name");
        if (m.blurb)
            for (const char* p = m.blurb; *p; ++p)
                if ((unsigned char)*p < 32 || (unsigned char)*p > 127)
                    return fail("non-ASCII blurb");
        if (m.id == ModeId::kNone && m.enter != nullptr)
            return fail("empty slot has an enter function");
        if (m.id != ModeId::kNone && m.enter == nullptr)
            return fail("playable slot without enter function");
    }
    for (int i = 0; i < kModeSlots; ++i)
        if (seen[i] != 1) return fail("slot not used exactly once");
    if (why) *why = "ok";
    return true;
}

} // namespace bbl
