// The loader's sprite engine, reimplemented from how ~90 legacy games use it.
//
// Games build their screens from Sprites (bitmap and/or text, x/y/z, flags) owned by the current
// world (MegacGlobals+0x207c), and run it by calling WorldClass::PlayMovie(1, ...) once per loop
// iteration. Each engine frame: due events run (timed queue +0x88, per-frame +0x80), sprites
// update (DoNextFrame), sprites flagged 8 are deleted, everything is drawn in z order over the
// background, and touches go to the topmost clickable sprite (SpriteClick + click events +0x7c).
// The animated setters (Move, FadeIn, RunAnim, delayed SetCord/Enable/...) are engine EventOs
// queued on +0x88. Reference: docs/reference/sprite-engine.md.
#include "sprite.h"
#include "../common/env.h"
#include <algorithm>
#include <array>
#include <memory>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

class MegacGlobals { public: static MegacGlobals* GetInstance(); };   // megatouch-host
int PlayPreWave(char* name, unsigned short, bool, int vol, int, int);  // legacy.cpp

namespace {
const uint32_t kForever = 0xefffffff;
enum : uint32_t { F_DELETE = 0x8, F_DIRTY = 0x10, F_DISABLED = 0x80, F_CLICK = 0x100, F_CENTER_X = 0x4000, F_CENTER_Y = 0x8000 };

WorldClass*& world_slot() {
    auto* g = reinterpret_cast<unsigned char*>(MegacGlobals::GetInstance());
    return *reinterpret_cast<WorldClass**>(g + 0x207c);
}
// sprites created while no world exists (rare): adopted by the next world
std::set<Sprite*>& orphans() { static std::set<Sprite*> s; return s; }
}

namespace legacy {
WorldClass* current_world() { return world_slot(); }
}

struct WorldState {
    Bitmap* back = nullptr;                      // background (owned when back_owned)
    bool back_owned = false;
    std::vector<uint32_t> back_px;               // captured video buffer (SetBack(short))
    Uint32 last_frame = 0, start = 0;
    Bitmap* draw_target = nullptr;
    std::set<Bitmap*> loaded;                    // LoadBmp results (first frames)
    Sprite* pressed = nullptr;                   // sprite under the current touch
};

struct SpriteState {
    bool sized = false;                          // SetSize called: keep w/h when the bitmap changes
    bool colored = false;
    int matrix[9] = {255, 0, 0, 0, 255, 0, 0, 0, 255};
    int rgb[3] = {0, 0, 0};                      // bitmap colour offsets (SetRGB)
    int text_rgb[3] = {0, 0, 0};                 // text colour offsets (SetTextRGB), applied over the String's
    bool text_rgb_set = false;
    bool shadow = false;
    float shadow_dx = 0, shadow_dy = 0;
    int shadow_rgb[3] = {-240, -240, -240};
    BITMAP* text_cache = nullptr;                // rendered String
    std::string cache_key;
    Bitmap* anim_chain = nullptr;                // RunDiskAnimThenDelete: owned chain
    bool hq = false;
};

// ------------------------------------------------------------------------------ Group
Group::Group(List* l, Group* o) : children(l), owner(o) {}
Group::~Group() {}
int Group::Signal(SpriteSignal*) { return 0; }
int Group::Signal(SpriteSignalType t) { SpriteSignal s(t, nullptr, nullptr, nullptr); return Signal(&s); }
int Group::Message(SpriteSignal*) { return 0; }
int Group::Message(SpriteSignalType t) { SpriteSignal s(t, nullptr, nullptr, nullptr); return Message(&s); }

SpriteSignal::SpriteSignal(Group::SpriteSignalType t, Sprite* a, Sprite* b, Gash* g)
    : Group(nullptr, nullptr), type(t), from(a), to(b), gash(g) {}
SpriteSigHand::SpriteSigHand() : Group(nullptr, nullptr) {}

// ------------------------------------------------------------------------------ lists
ListObj::ListObj() : Group(nullptr, nullptr), data(nullptr), prev(nullptr), next(nullptr) {}
ListObj::~ListObj() {}
ListObjHeader::ListObjHeader() : extra(0) {}

static ListObjHeader* new_header() {
    auto* h = new ListObjHeader;
    auto* t = new ListObj;
    h->data = t; h->prev = nullptr; h->next = t;
    t->prev = h; t->next = nullptr; t->data = h;
    return h;
}
static ListObj* find_header(ListObj* n) {
    while (n && n->prev) n = n->prev;
    return n;
}
static ListObj* tail_of(ListObj* h) { return h ? static_cast<ListObj*>(h->data) : nullptr; }
static void unlink(ListObj* n) {
    if (n->prev) n->prev->next = n->next;
    if (n->next) n->next->prev = n->prev;
}

