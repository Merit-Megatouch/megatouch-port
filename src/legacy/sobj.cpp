// SObj: the older "AllocSprite" sprite API (euchre, hearts, spades, snubble).
//
// An SObj is a Sprite (0x200 bytes) whose animations are frame-counted events queued on a C list
// at +0x148. Games poll that list for empty to know a sprite is idle, so finished events must
// leave it. Each world frame the SObj copies its position to +0xec (previous frame), counts down
// its show delay (+0x110) and runs the due events; the world then reaps killed SObjs
// (+0xe4 & 0x40, or a fired DeleteAfterLast) and draws the visible ones (+0x108) in z order with
// the other sprites. Reference: docs/reference/sobj.md.
#include "sprite.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int PlayPreWave(char* name, unsigned short, bool, int vol, int, int);   // legacy.cpp
char* CommaStr(char* out, unsigned long n);                           // legacy.cpp

namespace {
enum Kind { K_MOVE, K_CURVE, K_NUM_INT, K_NUM_BYTE, K_ANIM_ALL, K_ANIM_OBJ, K_ANIM_REST, K_HIDE, K_DELETE, K_SOUND };
const int kForever = 999999999;

// A queued event. A Group, so ClearList(…, 4/8) can delete it through its virtual destructor.
struct SEvent : Group {
    SEvent() : Group(nullptr, nullptr) {}
    ~SEvent() override {}
    int kind = 0;
    uint32_t start = 0;                          // first world frame it runs on
    int frames = 1;                              // length (kForever: never ends)
    unsigned char ease = 0;
    float p[12] = {};                            // control points (x, y, z per point)
    void* target = nullptr;                      // CurveNum field
    Bitmap* anim = nullptr;
    int first = 0, last = 0, dur = 1, loops = 1;
    unsigned char unlink = 0;
    std::string wav;
    unsigned short wav_flags = 0;
    bool fired = false;
};

uint32_t now(const SObj*) { return legacy::world_frame_no(legacy::current_world()); }
float eased(float t, unsigned char e) { return e ? t * t * (3 - 2 * t) : t; }
float bezier(float a, float b, float c, float d, float t) {
    float u = 1 - t;
    return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d;
}
int chain_len(Bitmap* b) { int n = 0; for (; b; b = b->next) n++; return n; }
Bitmap* chain_at(Bitmap* b, int i) { while (b && b->prev) b = b->prev; for (; b && i > 0; i--) b = b->next; return b; }

void set_bmp(SObj* s, Bitmap* b) {
    s->bmp = b;
    if (b && reinterpret_cast<uintptr_t>(b) >= 0x10000) { s->w = (float)b->w; s->h = (float)b->h; }
}
std::vector<SEvent*> events_of(SObj* s) {
    std::vector<SEvent*> v;
    ListObj* h = s->events;
    for (ListObj* n = h ? h->next : nullptr; n && n->next; n = n->next) v.push_back(static_cast<SEvent*>(n->data));
    return v;
}
uint32_t end_of(const SEvent* e) { return e->frames >= kForever ? 0xffffffffu : e->start + (uint32_t)std::max(1, e->frames); }
// frame after the last pending event ends (now + 1 when idle)
uint32_t after_last(SObj* s) {
    uint32_t t = now(s) + 1;
    for (SEvent* e : events_of(s)) t = std::max(t, end_of(e));
    return t;
}
SEvent* queue(SObj* s, int kind, uint32_t start, int frames) {
    auto* e = new SEvent;
    e->kind = kind; e->start = start; e->frames = std::max(1, frames);
    if (!s->events) s->events = legacy::list_new_header();
    legacy::list_append(s->events, e);
    return e;
}
void free_events(SObj* s) {
    for (SEvent* e : events_of(s)) { legacy::list_remove(s->events, e); delete e; }
}
} // namespace

