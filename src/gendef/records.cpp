// The loader's xml_gamerandom records: per-game settings files (GameSettings*_record) and the
// random-deck state they hold (PICRAND_record children). They derive from the cabinet's
// abstract_xml_record so libsettings / libgendef_xml can read and write them, and so the games'
// dynamic_casts through their typeinfo work. docs/reference/gendef-records.md §1-2.
#include "records.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
// integer field helpers for the field-definition tables
template <int OFF> void get_i32(abstract_record* r, const char* s) {
    *reinterpret_cast<int*>(reinterpret_cast<char*>(r) + OFF) = s ? (int)strtol(s, nullptr, 0) : 0;
}
template <int OFF> void put_i32(const abstract_record* r, std::string* out, bool) {
    char b[24];
    snprintf(b, sizeof b, "%d", *reinterpret_cast<const int*>(reinterpret_cast<const char*>(r) + OFF));
    out->append(b);
}
template <int OFF> bool def_i32(const abstract_record* r) {
    return *reinterpret_cast<const int*>(reinterpret_cast<const char*>(r) + OFF) == 0;
}
db_common_field_def int_field(const char* name, void (*g)(abstract_record*, const char*),
                              void (*p)(const abstract_record*, std::string*, bool), bool (*d)(const abstract_record*)) {
    db_common_field_def f;
    memset(&f, 0, sizeof f);
    snprintf(f.name, sizeof f.name, "%s", name);
    snprintf(f.xml, sizeof f.xml, "%s", name);
    snprintf(f.type, sizeof f.type, "INT4");
    f.get = g; f.put = p; f.is_default = d;
    return f;
}
// a metadata object built once (the cabinet constructor fills it; never destroyed)
template <class Tag> const abstract_metadata* metadata(const char* table, const db_common_field_def* f, int n) {
    alignas(16) static unsigned char buf[sizeof(abstract_metadata)];
    static abstract_metadata* m = new (buf) abstract_metadata(table, f, n, "xml_gamerandom", nullptr, 0, nullptr, 0, false);
    return m;
}
}