List::List(List* src) : Group(nullptr, nullptr), cur(nullptr), node(nullptr), owns(0) {
    if (!src) { node = new_header(); owns = 1; }
    else SetList(src->header());
}
List::List(ListObj* h) : Group(nullptr, nullptr), cur(nullptr), node(nullptr), owns(0) { SetList(h); }
static void free_owned(List* l) {
    if (l->owns && l->node) {
        ListObj* h = l->header();
        ListObj* n = h->next;
        while (n && n->next) { ListObj* nx = n->next; delete n; n = nx; }
        delete tail_of(h);
        delete h;
    }
    l->node = nullptr; l->owns = 0;
}
List::~List() { free_owned(this); }
ListObj* List::header() const { return find_header(node); }
// as the inline ListT::SetList: a header steps to its first node; the tail sentinel means "end"
void List::SetList(ListObj* p) {
    if (node && owns && node != p) free_owned(this);
    node = p; cur = nullptr;
    if (!p) return;
    if (!p->prev) { p = p->next; node = p; if (!p) return; }
    cur = p->next ? static_cast<Group*>(p->data) : nullptr;
}
void List::LinkInto(Group* g, int pos) {
    ListObj* h = header();
    if (!h) { node = h = new_header(); owns = 1; }
    auto* n = new ListObj;
    n->data = g;
    if (pos == 2) {                               // front
        n->prev = h; n->next = h->next; h->next->prev = n; h->next = n;
    } else {                                      // append before the tail sentinel
        ListObj* t = tail_of(h);
        n->prev = t->prev; n->next = t; t->prev->next = n; t->prev = n;
    }
}
void List::Push(Group* g) { LinkInto(g, 1); }
void List::Clear(unsigned char flags) {
    ListObj* h = header();
    if (!h) { node = new_header(); owns = 1; cur = nullptr; return; }
    ListObj* t = tail_of(h);
    ListObj* n = h->next;
    h->next = t; t->prev = h;
    while (n && n != t) {
        ListObj* nx = n->next;
        if ((flags & 0x0a) && n->data) delete static_cast<Group*>(n->data);   // 2/8: delete the elements
        delete n;
        n = nx;
    }
    if (node != h) node = h;
    cur = nullptr;
}
bool List::IsEmpty() { ListObj* h = header(); return !h || h->next == tail_of(h); }
int List::Count() { int c = 0; each([&](Group*) { c++; }); return c; }
bool List::Contains(Group* g) { bool f = false; each([&](Group* x) { if (x == g) f = true; }); return f; }
bool List::Advance(int n) {
    for (; n; n--) { if (!node || !node->next) { cur = nullptr; return false; } node = node->next; }
    if (!node || !node->next) { cur = nullptr; return false; }
    cur = static_cast<Group*>(node->data);
    return true;
}
bool List::Backup(int n) {
    for (; n; n--) { if (!node || !node->prev || !node->prev->prev) { return false; } node = node->prev; }
    cur = node && node->next ? static_cast<Group*>(node->data) : nullptr;
    return cur != nullptr;
}
Group* List::Pop() {
    ListObj* h = header();
    if (!h || h->next == tail_of(h)) return nullptr;
    ListObj* n = h->next;
    Group* g = static_cast<Group*>(n->data);
    if (node == n) node = h;
    unlink(n); delete n;
    return g;
}
void List::RemoveFrom(Group* g) {
    ListObj* h = header();
    if (!h) return;
    for (ListObj* n = h->next; n && n->next; n = n->next)
        if (n->data == g) { if (node == n) node = n->prev; unlink(n); delete n; return; }
}
void List::Remove(Group* g) { RemoveFrom(g); }
void List::UnLinkAndAdvance() {
    if (!node || !node->prev || !node->next) return;
    ListObj* n = node;
    node = n->next;
    unlink(n); delete n;
    cur = node && node->next ? static_cast<Group*>(node->data) : nullptr;
}
void List::Dump() {}
template <class F> void List::each(F f) {
    ListObj* h = header();
    if (!h) return;
    std::vector<Group*> v;
    for (ListObj* n = h->next; n && n->next; n = n->next) v.push_back(static_cast<Group*>(n->data));
    for (Group* g : v) f(g);
}

template <class T> ListT<T>::ListT(List* src) : List(src) {}
template <class T> ListT<T>::~ListT() {}
template <class T> bool ListT<T>::Advance(int n) { return List::Advance(n); }
template <class T> void ListT<T>::UnLinkAndAdvance() { List::UnLinkAndAdvance(); }
template <class T> void ListT<T>::Set(List* l) { SetList(l ? l->header() : nullptr); }
template class ListT<Sprite>;
template class ListT<EventO>;

// C list API on raw headers
void LinkIntoList(ListObj* h, void* data, int pos) {
    if (!h) return;
    List l(static_cast<ListObj*>(nullptr));
    l.node = find_header(h);
    l.LinkInto(static_cast<Group*>(data), pos);
    l.node = nullptr;
}
void RemoveFromList(ListObj* h, void* data) {
    if (!h) return;
    List l(static_cast<ListObj*>(nullptr));
    l.node = find_header(h);
    l.RemoveFrom(static_cast<Group*>(data));
    l.node = nullptr;
}
bool IsInList(ListObj* h, void* data) {
    ListObj* hd = find_header(h);
    for (ListObj* n = hd ? hd->next : nullptr; n && n->next; n = n->next) if (n->data == data) return true;
    return false;
}

// ------------------------------------------------------------------------------ events
EventO::EventO() : Group(nullptr, nullptr), start(0), end(0), kind(0) {}
EventO::~EventO() {}
int EventO::DoIt(Sprite*, int) { return 1; }
int EventO::GetType() { return kind; }

namespace {
WorldClass* W() { return legacy::current_world(); }
uint32_t now() { WorldClass* w = W(); return w ? w->clock : legacy::ticks(); }

enum Kind : unsigned char { K_GAME = 0, K_CALL = 1, K_MOVE = 2, K_ENABLE = 3, K_FADE = 4, K_ANIM = 5, K_MISC = 6 };

double ease(double t, int smooth) {
    t = std::clamp(t, 0.0, 1.0);
    switch (smooth & 3) {
    case 1: return t * t;                                   // accelerate
    case 2: return 1 - (1 - t) * (1 - t);                   // decelerate
    case 3: return t * t * (3 - 2 * t);                     // both
    default: return t;
    }
}

// an engine event: runs `step(sprite, t)` with t 0..1 between start and end
class EngineEvent : public EventO {
public:
    std::function<void(Sprite*, double)> step;
    bool done = false;
    int DoIt(Sprite* s, int) override {
        uint32_t t = now();
        if (t < start) return 1;
        double f = end > start ? double(t - start) / double(end - start) : 1.0;
        step(s, std::min(1.0, f));
        if (t >= end) done = true;
        return 1;
    }
};
EngineEvent* add_event(Sprite* s, Kind k, unsigned long delay, unsigned long dur, std::function<void(Sprite*, double)> f) {
    auto* e = new EngineEvent;
    e->kind = k;
    e->start = now() + delay;
    e->end = e->start + dur;
    e->step = std::move(f);
    s->timed_events->Push(e);
    return e;
}
// apply now when there is no delay, else queue
void at(Sprite* s, Kind k, unsigned long delay, std::function<void(Sprite*)> f) {
    if (!delay) { f(s); return; }
    add_event(s, k, delay, 0, [f](Sprite* sp, double t) { if (t >= 1.0) f(sp); });
}
}

// ------------------------------------------------------------------------------ Sprite
bool Sprite::ClicksEnabled = true;