// ------------------------------------------------------------------------------ text
struct SObjText {
    std::string fmt;
    int* v1 = nullptr;
    int* v2 = nullptr;
    int last1 = 0, last2 = 0;
    BmpFont* font = nullptr;
    unsigned short w = 0, h = 0;
    int just = 0;
    short r = 0, g = 0, b = 0;
    Bitmap* bmp = nullptr;
};
static void render_text(SObj* s, const char* text) {
    SObjText* t = s->text;
    if (!t->bmp) t->bmp = new Bitmap(0, 0, 0, 16);
    t->bmp->CreateColoredSmackTextBox(text, reinterpret_cast<FontBase*>(t->font), (signed char)t->just, t->w, t->h,
                                      t->r, t->g, t->b, 0, false);
    set_bmp(s, t->bmp);
}
static void render_counter(SObj* s) {
    SObjText* t = s->text;
    char buf[256];
    if (t->fmt.find('%') != std::string::npos) snprintf(buf, sizeof buf, t->fmt.c_str(), t->v1 ? *t->v1 : 0, t->v2 ? *t->v2 : 0);
    else if (!t->fmt.empty() && t->fmt != "SCORECOMMASTR" && !t->v1) snprintf(buf, sizeof buf, "%s", t->fmt.c_str());
    else CommaStr(buf, (unsigned long)(t->v1 ? *t->v1 : 0));      // "" or SCORECOMMASTR: the number
    render_text(s, buf);
    t->last1 = t->v1 ? *t->v1 : 0;
    t->last2 = t->v2 ? *t->v2 : 0;
}
static SObjText* text_of(SObj* s, BmpFont* font, unsigned short w, unsigned short h, int just, short r, short g, short b) {
    if (!s->text) s->text = new SObjText;
    SObjText* t = s->text;
    t->font = font; t->w = w; t->h = h; t->just = just; t->r = r; t->g = g; t->b = b;
    t->v1 = t->v2 = nullptr;
    return t;
}

// ------------------------------------------------------------------------------ SObj
SObj::SObj(Bitmap* b, unsigned short fl) : Sprite(b, fl, nullptr) {
    memset(padac, 0, sizeof(SObj) - offsetof(SObj, padac));
    set_bmp(this, b);
    base_bmp = b;
    visible = 2;
    alloc_flags = fl;
    events = legacy::list_new_header();
    if (WorldClass* w = legacy::current_world()) if (w->sobj_list) legacy::list_append(w->sobj_list, this);
}
SObj::~SObj() {
    if (WorldClass* w = legacy::current_world()) if (w->sobj_list) legacy::list_remove(w->sobj_list, this);
    legacy::list_unlink_everywhere(this, events);
    if (events) {
        free_events(this);
        ListObj* h = events;
        events = nullptr;
        delete static_cast<ListObj*>(h->data);  // tail
        delete h;
    }
    if (text) { delete text->bmp; delete text; text = nullptr; }
}

void SObj::SetBmp(Bitmap* b) { set_bmp(this, b); base_bmp = b; }
void SObj::SetBmpSeq(Bitmap* seq) { set_bmp(this, chain_at(seq, seq_index)); }
void SObj::SetCord(int nx, int ny, int nz) { x = (float)nx; y = (float)ny; z = (float)nz; }

