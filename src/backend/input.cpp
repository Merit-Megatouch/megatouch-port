// Input: mouse/touch → Input::BaseInputManager.

#include "backend.h"
#include <cstring>
#include <deque>

// ---------------------------------------------------------------------------
// Input. BaseInputManager keeps a std::deque<PendingKey> at +0x94 and the mouse
// position as floats at +0xc4/+0xc8. Each update we queue the current button state
// ({2,key} = down, {1,key} = up) and let the base class derive press/release edges.

struct PendingKey { int state; int key; };
static void** g_inVtbl;
bool g_mouseDown;
float g_mouseX, g_mouseY;   // set by device.cpp from SDL events

static void in_update(void* self) {
    auto& q = at<std::deque<PendingKey>>(self, 0x94);
    at<float>(self, 0xc4) = g_mouseX;
    at<float>(self, 0xc8) = g_mouseY;
    q.push_back({g_mouseDown ? 2 : 1, 0});
    reinterpret_cast<void (*)(void*)>(sym("_ZN5Input16BaseInputManager6UpdateEv"))(self);
}

void* in_create() {
    void*& cur = *static_cast<void**>(sym("_ZN5Input13IInputManager18m_pCurrentInstanceE"));
    if (cur) return cur;
    if (!g_inVtbl) {
        g_inVtbl = clone_vtable("_ZTVN5Input16BaseInputManagerE");
        g_inVtbl[0] = (void*)in_update;
    }
    void* m = operator new(0xf0);
    memset(m, 0, 0xf0);
    reinterpret_cast<ctor0_t>(sym("_ZN5Input16BaseInputManagerC2Ev"))(m);
    at<void**>(m, 0) = g_inVtbl;
    cur = m;
    return m;
}