Sprite::Sprite(Bitmap* b, unsigned long fl, Sprite* parent) : Group(nullptr, parent) {
    memset(reinterpret_cast<unsigned char*>(this) + offsetof(Sprite, x), 0, offsetof(Sprite, last) + 1 - offsetof(Sprite, x));
    st = new SpriteState;
    flags = (uint32_t)fl;
    scale_x = scale_y = 0x10000;
    click_events = new List(static_cast<List*>(nullptr));
    frame_events = new List(static_cast<List*>(nullptr));
    events84 = new List(static_cast<List*>(nullptr));
    timed_events = new List(static_cast<List*>(nullptr));
    sig_handlers = new List(static_cast<List*>(nullptr));
    if (b) { bmp = b; w = (float)b->w; h = (float)b->h; }
    if (WorldClass* wd = W()) wd->sprites->insert(this);
    else orphans().insert(this);
    if (parent) {
        if (!parent->children) parent->children = new List(static_cast<List*>(nullptr));
        parent->children->Push(this);
    }
}
Sprite::~Sprite() {
    if (WorldClass* wd = W()) {
        wd->sprites->erase(this);
        if (wd->ws && wd->ws->pressed == this) wd->ws->pressed = nullptr;
    }
    orphans().erase(this);
    if (auto* p = dynamic_cast<Sprite*>(owner)) if (p->children) p->children->RemoveFrom(this);
    for (List** l : {&click_events, &frame_events, &events84, &timed_events}) {
        if (*l) { (*l)->Clear(8); delete *l; *l = nullptr; }
    }
    if (sig_handlers) { delete sig_handlers; sig_handlers = nullptr; }
    if (st) {
        if (st->text_cache) destroy_bitmap(st->text_cache);
        if (st->anim_chain) legacy::free_bitmap_chain(st->anim_chain);
        delete st; st = nullptr;
    }
    free(name); name = nullptr;
}
int Sprite::Signal(SpriteSignal* s) {
    int r = 0;
    if (sig_handlers) sig_handlers->each([&](Group* g) { r |= static_cast<SpriteSigHand*>(g)->DoIt(this, s); });
    if (auto* p = dynamic_cast<Sprite*>(owner)) r |= p->Signal(s);
    return r;
}
int Sprite::Signal(Group::SpriteSignalType t) { SpriteSignal s(t, this, nullptr, nullptr); return Signal(&s); }
int Sprite::Message(SpriteSignal* s) {
    int r = 0;
    if (children) children->each([&](Group* g) { r |= g->Message(s); });
    return r;
}
int Sprite::Message(Group::SpriteSignalType t) { SpriteSignal s(t, nullptr, this, nullptr); return Message(&s); }
int Sprite::SpriteClick() {
    if (click_events) click_events->each([&](Group* g) { static_cast<EventO*>(g)->DoIt(this, 0); });
    if (signal_type) Signal((Group::SpriteSignalType)signal_type);
    return 1;
}
int Sprite::DoNextFrame() { return 1; }
int Sprite::DoCommand(int) { return 0; }
void Sprite::Reset() {}

void Sprite::SetCord(float nx, float ny, float nz, unsigned long delay) {
    at(this, K_MOVE, delay, [=](Sprite* s) { s->x = nx; s->y = ny; s->z = nz; });
}
void Sprite::SetSize(int nw, int nh) { w = (float)nw; h = (float)nh; st->sized = true; flags |= F_DIRTY; }
void Sprite::Enable(unsigned long delay) { at(this, K_ENABLE, delay, [](Sprite* s) { s->flags &= ~F_DISABLED; }); }
void Sprite::Disable(unsigned long delay, bool) { at(this, K_ENABLE, delay, [](Sprite* s) { s->flags |= F_DISABLED; }); }
static void set_bmp_now(Sprite* s, Bitmap* b) {
    s->bmp = b;
    if (b && !s->st->sized) { s->w = (float)b->w; s->h = (float)b->h; }
    s->flags |= F_DIRTY;
}
void Sprite::SetBmp(Bitmap* b, unsigned long delay) { at(this, K_MISC, delay, [b](Sprite* s) { set_bmp_now(s, b); }); }
void Sprite::ChangeBmp(Bitmap* b, unsigned long delay) { SetBmp(b, delay); }
void Sprite::SetBmpCord(Bitmap* b, int nx, int ny, int nz, unsigned long delay) {
    at(this, K_MISC, delay, [=](Sprite* s) { set_bmp_now(s, b); s->x = (float)nx; s->y = (float)ny; s->z = (float)nz; });
}
void Sprite::AssignString(String* s, unsigned long delay) {
    at(this, K_MISC, delay, [s](Sprite* sp) { sp->str = s; sp->flags |= F_DIRTY; });
}
void Sprite::ChangeString(char const* t, unsigned long delay) {
    std::string text = t ? t : "";
    at(this, K_MISC, delay, [text](Sprite* sp) { if (sp->str) sp->str->set_text(text.c_str()); sp->flags |= F_DIRTY; });
}
void Sprite::AssignName(char* n) { free(name); name = n ? strdup(n) : nullptr; }
void Sprite::ProcessAssignedString() {}

void Sprite::Move(float x0, float y0, float z0, float x1, float y1, float z1, unsigned long dur, unsigned long delay, unsigned char smooth) {
    if (!dur && !delay) { x = x1; y = y1; z = z1; return; }
    add_event(this, K_MOVE, delay, dur, [=](Sprite* s, double t) {
        double e = ease(t, smooth);
        s->x = (float)(x0 + (x1 - x0) * e); s->y = (float)(y0 + (y1 - y0) * e); s->z = (float)(z0 + (z1 - z0) * e);
    });
}
void Sprite::Move(int x0, int y0, int z0, int x1, int y1, int z1, unsigned long dur, unsigned long delay, unsigned char smooth) {
    Move((float)x0, (float)y0, (float)z0, (float)x1, (float)y1, (float)z1, dur, delay, smooth);
}
void Sprite::MoveTo(float x1, float y1, float z1, unsigned long dur, unsigned long delay, unsigned char smooth) {
    // from wherever the sprite is when the move starts
    auto from = std::make_shared<std::array<float, 3>>();
    auto started = std::make_shared<bool>(false);
    add_event(this, K_MOVE, delay, dur, [=](Sprite* s, double t) {
        if (!*started) { *started = true; *from = {s->x, s->y, s->z}; }
        double e = ease(t, smooth);
        s->x = (float)((*from)[0] + (x1 - (*from)[0]) * e);
        s->y = (float)((*from)[1] + (y1 - (*from)[1]) * e);
        s->z = (float)((*from)[2] + (z1 - (*from)[2]) * e);
    });
}
void Sprite::MoveTo(int x1, int y1, int z1, unsigned long dur, unsigned long delay, unsigned char smooth) {
    MoveTo((float)x1, (float)y1, (float)z1, dur, delay, smooth);
}
void Sprite::MoveChangeBmp(Bitmap* b, float x0, float y0, float z0, float x1, float y1, float z1, unsigned long dur, unsigned long delay, unsigned char smooth) {
    SetBmp(b, delay);
    Move(x0, y0, z0, x1, y1, z1, dur, delay, smooth);
}
// cubic Bézier: start, start control, end, end control (xyz each)
void Sprite::Curve(int sx, int sy, int sz, int cx0, int cy0, int cz0, int ex, int ey, int ez, int cx1, int cy1, int cz1,
                   unsigned long dur, unsigned long delay, unsigned char smooth) {
    add_event(this, K_MOVE, delay, dur, [=](Sprite* s, double t) {
        double u = ease(t, smooth), v = 1 - u;
        auto bz = [&](double p0, double p1, double p2, double p3) { return v * v * v * p0 + 3 * v * v * u * p1 + 3 * v * u * u * p2 + u * u * u * p3; };
        s->x = (float)bz(sx, cx0, cx1, ex); s->y = (float)bz(sy, cy0, cy1, ey); s->z = (float)bz(sz, cz0, cz1, ez);
    });
}
static void fade(Sprite* sp, unsigned long delay, unsigned long dur, int from, int to) {
    if (!dur) { at(sp, K_FADE, delay, [to](Sprite* s) { s->transparency = (unsigned char)std::clamp(to, 0, 255); s->flags |= F_DIRTY; }); return; }
    add_event(sp, K_FADE, delay, dur, [=](Sprite* s, double t) {
        s->transparency = (unsigned char)std::clamp((int)std::lround(from + (to - from) * t), 0, 255);
        s->flags |= F_DIRTY;
    });
}
void Sprite::FadeIn(unsigned long delay, unsigned long dur, int from, int to) {
    fade(this, delay, dur, from < 0 ? 255 : from, to < 0 ? 0 : to);
}
void Sprite::FadeOut(unsigned long delay, unsigned long dur, int from, int to) {
    fade(this, delay, dur, from < 0 ? 0 : from, to < 0 ? 255 : to);
}
void Sprite::SetAlpha(int t, unsigned long delay) { fade(this, delay, 0, t, t); }

