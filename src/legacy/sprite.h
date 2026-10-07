// The loader's sprite engine (Group, List, EventO, Sprite, WorldClass...), ABI-compatible with
// the legacy games: same class names and method signatures (mangled names), same vtable slot
// order, object sizes and the field offsets games use inline. Reference:
// docs/reference/sprite-engine.md.
#pragma once
#include "bitmap.h"
#include <cstddef>
#include <cstdint>
#include <set>

class List;
class ListObj;
class Sprite;
class SpriteSignal;
class EventO;
class Gash;
class SObj;

class Group {
public:
    enum SpriteSignalType : int {};
    Group(List*, Group*);
    virtual ~Group();
    virtual int Signal(SpriteSignal*);
    virtual int Signal(SpriteSignalType);
    virtual int Message(SpriteSignal*);
    virtual int Message(SpriteSignalType);
    List* children;                              // +0x04 child list (NULL until the game makes one)
    Group* owner;                                // +0x08
};
static_assert(sizeof(Group) == 0xc, "Group");

// List nodes (the C list API games also use): +0xc data, +0x10 prev (0 = header), +0x14 next
// (0 = tail sentinel). A header's data field points at its tail; the tail's data and prev
// point back at the header when the list is empty.
class ListObj : public Group {
public:
    ListObj();
    virtual ~ListObj();
    void* data;                                  // +0x0c
    ListObj* prev;                               // +0x10
    ListObj* next;                               // +0x14
};
static_assert(sizeof(ListObj) == 0x18, "ListObj");
class ListObjHeader : public ListObj {
public:
    ListObjHeader();
    int extra;
};
static_assert(sizeof(ListObjHeader) == 0x1c, "ListObjHeader");

class List : public Group {
public:
    explicit List(List* src);                    // NULL: new owning list; else an iterator on src
    explicit List(ListObj* hdr);
    virtual ~List();
    void SetList(ListObj*);
    void Push(Group*);
    void LinkInto(Group*, int pos);
    void Clear(unsigned char flags);
    bool IsEmpty();
    int Count();
    bool Contains(Group*);
    bool Advance(int n);
    bool Backup(int n);
    Group* Pop();
    void RemoveFrom(Group*);
    void Remove(Group*);
    void UnLinkAndAdvance();
    void Dump();
    // engine helpers
    ListObj* header() const;
    template <class F> void each(F f);           // f(Group*) over a snapshot of the elements

    Group* cur;                                  // +0x0c current element
    ListObj* node;                               // +0x10 current node (the header for owners)
    unsigned char owns;                          // +0x14
};
static_assert(sizeof(List) == 0x18, "List");

template <class T> class ListT : public List {
public:
    explicit ListT(List* src);
    virtual ~ListT();
    bool Advance(int n);
    void UnLinkAndAdvance();
    void Set(List*);
};

class EventO : public Group {
public:
    EventO();
    virtual ~EventO();
    virtual int DoIt(Sprite*, int reason);
    virtual int GetType();
    uint32_t start;                              // +0x0c world time it becomes due
    uint32_t end;                                // +0x10 0xefffffff = forever
    unsigned char kind;                          // +0x14 (engine events: what they do)
};
static_assert(sizeof(EventO) == 0x18, "EventO");

class SpriteSignal : public Group {
public:
    SpriteSignal(Group::SpriteSignalType, Sprite*, Sprite*, Gash*);
    Group::SpriteSignalType type;                // +0x0c
    Sprite* from;                                // +0x10
    Sprite* to;                                  // +0x14
    Gash* gash;                                  // +0x18
};
static_assert(sizeof(SpriteSignal) == 0x1c, "SpriteSignal");

class SpriteSigHand : public Group {
public:
    SpriteSigHand();
    virtual int DoIt(Sprite*, SpriteSignal*) = 0;
};

struct SpriteState;

class Sprite : public Group {
public:
    enum RotationDirection : int {};
    Sprite(Bitmap* bmp, unsigned long flags, Sprite* parent);
    virtual ~Sprite();
    int Signal(SpriteSignal*) override;
    int Signal(Group::SpriteSignalType) override;
    int Message(SpriteSignal*) override;
    int Message(Group::SpriteSignalType) override;
    virtual int SpriteClick();
    virtual int DoNextFrame();
    virtual int DoDraw();
    virtual int DoCommand(int);
    virtual void Reset();