int SObj::MoveSprite(int x0, int y0, int z0, int x1, int y1, int z1, int frames, int delay, unsigned char e) {
    if (delay <= 0) { x = (float)x0; y = (float)y0; z = (float)z0; }
    SEvent* ev = queue(this, K_MOVE, now(this) + 1 + std::max(0, delay), frames);
    float pts[6] = {(float)x0, (float)y0, (float)z0, (float)x1, (float)y1, (float)z1};
    std::copy(pts, pts + 6, ev->p);
    ev->ease = e;
    return 1;
}
int SObj::CurveSprite(int a0, int a1, int a2, int b0, int b1, int b2, int c0, int c1, int c2, int d0, int d1, int d2, int frames,
                      int delay, unsigned char e) {
    if (delay <= 0) { x = (float)a0; y = (float)a1; z = (float)a2; }
    SEvent* ev = queue(this, K_CURVE, now(this) + 1 + std::max(0, delay), frames);
    float pts[12] = {(float)a0, (float)a1, (float)a2, (float)b0, (float)b1, (float)b2,
                     (float)c0, (float)c1, (float)c2, (float)d0, (float)d1, (float)d2};
    std::copy(pts, pts + 12, ev->p);
    ev->ease = e;
    return 1;
}
int SObj::CurveNum(int* p, int a, int b, int c, int d, int frames, int delay, unsigned char e) {
    if (!p) return 0;
    SEvent* ev = queue(this, K_NUM_INT, now(this) + 1 + std::max(0, delay), frames);
    ev->target = p; ev->ease = e;
    ev->p[0] = (float)a; ev->p[1] = (float)b; ev->p[2] = (float)c; ev->p[3] = (float)d;
    return 1;
}
int SObj::CurveNum(unsigned char* p, unsigned char a, unsigned char b, unsigned char c, unsigned char d, int frames, int delay,
                   unsigned char e) {
    if (!p) return 0;
    if (delay <= 0) *p = a;
    SEvent* ev = queue(this, K_NUM_BYTE, now(this) + 1 + std::max(0, delay), frames);
    ev->target = p; ev->ease = e;
    ev->p[0] = a; ev->p[1] = b; ev->p[2] = c; ev->p[3] = d;
    return 1;
}
void SObj::LoopAllAnim(Bitmap* anim, int loops, unsigned char, int delay) {
    if (!anim) return;
    while (anim->prev) anim = anim->prev;
    int n = chain_len(anim);
    SEvent* ev = queue(this, K_ANIM_ALL, now(this) + 1 + std::max(0, delay), loops >= kForever ? kForever : n * std::max(1, loops));
    ev->anim = anim;
}
static void obj_anim(SObj* s, Bitmap* anim, int first, int last, int dur, int loops, uint32_t start) {
    if (!anim) return;
    while (anim->prev) anim = anim->prev;
    int n = chain_len(anim);
    // frame numbers count from 1 (sobj.md §4.4: (1, 15) is a 15-frame pop)
    first = std::clamp(first - 1, 0, n - 1);
    last = std::clamp(last - 1, first, n - 1);
    dur = std::max(1, dur);
    SEvent* ev = queue(s, K_ANIM_OBJ, start, loops >= kForever ? kForever : dur * std::max(1, loops));
    ev->anim = anim; ev->first = first; ev->last = last; ev->dur = dur;
}
void SObj::LoopObjAnim(Bitmap* anim, int first, int last, int dur, int loops, int delay, unsigned char) {
    obj_anim(this, anim, first, last, dur, loops, now(this) + 1 + std::max(0, delay));
}
void SObj::LoopObjAnimAfterLast(Bitmap* anim, int first, int last, int dur, int loops, int delay, unsigned char) {
    obj_anim(this, anim, first, last, dur, loops, after_last(this) + std::max(0, delay));
}
void SObj::LoopRestAnim(unsigned char) {
    if (!bmp) return;
    int rest = 0;
    for (Bitmap* b = bmp->next; b; b = b->next) rest++;
    SEvent* ev = queue(this, K_ANIM_REST, now(this) + 1, std::max(1, rest));
    ev->anim = bmp;
}
void SObj::HideAfterLast(int delay) { queue(this, K_HIDE, after_last(this) + std::max(0, delay), 1); }
void SObj::DeleteAfterLast(int delay, unsigned char unlink) {
    queue(this, K_DELETE, after_last(this) + std::max(0, delay), 1)->unlink = unlink;
}
int SObj::FramesLeft() {
    uint32_t t = now(this), last = t;
    for (SEvent* e : events_of(this)) last = std::max(last, end_of(e) == 0xffffffffu ? t + kForever : end_of(e) - 1);
    return (int)(last - t);
}
void SObj::StripEvents(int) { free_events(this); }
void SObj::PlaySound(char* wav, unsigned short fl, unsigned long delay) {
    SEvent* ev = queue(this, K_SOUND, now(this) + 1 + (uint32_t)delay, 1);
    ev->wav = wav ? wav : "";                     // games pass the shared buffer world+0x64
    ev->wav_flags = fl;
}
void SObj::DebugSprite() {}