// Plays frames first..last of a Bitmap chain, `loops` times (65535+ = forever), at the world
// frame rate times `speed`.
void Sprite::RunAnim(Bitmap* anim, int first, int last_f, int loops, unsigned long delay, float speed, unsigned long, int) {
    if (!anim) return;
    while (anim->prev) anim = anim->prev;
    std::vector<Bitmap*> fr;
    for (Bitmap* b = anim; b; b = b->next) fr.push_back(b);
    int n = (int)fr.size();
    first = std::clamp(first, 0, n - 1);
    last_f = std::clamp(last_f, first, n - 1);
    int count = last_f - first + 1;
    WorldClass* wd = W();
    double ms_per = 1000.0 / std::max(1, wd ? wd->fps : 30) / (speed > 0 ? speed : 1.0f);
    bool forever = loops <= 0 || loops >= 65535;
    unsigned long dur = forever ? (kForever - now() - delay - 1) : (unsigned long)(count * std::max(1, loops) * ms_per);
    auto* e = add_event(this, K_ANIM, delay, dur, nullptr);
    uint32_t t0 = e->start;
    e->step = [=](Sprite* s, double) {
        long k = (long)((now() - t0) / ms_per);
        if (!forever && k >= (long)count * std::max(1, loops)) k = (long)count * std::max(1, loops) - 1;
        set_bmp_now(s, fr[first + (int)(k % count)]);
    };
    if (forever) e->end = kForever;
}
void Sprite::RunDiskAnimThenDelete(char const* nm, unsigned long delay, float speed, unsigned long loops, bool) {
    Bitmap* chain = legacy::load_bitmap_chain(nm, true, 0);
    if (!chain) { flags |= F_DELETE; return; }
    if (st->anim_chain) legacy::free_bitmap_chain(st->anim_chain);
    st->anim_chain = chain;
    RunAnim(chain, 0, 999999, loops > 0 && loops < 65535 ? (int)loops : 1, delay, speed, 0, -1);
    DeleteAfterLast(0);
}
unsigned long Sprite::LastEventTime() {
    uint32_t t = now();
    timed_events->each([&](Group* g) { auto* e = static_cast<EventO*>(g); if (e->end != kForever) t = std::max(t, e->end); });
    return t;
}
void Sprite::DeleteAfterLast(unsigned long delay) {
    unsigned long when = LastEventTime() - now() + delay;
    add_event(this, K_MISC, when, 0, [](Sprite* s, double t) { if (t >= 1.0) s->flags |= F_DELETE; });
}
static void kill_kind(Sprite* s, unsigned char k) {
    s->timed_events->each([&](Group* g) {
        auto* e = static_cast<EventO*>(g);
        if (dynamic_cast<EngineEvent*>(e) && e->kind == k) { s->timed_events->RemoveFrom(e); delete e; }
    });
}
void Sprite::KillAllMovementEvents() { kill_kind(this, K_MOVE); }
void Sprite::KillAllEnableEvents() { kill_kind(this, K_ENABLE); }
void Sprite::AddEvent(EventO* e) { if (e) timed_events->Push(e); }
void Sprite::PlaySound(char* wav, int fl, unsigned long delay) {
    std::string n = wav ? wav : "";
    at(this, K_MISC, delay, [n, fl](Sprite*) { PlayPreWave(const_cast<char*>(n.c_str()), (unsigned short)fl, true, 255, 1000, 127); });
}
void Sprite::ColorIt(int a, int b, int c, int d, int e, int f, int g, int h_, int i, unsigned long delay) {
    std::array<int, 9> m{a, b, c, d, e, f, g, h_, i};
    at(this, K_MISC, delay, [m](Sprite* s) { std::copy(m.begin(), m.end(), s->st->matrix); s->st->colored = true; s->flags |= F_DIRTY; });
}
void Sprite::ColoredOff(unsigned long delay) { at(this, K_MISC, delay, [](Sprite* s) { s->st->colored = false; s->flags |= F_DIRTY; }); }
void Sprite::SetRGB(int r, int g, int b, unsigned long delay) {
    at(this, K_MISC, delay, [=](Sprite* s) { s->st->rgb[0] = r; s->st->rgb[1] = g; s->st->rgb[2] = b; s->flags |= F_DIRTY; });
}
void Sprite::SetTextRGB(int r, int g, int b, unsigned long delay) {
    at(this, K_MISC, delay, [=](Sprite* s) { s->st->text_rgb[0] = r; s->st->text_rgb[1] = g; s->st->text_rgb[2] = b; s->st->text_rgb_set = true; s->flags |= F_DIRTY; });
}
void Sprite::FadeRGB(int r0, int g0, int b0, int r1, int g1, int b1, unsigned long dur, unsigned long delay, unsigned char smooth) {
    add_event(this, K_FADE, delay, dur, [=](Sprite* s, double t) {
        double e = ease(t, smooth);
        s->st->rgb[0] = (int)(r0 + (r1 - r0) * e); s->st->rgb[1] = (int)(g0 + (g1 - g0) * e); s->st->rgb[2] = (int)(b0 + (b1 - b0) * e);
        s->flags |= F_DIRTY;
    });
}
void Sprite::CreateTextDropShadow(float dx, float dy, unsigned long delay, int r, int g, int b) {
    at(this, K_MISC, delay, [=](Sprite* s) {
        s->st->shadow = true; s->st->shadow_dx = dx; s->st->shadow_dy = dy;
        s->st->shadow_rgb[0] = r; s->st->shadow_rgb[1] = g; s->st->shadow_rgb[2] = b;
    });
}
void Sprite::EffectOutline(int, unsigned char, unsigned char, unsigned char, bool) {}
static void scale_anim(Sprite* sp, unsigned long delay, unsigned long dur, double s0x, double s0y, double s1x, double s1y, int smooth) {
    if (!dur) { at(sp, K_MISC, delay, [=](Sprite* s) { s->scale_x = (int)(s1x * 65536); s->scale_y = (int)(s1y * 65536); }); return; }
    add_event(sp, K_MISC, delay, dur, [=](Sprite* s, double t) {
        double e = ease(t, smooth);
        s->scale_x = (int)((s0x + (s1x - s0x) * e) * 65536); s->scale_y = (int)((s0y + (s1y - s0y) * e) * 65536);
        s->flags |= F_DIRTY;
    });
}
void Sprite::EffectImplode(int, int, int dur, unsigned long delay) { scale_anim(this, delay, dur, 1, 1, 0.01, 0.01, 0); fade(this, delay, dur, 0, 255); }
void Sprite::EffectExplode(int, int, int dur, unsigned long delay) { scale_anim(this, delay, dur, 1, 1, 2, 2, 0); fade(this, delay, dur, 0, 255); }
void Sprite::EffectBalloon(int, unsigned long dur, unsigned long delay) {
    add_event(this, K_MISC, delay, dur, [](Sprite* s, double t) {
        double k = 1 + 0.25 * std::sin(t * M_PI);
        s->scale_x = s->scale_y = (int)(k * 65536);
        if (t >= 1) s->scale_x = s->scale_y = 0x10000;
    });
}
void Sprite::EffectSlideFadeIn(int dir, int dist, int dur, int delay) {
    float x1 = x, y1 = y;
    float x0 = x1 + (dir == 0 ? -dist : dir == 1 ? dist : 0), y0 = y1 + (dir == 2 ? -dist : dir == 3 ? dist : 0);
    Move(x0, y0, z, x1, y1, z, dur, delay, 2);
    fade(this, delay, dur, 255, 0);
}
void Sprite::EffectSquishChangeBmp(Bitmap* b, int, int, unsigned long dur, unsigned long delay, int, int, bool) {
    SetBmp(b, delay + dur / 2);
}
void Sprite::Scale(float from, float to, unsigned long dur, unsigned long delay, unsigned char smooth) {
    scale_anim(this, delay, dur, from, from, to, to, smooth);
}
void Sprite::ScaleXY(float fx, float fy, float tx, float ty, unsigned long dur, unsigned long delay, unsigned char smooth) {
    scale_anim(this, delay, dur, fx, fy, tx, ty, smooth);
}
void Sprite::ScaleXY(float sx, float sy, unsigned long delay) { scale_anim(this, delay, 0, sx, sy, sx, sy, 0); }
void Sprite::HardScale(int nw, int nh) {
    if (bmp && nw > 0 && nh > 0) { bmp->Scale(nw, nh, 0); set_bmp_now(this, bmp); }
}
void Sprite::Rotate(float, float to, RotationDirection, unsigned long, unsigned long, unsigned char) { angle = to; }
void Sprite::SetHQScaling(bool on) { st->hq = on; }
// sprites whose boxes overlap this one (NULL-terminated, static storage)
Sprite** Sprite::PixelCollide(float, int) {
    static std::vector<Sprite*> out;
    out.clear();
    if (WorldClass* wd = W())
        for (Sprite* s : *wd->sprites)
            if (s != this && !(s->flags & (F_DISABLED | F_DELETE)) && s->x < x + w && x < s->x + s->w && s->y < y + h && y < s->y + s->h)
                out.push_back(s);
    out.push_back(nullptr);
    return out.data();
}
static void clicks_all(Sprite* s, bool on) {
    if (on) s->flags |= F_CLICK; else s->flags &= ~F_CLICK;
    if (s->children) s->children->each([&](Group* g) { if (auto* c = dynamic_cast<Sprite*>(g)) clicks_all(c, on); });
}
void Sprite::EnableAllClicks(unsigned long delay) { at(this, K_MISC, delay, [](Sprite* s) { clicks_all(s, true); }); }
void Sprite::DisableAllClicks(unsigned long delay) { at(this, K_MISC, delay, [](Sprite* s) { clicks_all(s, false); }); }
Sprite* Sprite::Copy() {
    auto* c = new Sprite(bmp, flags, nullptr);
    c->x = x; c->y = y; c->z = z; c->w = w; c->h = h; c->scale_x = scale_x; c->scale_y = scale_y;
    c->transparency = transparency; c->str = str ? str->Copy() : nullptr;
    *c->st = SpriteState{};
    c->st->sized = st->sized;
    return c;
}
void Sprite::DoChain(List*, int) {}
void Sprite::ChangeClipping(int, int, int, int, int, int, int, int, unsigned long, unsigned long) {}