    void SetCord(float x, float y, float z, unsigned long delay);
    void SetSize(int w, int h);
    void Enable(unsigned long delay);
    void Disable(unsigned long delay, bool);
    void SetBmp(Bitmap*, unsigned long delay);
    void ChangeBmp(Bitmap*, unsigned long delay);
    void SetBmpCord(Bitmap*, int x, int y, int z, unsigned long delay);
    void AssignString(String*, unsigned long delay);
    void ChangeString(char const*, unsigned long delay);
    void AssignName(char*);
    void ProcessAssignedString();
    void Move(int, int, int, int, int, int, unsigned long dur, unsigned long delay, unsigned char smooth);
    void Move(float, float, float, float, float, float, unsigned long dur, unsigned long delay, unsigned char smooth);
    void MoveTo(int, int, int, unsigned long dur, unsigned long delay, unsigned char smooth);
    void MoveTo(float, float, float, unsigned long dur, unsigned long delay, unsigned char smooth);
    void MoveChangeBmp(Bitmap*, float, float, float, float, float, float, unsigned long, unsigned long, unsigned char);
    void Curve(int, int, int, int, int, int, int, int, int, int, int, int, unsigned long dur, unsigned long delay, unsigned char smooth);
    void FadeIn(unsigned long delay, unsigned long dur, int from, int to);
    void FadeOut(unsigned long delay, unsigned long dur, int from, int to);
    void SetAlpha(int transparency, unsigned long delay);
    void RunAnim(Bitmap* anim, int first, int last, int loops, unsigned long delay, float speed, unsigned long, int);
    void RunDiskAnimThenDelete(char const* name, unsigned long delay, float speed, unsigned long, bool);
    void DeleteAfterLast(unsigned long delay);
    unsigned long LastEventTime();
    void KillAllMovementEvents();
    void KillAllEnableEvents();
    void AddEvent(EventO*);
    void PlaySound(char* wav, int flags, unsigned long delay);
    void ColorIt(int, int, int, int, int, int, int, int, int, unsigned long delay);
    void ColoredOff(unsigned long delay);
    void SetRGB(int r, int g, int b, unsigned long delay);
    void SetTextRGB(int r, int g, int b, unsigned long delay);
    void FadeRGB(int, int, int, int, int, int, unsigned long dur, unsigned long delay, unsigned char smooth);
    void CreateTextDropShadow(float dx, float dy, unsigned long delay, int r, int g, int b);
    void EffectOutline(int, unsigned char, unsigned char, unsigned char, bool);
    void EffectImplode(int, int, int dur, unsigned long delay);
    void EffectExplode(int, int, int dur, unsigned long delay);
    void EffectBalloon(int, unsigned long dur, unsigned long delay);
    void EffectSlideFadeIn(int dir, int dist, int dur, int delay);
    void EffectSquishChangeBmp(Bitmap*, int axis, int frames, unsigned long, unsigned long delay, int, int, bool);
    void Scale(float from, float to, unsigned long dur, unsigned long delay, unsigned char smooth);
    void ScaleXY(float, float, float, float, unsigned long dur, unsigned long delay, unsigned char smooth);
    void ScaleXY(float sx, float sy, unsigned long delay);
    void HardScale(int w, int h);
    void Rotate(float, float, RotationDirection, unsigned long, unsigned long, unsigned char);
    void SetHQScaling(bool);
    Sprite** PixelCollide(float, int);
    void EnableAllClicks(unsigned long delay);
    void DisableAllClicks(unsigned long delay);
    Sprite* Copy();
    void DoChain(List*, int);
    void ChangeClipping(int, int, int, int, int, int, int, int, unsigned long, unsigned long);
    static bool ClicksEnabled;

    // ---- layout
    float x, y;                                  // +0x0c
    float w, h;                                  // +0x14 unscaled size
    SpriteState* st;                             // +0x1c engine state
    int pad20;
    int scale_x, scale_y;                        // +0x24 16.16
    float angle;                                 // +0x2c
    uint32_t flags;                              // +0x30
    int pad34[5];
    float z;                                     // +0x48 draw priority
    int pad4c[4];
    unsigned char transparency;                  // +0x5c (255 = invisible)
    unsigned char pad5d[3];
    Bitmap* bmp;                                 // +0x60
    String* str;                                 // +0x64
    char* name;                                  // +0x68
    int pad6c[4];
    List* click_events;                          // +0x7c
    List* frame_events;                          // +0x80
    List* events84;                              // +0x84
    List* timed_events;                          // +0x88
    List* sig_handlers;                          // +0x8c
    int pad90[4];
    int signal_type;                             // +0xa0
    int pada4;
    unsigned char last;                          // +0xa8 (0xa9..0xab belong to derived classes)
};
static_assert(sizeof(Sprite) == 0xac, "Sprite");
static_assert(offsetof(Sprite, flags) == 0x30, "Sprite +0x30");
static_assert(offsetof(Sprite, z) == 0x48, "Sprite +0x48");
static_assert(offsetof(Sprite, bmp) == 0x60, "Sprite +0x60");
static_assert(offsetof(Sprite, click_events) == 0x7c, "Sprite +0x7c");
static_assert(offsetof(Sprite, timed_events) == 0x88, "Sprite +0x88");
static_assert(offsetof(Sprite, last) == 0xa8, "Sprite +0xa8");