// one frame: previous position, show delay, due events (sobj.md §6)
int SObj::DoNextFrame() {
    prev_x = x; prev_y = y; prev_z = z;
    if (show_delay > 0 && --show_delay == 0) visible = 2;
    uint32_t t = now(this);
    for (SEvent* e : events_of(this)) {
        if (!events || t < e->start) continue;
        int rel = (int)(t - e->start);           // 0 on the first frame
        float f = e->frames >= kForever ? 0 : std::min(1.0f, (rel + 1) / (float)e->frames);
        float k = eased(f, e->ease);
        switch (e->kind) {
        case K_MOVE:
            x = e->p[0] + (e->p[3] - e->p[0]) * k; y = e->p[1] + (e->p[4] - e->p[1]) * k; z = e->p[2] + (e->p[5] - e->p[2]) * k;
            break;
        case K_CURVE:
            x = bezier(e->p[0], e->p[3], e->p[6], e->p[9], k);
            y = bezier(e->p[1], e->p[4], e->p[7], e->p[10], k);
            z = bezier(e->p[2], e->p[5], e->p[8], e->p[11], k);
            break;
        case K_NUM_INT: *static_cast<int*>(e->target) = (int)std::lround(bezier(e->p[0], e->p[1], e->p[2], e->p[3], k)); break;
        case K_NUM_BYTE:
            *static_cast<unsigned char*>(e->target) = (unsigned char)std::clamp((int)std::lround(bezier(e->p[0], e->p[1], e->p[2], e->p[3], k)), 0, 255);
            break;
        case K_ANIM_ALL: set_bmp(this, chain_at(e->anim, rel % std::max(1, chain_len(e->anim)))); break;
        case K_ANIM_OBJ: {
            int n = e->last - e->first + 1;
            set_bmp(this, chain_at(e->anim, e->first + (rel % e->dur) * n / e->dur));
            break;
        }
        case K_ANIM_REST: { Bitmap* b = e->anim; for (int i = 0; i <= rel && b && b->next; i++) b = b->next; set_bmp(this, b); break; }
        case K_HIDE: visible = 0; break;
        case K_DELETE: sflags |= 0x40; kill_unlink = e->unlink; break;
        case K_SOUND: if (!e->wav.empty()) PlayPreWave(const_cast<char*>(e->wav.c_str()), e->wav_flags, true, 255, 1000, 127); break;
        }
        if (e->frames < kForever && rel + 1 >= e->frames && events && legacy::list_remove(events, e)) delete e;
    }
    // live counters (AssignString1/2)
    if (text && text->v1 && (*text->v1 != text->last1 || (text->v2 && *text->v2 != text->last2))) render_counter(this);
    return 1;
}
int SObj::DoDraw() {
    if (!visible || (sflags & 0x40)) return 1;
    int clip[4] = {clip_x1, clip_y1, clip_x2, clip_y2};
    float dx = x, dy = y;
    double sx = scale_x ? scale_x / 65536.0 : 1.0, sy = scale_y ? scale_y / 65536.0 : 1.0;
    if (flags & 0x4000) dx -= (float)(w * sx / 2);   // Sprite centring flags (snubble balls: 0xc000)
    if (flags & 0x8000) dy -= (float)(h * sy / 2);
    legacy::sobj_blit(bmp, dx, dy, scale_x, scale_y, transparency, sflags & 1, clip_on ? clip : nullptr);
    return 1;
}