// ------------------------------------------------------------------------------ drawing
namespace {
inline uint32_t* px32(BITMAP* b, int y) { return reinterpret_cast<uint32_t*>(b->line[y]); }

// Draws a 16/32-bit source onto the 32-bit target: scaled to dw x dh, with transparency
// (0 opaque..255 invisible), colour matrix and colour offsets.
void blit_sprite(BITMAP* src, BITMAP* dst, int dx, int dy, int dw, int dh, int transparency, const SpriteState* st,
                 const int* rgb_override, bool opaque) {
    if (!src || !dst || dw <= 0 || dh <= 0 || transparency >= 255) return;
    int sd = src->vtable->color_depth;
    int alpha = 255 - transparency;
    const int* rgb = rgb_override ? rgb_override : (st ? st->rgb : nullptr);
    bool tint = rgb && (rgb[0] || rgb[1] || rgb[2]);
    bool mat = st && st->colored;
    for (int j = 0; j < dh; j++) {
        int Y = dy + j;
        if (Y < dst->ct || Y >= dst->cb) continue;
        int syy = (int)((long)j * src->h / dh);
        uint32_t* out = px32(dst, Y);
        for (int i = 0; i < dw; i++) {
            int X = dx + i;
            if (X < dst->cl || X >= dst->cr) continue;
            int sxx = (int)((long)i * src->w / dw);
            int r, g, b;
            if (sd == 16) {
                int c = reinterpret_cast<uint16_t*>(src->line[syy])[sxx];
                if (!opaque && c == kKey16) continue;
                r = getr_depth(16, c); g = getg_depth(16, c); b = getb_depth(16, c);
            } else {
                uint32_t c = px32(src, syy)[sxx];
                if (!opaque && (c & 0xffffff) == kKey) continue;
                r = c >> 16 & 255; g = c >> 8 & 255; b = c & 255;
            }
            if (mat) {
                const int* m = st->matrix;
                int nr = (r * m[0] + g * m[1] + b * m[2]) / 255, ng = (r * m[3] + g * m[4] + b * m[5]) / 255, nb = (r * m[6] + g * m[7] + b * m[8]) / 255;
                r = std::min(255, nr); g = std::min(255, ng); b = std::min(255, nb);
            }
            if (tint) { r = r * (255 + rgb[0]) / 255; g = g * (255 + rgb[1]) / 255; b = b * (255 + rgb[2]) / 255; r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255); }
            if (alpha < 255) {
                uint32_t o = out[X];
                r = ((o >> 16 & 255) * (255 - alpha) + r * alpha) / 255;
                g = ((o >> 8 & 255) * (255 - alpha) + g * alpha) / 255;
                b = ((o & 255) * (255 - alpha) + b * alpha) / 255;
            }
            out[X] = (uint32_t)(r << 16 | g << 8 | b);
        }
    }
}