namespace xml_gamerandom {

// ------------------------------------------------------------------------------ PICRAND_record
PICRAND_record::PICRAND_record() : seed(0), pos(0), max(0) {}
PICRAND_record::PICRAND_record(const PICRAND_record& o) : abstract_xml_record(o), seed(o.seed), pos(o.pos), max(o.max) {}
PICRAND_record::~PICRAND_record() {}
PICRAND_record& PICRAND_record::operator=(const PICRAND_record& o) {
    if (this != &o) { abstract_xml_record::operator=(o); seed = o.seed; pos = o.pos; max = o.max; }
    return *this;
}
bool PICRAND_record::operator==(const PICRAND_record& o) const { return seed == o.seed && pos == o.pos && max == o.max; }
bool PICRAND_record::operator!=(const PICRAND_record& o) const { return !(*this == o); }
const abstract_metadata* PICRAND_record::Metadata() const {
    static const db_common_field_def f[3] = {
        int_field("seed", get_i32<0x08>, put_i32<0x08>, def_i32<0x08>),
        int_field("pos", get_i32<0x0c>, put_i32<0x0c>, def_i32<0x0c>),
        int_field("max", get_i32<0x10>, put_i32<0x10>, def_i32<0x10>),
    };
    return metadata<PICRAND_record>("PICRAND", f, 3);
}
abstract_record* PICRAND_record::clone() const { return new PICRAND_record(*this); }
bool PICRAND_record::equal(const abstract_record* r) const {
    auto* p = r ? dynamic_cast<const PICRAND_record*>(r) : nullptr;
    return p && *p == *this;
}
bool PICRAND_record::not_equal(const abstract_record* r) const {
    auto* p = r ? dynamic_cast<const PICRAND_record*>(r) : nullptr;
    return !p || *p != *this;
}
void PICRAND_record::copy(const abstract_record* r) {
    if (auto* p = r ? dynamic_cast<const PICRAND_record*>(r) : nullptr) *this = *p;
}
void PICRAND_record::AdjustTime(int d, long w) { if (d) AdjustTimeFields(d, w); }
const char* PICRAND_record::cTag() const { return "PICRAND"; }
int PICRAND_record::eTag() const { return 0; }

// ------------------------------------------------------------------------------ PICRAND groups
// Records whose content is runs of PICRAND children (laid out contiguously after the header):
// the XML child names and counts, in order.
bool pic_add(PICRAND_record* first, const PicGroup* g, int ng, abstract_xml_record* c, const char* name, unsigned idx) {
    auto* p = c ? dynamic_cast<PICRAND_record*>(c) : nullptr;
    if (!p || !name) return false;
    int base = 0;
    for (int i = 0; i < ng; base += g[i].count, i++)
        if (!strcmp(name, g[i].name)) {
            if ((int)idx >= g[i].count) return false;
            first[base + idx] = *p;
            return true;
        }
    return false;
}
abstract_xml_record* pic_new(const PicGroup* g, int ng, const char* name) {
    for (int i = 0; i < ng; i++) if (name && !strcmp(name, g[i].name)) return new PICRAND_record;
    return nullptr;
}
const char* pic_name(const PicGroup* g, int ng, int idx) {
    for (int i = 0; i < ng; idx -= g[i].count, i++) if (idx >= 0 && idx < g[i].count) return g[i].name;
    return nullptr;
}
int pic_count(const PicGroup* g, int ng) { int n = 0; for (int i = 0; i < ng; i++) n += g[i].count; return n; }

#define PIC_CONTAINER(Class, Tag, Groups)                                                              \
    static const PicGroup Class##_groups[] = Groups;                                                  \
    static const int Class##_ng = sizeof(Class##_groups) / sizeof(PicGroup);                          \
    Class::Class() {}                                                                                  \
    Class::Class(const Class& o) : abstract_xml_record(o) { for (int i = 0; i < N; i++) pics[i] = o.pics[i]; } \
    Class::~Class() {}                                                                                 \
    Class& Class::operator=(const Class& o) {                                                          \
        if (this != &o) { abstract_xml_record::operator=(o); for (int i = 0; i < N; i++) pics[i] = o.pics[i]; } \
        return *this;                                                                                  \
    }                                                                                                  \
    bool Class::operator==(const Class& o) const { for (int i = 0; i < N; i++) if (pics[i] != o.pics[i]) return false; return true; } \
    bool Class::operator!=(const Class& o) const { return !(*this == o); }                            \
    const abstract_metadata* Class::Metadata() const {                                                 \
        static const db_common_field_def none[1] = {};                                                 \
        return metadata<Class>(Tag, none, 0);                                                          \
    }                                                                                                  \
    abstract_record* Class::clone() const { return new Class(*this); }                                 \
    bool Class::equal(const abstract_record* r) const { auto* p = r ? dynamic_cast<const Class*>(r) : nullptr; return p && *p == *this; } \
    bool Class::not_equal(const abstract_record* r) const { auto* p = r ? dynamic_cast<const Class*>(r) : nullptr; return !p || *p != *this; } \
    void Class::copy(const abstract_record* r) { if (auto* p = r ? dynamic_cast<const Class*>(r) : nullptr) *this = *p; } \
    void Class::AdjustTime(int d, long w) { if (d) { for (int i = 0; i < N; i++) pics[i].AdjustTime(d, w); AdjustTimeFields(d, w); } } \
    const char* Class::cTag() const { return Tag; }                                                    \
    int Class::eTag() const { return 0; }                                                              \
    bool Class::AddChild(abstract_xml_record* c, const char* name, unsigned idx) { return pic_add(pics, Class##_groups, Class##_ng, c, name, idx); } \
    abstract_xml_record* Class::NewChild(char* name) { return pic_new(Class##_groups, Class##_ng, name); } \
    std::vector<const abstract_xml_record*> Class::GetChildren() const {                              \
        std::vector<const abstract_xml_record*> v;                                                     \
        for (int i = 0; i < N; i++) v.push_back(&pics[i]);                                             \
        return v;                                                                                      \
    }                                                                                                  \
    int Class::ChildCount() const { return N; }                                                        \
    const char* Class::ChildName(int i) const { return pic_name(Class##_groups, Class##_ng, i); }

#define G(...) {__VA_ARGS__}
PIC_CONTAINER(GameSettingsPixMix_record, "GameSettings", G({"Random", 8}))
PIC_CONTAINER(GameSettingsPhotoHunt_record, "GameSettings", G({"Random", 11}))
PIC_CONTAINER(GameSettingsRandom_record, "GameSettings", G({"Random", 1}))
PIC_CONTAINER(TrivPICRAND_record, "TrivPICRAND", G({"RatingRand", 49}, {"CatRand", 140}, {"DBRand", 1}))
PIC_CONTAINER(BigTrivPICRAND_record, "BigTrivPICRAND", G({"RatingRand", 50}, {"CatRand", 140}, {"DBRand", 1}))
PIC_CONTAINER(MystPICRAND_record, "MystPICRAND", G({"CatRand", 13}, {"AllRand", 1}))

// ------------------------------------------------------------------------------ HighRun
GameSettingsHighRun_record::GameSettingsHighRun_record() : highRun(0) {}
GameSettingsHighRun_record::GameSettingsHighRun_record(const GameSettingsHighRun_record& o) : abstract_xml_record(o), highRun(o.highRun) {}
GameSettingsHighRun_record::~GameSettingsHighRun_record() {}
GameSettingsHighRun_record& GameSettingsHighRun_record::operator=(const GameSettingsHighRun_record& o) {
    if (this != &o) { abstract_xml_record::operator=(o); highRun = o.highRun; }
    return *this;
}
const abstract_metadata* GameSettingsHighRun_record::Metadata() const {
    static const db_common_field_def f[1] = {int_field("HighRunLength", get_i32<0x08>, put_i32<0x08>, def_i32<0x08>)};
    return metadata<GameSettingsHighRun_record>("GameSettings", f, 1);
}
abstract_record* GameSettingsHighRun_record::clone() const { return new GameSettingsHighRun_record(*this); }
bool GameSettingsHighRun_record::equal(const abstract_record* r) const {
    auto* p = r ? dynamic_cast<const GameSettingsHighRun_record*>(r) : nullptr;
    return p && p->highRun == highRun;
}
bool GameSettingsHighRun_record::not_equal(const abstract_record* r) const {
    auto* p = r ? dynamic_cast<const GameSettingsHighRun_record*>(r) : nullptr;
    return !p || p->highRun != highRun;
}
void GameSettingsHighRun_record::copy(const abstract_record* r) {
    if (auto* p = r ? dynamic_cast<const GameSettingsHighRun_record*>(r) : nullptr) *this = *p;
}
void GameSettingsHighRun_record::AdjustTime(int d, long w) { if (d) AdjustTimeFields(d, w); }
const char* GameSettingsHighRun_record::cTag() const { return "GameSettings"; }
int GameSettingsHighRun_record::eTag() const { return 0; }
// the abstract_xml_record child defaults (no children), defined here so the vtable slots exist
bool GameSettingsHighRun_record::AddChild(abstract_xml_record* c, const char* n, unsigned i) { return abstract_xml_record::AddChild(c, n, i); }
abstract_xml_record* GameSettingsHighRun_record::NewChild(char* n) { return abstract_xml_record::NewChild(n); }
std::vector<const abstract_xml_record*> GameSettingsHighRun_record::GetChildren() const { return {}; }
int GameSettingsHighRun_record::ChildCount() const { return 0; }
const char* GameSettingsHighRun_record::ChildName(int) const { return nullptr; }

// ------------------------------------------------------------------------------ TriviaRandom
// one TrivPICRAND_record child named "Random"
GameSettingsTriviaRandom_record::GameSettingsTriviaRandom_record() {}
GameSettingsTriviaRandom_record::GameSettingsTriviaRandom_record(const GameSettingsTriviaRandom_record& o)
    : abstract_xml_record(o), Random(o.Random) {}
GameSettingsTriviaRandom_record::~GameSettingsTriviaRandom_record() {}
GameSettingsTriviaRandom_record& GameSettingsTriviaRandom_record::operator=(const GameSettingsTriviaRandom_record& o) {
    if (this != &o) { abstract_xml_record::operator=(o); Random = o.Random; }
    return *this;
}
const abstract_metadata* GameSettingsTriviaRandom_record::Metadata() const {
    static const db_common_field_def none[1] = {};
    return metadata<GameSettingsTriviaRandom_record>("GameSettings", none, 0);
}
abstract_record* GameSettingsTriviaRandom_record::clone() const { return new GameSettingsTriviaRandom_record(*this); }
bool GameSettingsTriviaRandom_record::equal(const abstract_record* r) const {
    auto* p = r ? dynamic_cast<const GameSettingsTriviaRandom_record*>(r) : nullptr;
    return p && p->Random == Random;
}
bool GameSettingsTriviaRandom_record::not_equal(const abstract_record* r) const {
    auto* p = r ? dynamic_cast<const GameSettingsTriviaRandom_record*>(r) : nullptr;
    return !p || p->Random != Random;
}
void GameSettingsTriviaRandom_record::copy(const abstract_record* r) {
    if (auto* p = r ? dynamic_cast<const GameSettingsTriviaRandom_record*>(r) : nullptr) *this = *p;
}
void GameSettingsTriviaRandom_record::AdjustTime(int d, long w) { if (d) { Random.AdjustTime(d, w); AdjustTimeFields(d, w); } }
const char* GameSettingsTriviaRandom_record::cTag() const { return "GameSettings"; }
int GameSettingsTriviaRandom_record::eTag() const { return 0; }
bool GameSettingsTriviaRandom_record::AddChild(abstract_xml_record* c, const char* name, unsigned idx) {
    auto* p = c ? dynamic_cast<TrivPICRAND_record*>(c) : nullptr;
    if (!p || !name || strcmp(name, "Random") || idx) return false;
    Random = *p;
    return true;
}
abstract_xml_record* GameSettingsTriviaRandom_record::NewChild(char* name) {
    return name && !strcmp(name, "Random") ? new TrivPICRAND_record : nullptr;
}
std::vector<const abstract_xml_record*> GameSettingsTriviaRandom_record::GetChildren() const { return {&Random}; }
int GameSettingsTriviaRandom_record::ChildCount() const { return 1; }
const char* GameSettingsTriviaRandom_record::ChildName(int i) const { return i == 0 ? "Random" : nullptr; }

}  // namespace xml_gamerandom