// ------------------------------------------------------------------------------ world side
class VideoClassDbg;
class VideoClass { public: int GetVB(); };
class VideoClassDbg : public VideoClass {};
namespace legacy {
void sobj_reap(WorldClass* w) {
    if (!w || !w->sobj_list) return;
    static bool dbg = getenv("MEGA_DEBUG_SOBJ") != nullptr;
    if (dbg && world_frame_no(w) % 30 == 0) {
        int n = 0, vis = 0;
        for (ListObj* nd = w->sobj_list->next; nd && nd->next; nd = nd->next) {
            auto* s = static_cast<SObj*>(nd->data);
            n++; if (s->visible) vis++;
            if (s->visible && vis <= 8) fprintf(stderr, "[sobj] %p bmp=%p %dx%d at %.0f,%.0f z=%.0f vis=%d tr=%d ev=%d fl=%x\n", (void*)s, (void*)s->bmp,
                                s->bmp ? s->bmp->w : 0, s->bmp ? s->bmp->h : 0, s->x, s->y, s->z, s->visible, s->transparency,
                                (int)events_of(s).size(), s->flags);
        }
        fprintf(stderr, "[sobj] frame %u: %d sobjs, %d visible, %zu sprites, vb %d\n", world_frame_no(w), n, vis, w->sprites->size(),
                reinterpret_cast<VideoClassDbg*>(0)->GetVB());
    }
    std::vector<SObj*> dead;
    for (ListObj* n = w->sobj_list->next; n && n->next; n = n->next) {
        auto* s = static_cast<SObj*>(n->data);
        if (s && (s->sflags & 0x40)) dead.push_back(s);
    }
    for (SObj* s : dead) delete s;              // virtual: a game's Ball runs its own dtor first
}
}

SObj* WorldClass::AllocSprite(Bitmap* b, unsigned short fl) { return new SObj(b, fl); }
void WorldClass::DeleteSpriteAllLists(SObj* s, unsigned char) { delete s; }
void WorldClass::AssignString(SObj* s, char const* text, int x, int y, int z, BmpFont* font, unsigned short w, unsigned short h,
                              int just, signed char, short r, short g, short b, unsigned char, unsigned char) {
    if (!s) return;
    text_of(s, font, w, h, just, r, g, b);
    s->x = (float)x; s->y = (float)y; s->z = (float)z;
    render_text(s, text ? text : "");
}
void WorldClass::AssignString1(SObj* s, char const* fmt, int* v, int x, int y, int z, BmpFont* font, unsigned short w,
                               unsigned short h, int just, signed char, short r, short g, short b, unsigned char, unsigned char) {
    if (!s) return;
    SObjText* t = text_of(s, font, w, h, just, r, g, b);
    t->fmt = fmt ? fmt : ""; t->v1 = v;
    s->x = (float)x; s->y = (float)y; s->z = (float)z;
    render_counter(s);
}
void WorldClass::AssignString2(SObj* s, char const* fmt, int* v1, int* v2, int x, int y, int z, BmpFont* font, unsigned short w,
                               unsigned short h, int just, signed char, short r, short g, short b, unsigned char, unsigned char) {
    if (!s) return;
    SObjText* t = text_of(s, font, w, h, just, r, g, b);
    t->fmt = fmt ? fmt : ""; t->v1 = v1; t->v2 = v2;
    s->x = (float)x; s->y = (float)y; s->z = (float)z;
    render_counter(s);
}

// ------------------------------------------------------------------------------ C list helpers
// ClearList: a NULL list gets a fresh header; otherwise every node goes and the header stays.
// flags 4 / 8: also delete the data (virtual dtor); 0x40: the data are SObjs.
void ClearList(ListObj** pp, unsigned char fl) {
    if (!pp) return;
    if (!*pp) { *pp = legacy::list_new_header(); return; }
    ListObj* h = *pp;
    while (h->prev) h = h->prev;
    std::vector<void*> data;
    for (ListObj* n = h->next; n && n->next; n = n->next) data.push_back(n->data);
    for (void* d : data) legacy::list_remove(h, d);
    if (fl & (0x04 | 0x08 | 0x40))
        for (void* d : data) if (d) delete static_cast<Group*>(d);
}
// moves node n one step toward the tail (swaps it with its successor)
void PushListObjForward(ListObj* n) {
    if (!n || !n->prev || !n->next || !n->next->next) return;
    ListObj* m = n->next;
    ListObj *a = n->prev, *b = m->next;
    a->next = m; m->prev = a;
    m->next = n; n->prev = m;
    n->next = b; b->prev = n;
}