void draw_sprite_tree(Sprite* s, BITMAP* dst, float ox, float oy);

std::string string_key(const String* s, const SpriteState* st, int w, int h) {
    char buf[96];
    snprintf(buf, sizeof buf, "|%d|%d|%d|%d|%d|%d|%d|%d|%d|", s->size, s->just, s->r, s->g, s->b, s->style, w, h,
             st->text_rgb_set ? st->text_rgb[0] * 65536 + st->text_rgb[1] * 256 + st->text_rgb[2] : 0);
    return std::string(s->text ? s->text : "") + buf + s->font;
}
}

// Base drawing: the bitmap, then the text inside the sprite's box. Children are drawn by the world.
int Sprite::DoDraw() {
    WorldClass* wd = W();
    BITMAP* dst = wd && wd->ws && wd->ws->draw_target && wd->ws->draw_target->al ? wd->ws->draw_target->al : legacy::target_bitmap();
    draw_sprite_tree(this, dst, 0, 0);
    return 1;
}

namespace {
float g_ox, g_oy;                                // parent offset while drawing children

void draw_self(Sprite* s, BITMAP* dst, float ox, float oy) {
    if (s->flags & (F_DISABLED | F_DELETE) || s->transparency >= 255) return;
    double sx = s->scale_x ? s->scale_x / 65536.0 : 1.0, sy = s->scale_y ? s->scale_y / 65536.0 : 1.0;
    int dw = (int)std::lround(s->w * sx), dh = (int)std::lround(s->h * sy);
    float px = s->x + ox, py = s->y + oy;
    if (s->flags & F_CENTER_X) px -= dw / 2.0f;
    if (s->flags & F_CENTER_Y) py -= dh / 2.0f;
    if (s->bmp && s->bmp->al) {
        int bw = (int)std::lround(s->bmp->w * sx), bh = (int)std::lround(s->bmp->h * sy);
        blit_sprite(s->bmp->al, dst, (int)std::lround(px), (int)std::lround(py), bw, bh, s->transparency, s->st, nullptr,
                    s->bmp->flags & 0x02);
    }
    if (s->str && s->str->text && *s->str->text) {
        int bw = (int)s->w, bh = (int)s->h;
        std::string key = string_key(s->str, s->st, bw, bh);
        if (!s->st->text_cache || s->st->cache_key != key) {
            if (s->st->text_cache) destroy_bitmap(s->st->text_cache);
            String tmp = *s->str;
            if (s->st->text_rgb_set) { tmp.r = s->st->text_rgb[0]; tmp.g = s->st->text_rgb[1]; tmp.b = s->st->text_rgb[2]; }
            s->st->text_cache = legacy::render_string(&tmp, bw, bh);
            s->st->cache_key = key;
        }
        BITMAP* t = s->st->text_cache;
        if (t) {
            int tw = (int)std::lround(t->w * sx), th = (int)std::lround(t->h * sy);
            float tx = s->x + ox, ty = s->y + oy;
            if (s->flags & F_CENTER_X) tx -= tw / 2.0f;
            if (s->flags & F_CENTER_Y) ty -= th / 2.0f;
            if (s->st->shadow) {
                int dark[3] = {s->st->shadow_rgb[0], s->st->shadow_rgb[1], s->st->shadow_rgb[2]};
                blit_sprite(t, dst, (int)(tx + s->st->shadow_dx), (int)(ty + s->st->shadow_dy), tw, th, s->transparency, nullptr, dark, false);
            }
            blit_sprite(t, dst, (int)std::lround(tx), (int)std::lround(ty), tw, th, s->transparency, nullptr, nullptr, false);
        }
    }
}

void draw_sprite_tree(Sprite* s, BITMAP* dst, float ox, float oy) {
    draw_self(s, dst, ox, oy);
    if (!s->children || (s->flags & F_DISABLED)) return;
    std::vector<Sprite*> kids;
    s->children->each([&](Group* g) { if (auto* c = dynamic_cast<Sprite*>(g)) kids.push_back(c); });
    std::stable_sort(kids.begin(), kids.end(), [](Sprite* a, Sprite* b) { return a->z < b->z; });
    for (Sprite* c : kids) {
        float sox = g_ox, soy = g_oy;
        g_ox = ox + s->x; g_oy = oy + s->y;
        c->DoDraw();                              // virtual: games override it
        g_ox = sox; g_oy = soy;
    }
}
}

// ------------------------------------------------------------------------------ WorldClass
WorldClass::WorldClass() : Group(nullptr, nullptr) {
    memset(reinterpret_cast<unsigned char*>(this) + offsetof(WorldClass, pad0c), 0, sizeof(WorldClass) - offsetof(WorldClass, pad0c));
    legacy::video_init();
    sprites = new std::set<Sprite*>;
    ws = new WorldState;
    ws->start = legacy::ticks();
    fps = menv_int("LEGACY_FPS", 30);
    convert = 1;
    // sprites made before any world existed belong to this one
    for (Sprite* s : orphans()) sprites->insert(s);
    orphans().clear();
}
WorldClass::~WorldClass() {
    // the game deletes the world at shutdown; sprites still registered are freed with it
    if (sprites) {
        std::vector<Sprite*> v(sprites->begin(), sprites->end());
        WorldClass* saved = world_slot();
        world_slot() = this;
        for (Sprite* s : v) if (sprites->count(s)) delete s;
        world_slot() = saved;
        delete sprites; sprites = nullptr;
    }
    if (ws) {
        if (ws->back_owned) legacy::free_bitmap_chain(ws->back);
        delete ws; ws = nullptr;
    }
}
void WorldClass::Init(int, int) { clock = 0; ws->start = legacy::ticks(); }
void WorldClass::Shutdown() {}
DOSLinuxWorld::DOSLinuxWorld() {}
DOSLinuxWorld::~DOSLinuxWorld() {}