struct WorldState;

class WorldClass : public Group {
public:
    WorldClass();
    virtual ~WorldClass();
    virtual void Init(int, int);                 // slot 6: called right after construction
    virtual void Shutdown();                     // slot 7: before the delete

    Bitmap* LoadBmp(char const*, int lang, float scale, int fmt, int max_frames, bool, bool, Bitmap* into);
    Bitmap* LoadAdBmp(char const*, int lang, float scale, int fmt, int max_frames, bool);
    void DeleteBmp(Bitmap*, unsigned char);
    void PlayMovie(int frames, unsigned long fps, void (*cb)(unsigned char), unsigned char arg, bool, bool, bool, bool);
    void NextFrame(unsigned char, unsigned char, bool, bool, bool, bool);
    void WaitOnFrame(unsigned long ms, bool);
    void SetBack(char const*, int lang);
    void SetBack(Bitmap*, int);
    void SetBack(short vb);
    void RefreshArea(int, int, int, int);
    void ShowAll();
    void ClearScreen();
    Bitmap* GetFrame(Bitmap*, int);
    int AnimationFrames(Bitmap*);
    int FrameNumber(Bitmap*);
    int FrameCount(char const*);
    void PlayDeltaAnim(char*, int x, int y, unsigned long fps, unsigned long, char* sound, unsigned short, unsigned char, bool);
    Bitmap* CopyCompBitmap(Bitmap*, int, int, int, int, int, int, int, int, int, int frames, bool);
    void SpriteDrawTarget(Bitmap*);
    void ResetTimers();

    // engine
    void frame();                                // one engine frame: events, update, reap, draw, clicks
    void render();

    unsigned char pad0c[0x54 - 0x0c];
    ListObj* sobj_list;                          // +0x54
    int pad58;
    std::set<Sprite*>* sprites;                  // +0x5c registry (games iterate it)
    void* root;                                  // +0x60
    unsigned char pad64[0x168 - 0x64];
    short field168;                              // +0x168
    unsigned char pad16a[6];
    uint32_t clock;                              // +0x170 world time, ms
    unsigned char pad174[8];
    int fps;                                     // +0x17c
    unsigned char pad180[0xc];
    int field18c;                                // +0x18c
    unsigned char convert;                       // +0x190
    unsigned char pad191[0xb];
    int frozen;                                  // +0x19c
    unsigned char field1a0;                      // +0x1a0
    unsigned char pad1a1[0x200 - 0x1a1];
    WorldState* ws;                              // +0x200
    int pad204;
};
static_assert(sizeof(WorldClass) == 0x208, "WorldClass");
static_assert(offsetof(WorldClass, sprites) == 0x5c, "World +0x5c");
static_assert(offsetof(WorldClass, clock) == 0x170, "World +0x170");
static_assert(offsetof(WorldClass, fps) == 0x17c, "World +0x17c");

class DOSLinuxWorld : public WorldClass {
public:
    DOSLinuxWorld();
    ~DOSLinuxWorld() override;
};

// Network sprites (linked cabinets): never linked here, so they behave as plain sprites.
class NetSprite : public Sprite {
public:
    NetSprite(Bitmap*, unsigned short, unsigned char, unsigned short);
    ~NetSprite() override;
    virtual void NetClick(unsigned char) = 0;    // slot 11
    void SendPacket(unsigned char);
    unsigned char payload[0x100];                // +0xac received packet
    unsigned char out_len;                       // +0x1ac
    unsigned char pad1ad[3];
    unsigned char* out;                          // +0x1b0
    unsigned char pad1b4[0x2c8 - 0x1b4];
};
static_assert(sizeof(NetSprite) == 0x2c8, "NetSprite");
class NetSpriteLock : public NetSprite {
public:
    NetSpriteLock(Bitmap*, unsigned short, unsigned char, unsigned short);
    ~NetSpriteLock() override;
    void SendPacket(unsigned char);
    void SendPacketLock(unsigned char, unsigned long);
    void SendAck(unsigned char);
    void SendNack(unsigned char);
    unsigned char pad2c8[8];
};
static_assert(sizeof(NetSpriteLock) == 0x2d0, "NetSpriteLock");

namespace legacy {
WorldClass* current_world();
}

// GameClass: base of some games' main objects (cardbandits, wordzap...). A Group with no virtuals of
// its own; 0x10 bytes (wordzap: new(0x40), its own members from +0x10).
class GameClass : public Group {
public:
    GameClass();
    ~GameClass() override;
    int field0c;
};
static_assert(sizeof(GameClass) == 0x10, "GameClass");