Bitmap* WorldClass::LoadBmp(char const* nm, int lang, float scale, int, int max_frames, bool, bool, Bitmap* into) {
    Bitmap* b = legacy::load_bitmap_chain(nm, lang != 0, max_frames >= 999999 ? 0 : max_frames);
    if (b && scale > 0 && std::fabs(scale - 1.0f) > 0.001f)
        for (Bitmap* f = b; f; f = f->next) f->Scale((int)(f->w * scale), (int)(f->h * scale), 0);
    if (into && b) {
        into->resize(b->w, b->h);
        blit(b->al, into->al, 0, 0, 0, 0, b->w, b->h);
        legacy::free_bitmap_chain(b);
        return into;
    }
    if (b) ws->loaded.insert(b);
    return b;
}
Bitmap* WorldClass::LoadAdBmp(char const* nm, int lang, float scale, int fmt, int max_frames, bool b6) {
    return LoadBmp(nm, lang, scale, fmt, max_frames, b6, false, nullptr);
}
void WorldClass::DeleteBmp(Bitmap* b, unsigned char) {
    if (!b) return;
    while (b->prev) b = b->prev;
    // sprites still showing a frame of it: detach (games normally kill them first)
    std::set<Bitmap*> frames;
    for (Bitmap* f = b; f; f = f->next) frames.insert(f);
    for (Sprite* s : *sprites) if (frames.count(s->bmp)) s->bmp = nullptr;
    if (ws->back && frames.count(ws->back)) { ws->back = nullptr; ws->back_owned = false; }
    ws->loaded.erase(b);
    legacy::free_bitmap_chain(b);
}
Bitmap* WorldClass::GetFrame(Bitmap* b, int n) {
    if (!b) return nullptr;
    while (b->prev) b = b->prev;
    for (int i = 0; i < n && b->next; i++) b = b->next;
    return b;
}
int WorldClass::AnimationFrames(Bitmap* b) {
    if (!b) return 0;
    while (b->prev) b = b->prev;
    int n = 0;
    for (; b; b = b->next) n++;
    return n;
}
int WorldClass::FrameNumber(Bitmap* b) { return b ? b->frame_index : 0; }
int WorldClass::FrameCount(char const* nm) {
    Bitmap* b = legacy::load_bitmap_chain(nm, true, 0);
    int n = AnimationFrames(b);
    legacy::free_bitmap_chain(b);
    return n;
}

void WorldClass::SetBack(char const* nm, int lang) {
    if (ws->back_owned) legacy::free_bitmap_chain(ws->back);
    ws->back = legacy::load_bitmap_chain(nm, lang != 0, 1);
    ws->back_owned = ws->back != nullptr;
    ws->back_px.clear();
    if (!ws->back) LOG("SetBack: missing %s", nm ? nm : "?");
}
void WorldClass::SetBack(Bitmap* b, int) {
    if (ws->back_owned) legacy::free_bitmap_chain(ws->back);
    ws->back = b; ws->back_owned = false;
    ws->back_px.clear();
}
void WorldClass::SetBack(short vb) {
    if (ws->back_owned) legacy::free_bitmap_chain(ws->back);
    ws->back = nullptr; ws->back_owned = false;
    BITMAP* src = legacy::vb_bitmap(vb);
    ws->back_px.assign(reinterpret_cast<uint32_t*>(src->line[0]), reinterpret_cast<uint32_t*>(src->line[0]) + SW * SH);
}
void WorldClass::RefreshArea(int, int, int, int) {}
void WorldClass::ClearScreen() {
    if (ws->back_owned) legacy::free_bitmap_chain(ws->back);
    ws->back = nullptr; ws->back_owned = false; ws->back_px.clear();
    clear_bitmap(screen);
    legacy::touched_target();
}
void WorldClass::SpriteDrawTarget(Bitmap* b) { ws->draw_target = b; }
void WorldClass::ResetTimers() { ws->start = legacy::ticks(); clock = 0; }

// the background, then every top-level sprite (children are drawn with their parents) in z order
void WorldClass::render() {
    BITMAP* dst = ws->draw_target && ws->draw_target->al ? ws->draw_target->al : screen;
    if (dst == screen) {
        if (!ws->back_px.empty()) memcpy(screen->line[0], ws->back_px.data(), (size_t)SW * SH * 4);
        else if (ws->back && ws->back->al) {
            clear_bitmap(screen);
            ws->back->draw_to(screen, 0, 0, ws->back->w, ws->back->h, 0, 0, false);
        } else clear_bitmap(screen);
    }
    std::set<Sprite*> kids;
    for (Sprite* s : *sprites) if (s->children) s->children->each([&](Group* g) { kids.insert(static_cast<Sprite*>(g)); });
    std::vector<Sprite*> top;
    for (Sprite* s : *sprites) if (!kids.count(s)) top.push_back(s);
    std::stable_sort(top.begin(), top.end(), [](Sprite* a, Sprite* b) { return a->z < b->z; });
    for (Sprite* s : top) {
        g_ox = g_oy = 0;
        s->DoDraw();                              // virtual
    }
    legacy::touched_target();
}

static bool hit(Sprite* s, int tx, int ty, float ox, float oy) {
    double sx = s->scale_x ? s->scale_x / 65536.0 : 1.0, sy = s->scale_y ? s->scale_y / 65536.0 : 1.0;
    double w = s->w * sx, h = s->h * sy, x = s->x + ox, y = s->y + oy;
    if (s->flags & F_CENTER_X) x -= w / 2;
    if (s->flags & F_CENTER_Y) y -= h / 2;
    return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

void WorldClass::frame() {
    clock = legacy::ticks() - ws->start;
    std::vector<Sprite*> all(sprites->begin(), sprites->end());
    // events: timed queue (removed when finished), per-frame list
    for (Sprite* s : all) {
        if (!sprites->count(s)) continue;
        if (s->timed_events) {
            std::vector<EventO*> evs;
            s->timed_events->each([&](Group* g) { evs.push_back(static_cast<EventO*>(g)); });
            for (EventO* e : evs) {
                if (!sprites->count(s) || !s->timed_events || !s->timed_events->Contains(e)) continue;
                if (clock < e->start) continue;
                e->DoIt(s, 0);
                if (!sprites->count(s) || !s->timed_events) break;
                auto* ee = dynamic_cast<EngineEvent*>(e);
                bool finished = ee ? ee->done : (e->end != kForever && clock >= e->end);
                if (finished && s->timed_events->Contains(e)) { s->timed_events->RemoveFrom(e); delete e; }
            }
        }
        if (sprites->count(s) && s->frame_events) {
            std::vector<EventO*> evs;
            s->frame_events->each([&](Group* g) { evs.push_back(static_cast<EventO*>(g)); });
            for (EventO* e : evs) {
                if (!sprites->count(s) || !s->frame_events || !s->frame_events->Contains(e)) continue;
                if (clock >= e->start) e->DoIt(s, 0);
            }
        }
        if (sprites->count(s)) s->DoNextFrame();
    }
    // reap sprites flagged for deletion
    for (Sprite* s : std::vector<Sprite*>(sprites->begin(), sprites->end()))
        if (sprites->count(s) && (s->flags & F_DELETE)) delete s;
    // touches: the topmost enabled, clickable sprite under the finger
    for (const auto& t : legacy::take_touches()) {
        if (!t.down) {
            if (ws->pressed && sprites->count(ws->pressed) && ws->pressed->click_events)
                ws->pressed->click_events->each([&](Group* g) { static_cast<EventO*>(g)->DoIt(ws->pressed, 2); });
            ws->pressed = nullptr;
            continue;
        }
        if (!Sprite::ClicksEnabled || frozen) continue;
        Sprite* best = nullptr;
        for (Sprite* s : *sprites) {
            if ((s->flags & (F_DISABLED | F_DELETE)) || !(s->flags & F_CLICK)) continue;
            float ox = 0, oy = 0;
            if (auto* p = dynamic_cast<Sprite*>(s->owner)) { ox = p->x; oy = p->y; if (p->flags & F_DISABLED) continue; }
            if (hit(s, t.x, t.y, ox, oy) && (!best || s->z >= best->z)) best = s;
        }
        if (best) { ws->pressed = best; best->SpriteClick(); }
    }
    render();
    ws->last_frame = legacy::ticks();
}

void WorldClass::NextFrame(unsigned char, unsigned char, bool, bool, bool, bool) { frame(); legacy::pump(); }
void WorldClass::WaitOnFrame(unsigned long ms, bool) {
    Uint32 due = ws->last_frame + ms;
    while (legacy::ticks() < due && !legacy::quitting()) legacy::sleep_ms(1);
}
void WorldClass::ShowAll() { render(); legacy::pump(); }
void WorldClass::PlayMovie(int frames, unsigned long f, void (*cb)(unsigned char), unsigned char arg, bool, bool, bool, bool) {
    int rate = f ? (int)f : (fps > 0 ? fps : 30);
    for (int i = 0; i < std::max(1, frames); i++) {
        Uint32 due = ws->last_frame + 1000 / rate;
        while (legacy::ticks() < due && !legacy::quitting()) legacy::sleep_ms(1);
        frame();
        if (cb) cb(arg);
        if (legacy::quitting()) break;
    }
}
// a one-shot .dlt animation over the current screen, with a sound; blocks until it ends
void WorldClass::PlayDeltaAnim(char* nm, int x, int y, unsigned long f, unsigned long, char* sound, unsigned short sflags, unsigned char, bool) {
    Anim* a = legacy::anim_load(nm, true);
    if (!a) return;
    if (sound && *sound) PlayPreWave(sound, sflags, true, 255, 1000, 127);
    std::vector<uint32_t> saved(reinterpret_cast<uint32_t*>(screen->line[0]), reinterpret_cast<uint32_t*>(screen->line[0]) + SW * SH);
    int rate = f ? (int)f : 30;
    for (int i = 0; i < (int)a->frames.size() && !legacy::quitting(); i++) {
        legacy::anim_seek(a, i);
        memcpy(screen->line[0], saved.data(), saved.size() * 4);
        BITMAP* fb = legacy::al_wrap32(a->canvas.data(), a->w, a->h);
        masked_blit(fb, screen, 0, 0, x, y, a->w, a->h);
        destroy_bitmap(fb);
        legacy::touched_target();
        legacy::sleep_ms(1000 / rate);
    }
    delete a;
}
// colour-matrix copy of a whole chain (or one frame)
Bitmap* WorldClass::CopyCompBitmap(Bitmap* src, int m0, int m1, int m2, int m3, int m4, int m5, int m6, int m7, int m8, int frames, bool) {
    if (!src) return nullptr;
    int m[9] = {m0, m1, m2, m3, m4, m5, m6, m7, m8};
    Bitmap *first = nullptr, *prev = nullptr;
    int n = 0;
    for (Bitmap* s = src; s && n < std::max(1, frames); s = s->next, n++) {
        auto* b = new Bitmap(s->w, s->h, 0, 16);
        for (int yy = 0; yy < s->h; yy++)
            for (int xx = 0; xx < s->w; xx++) {
                int c = s->al->vtable->getpixel(s->al, xx, yy);
                if (c == kKey16 && s->al->vtable->color_depth == 16) continue;
                int d = s->al->vtable->color_depth;
                int r = getr_depth(d, c), g = getg_depth(d, c), bl = getb_depth(d, c);
                int nr = std::clamp((r * m[0] + g * m[1] + bl * m[2]) / 255, 0, 255);
                int ng = std::clamp((r * m[3] + g * m[4] + bl * m[5]) / 255, 0, 255);
                int nb = std::clamp((r * m[6] + g * m[7] + bl * m[8]) / 255, 0, 255);
                int v = makecol_depth(16, nr, ng, nb);
                b->al->vtable->putpixel(b->al, xx, yy, v == kKey16 ? v ^ 0x20 : v);
            }
        b->frame_index = n;
        b->prev = prev;
        if (prev) prev->next = b; else first = b;
        prev = b;
    }
    return first;
}

// ------------------------------------------------------------------------------ net sprites
NetSprite::NetSprite(Bitmap* b, unsigned short fl, unsigned char, unsigned short) : Sprite(b, fl, nullptr) {
    memset(payload, 0, sizeof(NetSprite) - offsetof(NetSprite, payload));
    out = payload;                                // games write outgoing packets here; never sent
}
NetSprite::~NetSprite() {}
void NetSprite::SendPacket(unsigned char) {}
NetSpriteLock::NetSpriteLock(Bitmap* b, unsigned short fl, unsigned char id, unsigned short n) : NetSprite(b, fl, id, n) {}
NetSpriteLock::~NetSpriteLock() {}
void NetSpriteLock::SendPacket(unsigned char) {}
void NetSpriteLock::SendPacketLock(unsigned char, unsigned long) {}
void NetSpriteLock::SendAck(unsigned char) {}
void NetSpriteLock::SendNack(unsigned char) {}
