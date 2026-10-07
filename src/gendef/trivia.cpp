// DBFClass (dBase III reader, optionally XOR-encrypted) and TriviaClass (question banks), plus
// the text helpers the trivia games call. docs/reference/gendef-records.md §5.
#include "random.h"
#include <zlib.h>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace Locale { enum Languages : int {}; }
namespace xml_gameinfo { enum GameIds : int {}; }

// ------------------------------------------------------------------------------ DBFClass
// 0x220 bytes, virtual dtor (slot 1). Fields are addressed by 0-based index.
class DBFClass {
public:
    DBFClass();
    virtual ~DBFClass();
    void SetEncryption(bool);
    bool OpenDBF(char const*);
    void CloseDBF();
    bool GetRecord(int);
    int GetNumRecords();
    bool nGetField(unsigned int field, char* out, unsigned int size, bool trim);
    // engine helpers
    struct Field { char name[12]; int offset, length; };
    struct State { std::vector<uint8_t> data; std::vector<Field> fields; int nrec = 0, hlen = 0, rlen = 0, cur = -1; };
    State* st;
    bool encrypted;
    unsigned char pad[0x220 - 4 - sizeof(State*) - sizeof(bool)];
    int field_index(const char* name) const;
    std::string field(int rec, int idx, bool trim) const;
};
static_assert(sizeof(DBFClass) == 0x220, "DBFClass");

DBFClass::DBFClass() : st(new State), encrypted(false) { memset(pad, 0, sizeof pad); }
DBFClass::~DBFClass() { delete st; st = nullptr; }
void DBFClass::SetEncryption(bool on) { encrypted = on; }
bool DBFClass::OpenDBF(char const* path) {
    if (!path) return false;
    gzFile gz = gzopen(path, "rb");                   // reads plain files too (through the fs shim)
    if (!gz) {
        std::string p = std::string(path) + ".gz";
        gz = gzopen(p.c_str(), "rb");
        if (!gz) { fprintf(stderr, "[gendef] DBF %s not found\n", path); return false; }
    }
    st->data.clear();
    unsigned char buf[65536];
    int n;
    while ((n = gzread(gz, buf, sizeof buf)) > 0) st->data.insert(st->data.end(), buf, buf + n);
    gzclose(gz);
    std::vector<uint8_t>& d = st->data;
    if (encrypted || (d.size() > 0 && d[0] != 0x03 && d[0] != 0x83)) {
        static const uint8_t K[20] = {0x81, 0x88, 0x83, 0x85, 0x87, 0x82, 0x84, 0x9e, 0x94, 0x9d,
                                      0x91, 0x93, 0x8b, 0x99, 0x96, 0x9a, 0x98, 0x90, 0x8c, 0x92};
        for (size_t i = 0; i < d.size(); i++) d[i] ^= K[(i + 2) % 20];
    }
    if (d.size() < 32) return false;
    st->nrec = (int)(d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24);
    st->hlen = d[8] | d[9] << 8;
    st->rlen = d[10] | d[11] << 8;
    st->fields.clear();
    int off = 1;                                      // deletion flag
    for (size_t o = 32; o + 32 <= d.size() && o < (size_t)st->hlen && d[o] != 0x0d; o += 32) {
        Field f{};
        memcpy(f.name, &d[o], 11);
        f.offset = off; f.length = d[o + 16];
        off += f.length;
        st->fields.push_back(f);
    }
    st->cur = -1;
    if (getenv("MEGA_DEBUG_DBF")) {
        fprintf(stderr, "[dbf] %s: %d records, header %d, record %d, %zu fields:", path, st->nrec, st->hlen, st->rlen, st->fields.size());
        for (auto& f : st->fields) fprintf(stderr, " %s(%d)", f.name, f.length);
        fputc('\n', stderr);
    }
    return st->rlen > 0 && (size_t)st->hlen <= d.size();
}
void DBFClass::CloseDBF() { st->data.clear(); st->fields.clear(); st->nrec = 0; st->cur = -1; }
int DBFClass::GetNumRecords() { return st->nrec; }
bool DBFClass::GetRecord(int r) {
    if (r < 0 || r >= st->nrec || (size_t)st->hlen + (size_t)(r + 1) * st->rlen > st->data.size()) return false;
    st->cur = r;
    return true;
}
std::string DBFClass::field(int rec, int idx, bool trim) const {
    if (rec < 0 || idx < 0 || idx >= (int)st->fields.size()) return std::string();
    const Field& f = st->fields[idx];
    size_t o = (size_t)st->hlen + (size_t)rec * st->rlen + f.offset;
    if (o + f.length > st->data.size()) return std::string();
    std::string s(reinterpret_cast<const char*>(&st->data[o]), f.length);
    if (trim) {
        size_t b = s.find_first_not_of(' '), e = s.find_last_not_of(' ');
        s = b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    }
    for (auto& c : s) if (c == '`') c = '\'';        // a backquote stands for an apostrophe
    return s;
}
int DBFClass::field_index(const char* name) const {
    for (size_t i = 0; i < st->fields.size(); i++) if (!strncmp(st->fields[i].name, name, 11)) return (int)i;
    return -1;
}
bool DBFClass::nGetField(unsigned int idx, char* out, unsigned int size, bool trim) {
    if (!out || !size) return false;
    std::string s = field(st->cur, (int)idx, trim);
    snprintf(out, size, "%s", s.c_str());
    static bool dbg = getenv("MEGA_DEBUG_DBF") != nullptr;
    if (dbg) fprintf(stderr, "[dbf] rec %d field %u size %u -> '%s'\n", st->cur, idx, size, out);
    return st->cur >= 0;
}

// ------------------------------------------------------------------------------ TriviaClass
// 0x29ac bytes. The current question is at +0 (games read it inline); the rest is ours.
class TriviaClass {
public:
    TriviaClass(xml_gamerandom::TrivPICRAND_record*, char*, unsigned char, unsigned char, unsigned char);
    TriviaClass(xml_gamerandom::BigTrivPICRAND_record*, char*, unsigned char, unsigned char, unsigned char);
    ~TriviaClass();
    int GetQuesData(int cat, int rating);
    void ReadData(int recno);
    unsigned char GetRandomAns(unsigned char n);
    int GetRandomCat();
    int GetAmountOfQuestionsInCatX(short cat);

    char q1[0xfe], q2[0xfe];                     // +0x000, +0x0fe
    char ans[4][0x8c];                           // +0x1fc ([0] is the right answer)
    char topic[0x40];                            // +0x42c
    char info1[0xfe], info2[0xfe];               // +0x46c, +0x56a
    int gif;                                     // +0x668
    // ---- private (0x66c..0x29a8)
    struct Cat { int id = -1, total = 0, start = 0, rstart[4] = {}, rcount[4] = {}; RandomizedArrayClass* rac = nullptr; };
    struct Impl {
        DBFClass db;
        std::vector<Cat> cats;
        xml_gamerandom::PICRAND_record* picks = nullptr;   // per-category PICRANDs (CatRand)
        int npicks = 0;
        xml_gamerandom::PICRAND_record cat_cycle;
        RandomizedArrayClass* cat_rac = nullptr;
        std::vector<unsigned short> cat_ids;
        bool adult = false;
        unsigned used_answers = 0;
    };
    Impl* impl;
    unsigned char pad[0x29a9 - 0x66c - sizeof(Impl*)];
    unsigned char utf8;                          // +0x29a9: 0 = strings need MeritStringToUTF8
    unsigned char pad2[2];
    void init(xml_gamerandom::PICRAND_record* catrand, int ncatrand, char* path, unsigned char adult, unsigned char flags);
    Cat* cat_by_id(int id);
};
static_assert(sizeof(TriviaClass) == 0x29ac, "TriviaClass");
static_assert(offsetof(TriviaClass, ans) == 0x1fc && offsetof(TriviaClass, topic) == 0x42c, "TriviaClass layout");
static_assert(offsetof(TriviaClass, gif) == 0x668 && offsetof(TriviaClass, utf8) == 0x29a9, "TriviaClass layout");

void TriviaClass::init(xml_gamerandom::PICRAND_record* catrand, int ncatrand, char* path, unsigned char adult, unsigned char flags) {
    memset(this, 0, sizeof *this);
    impl = new Impl;
    impl->adult = adult;
    impl->picks = catrand;
    impl->npicks = ncatrand;
    impl->db.SetEncryption(flags & 1);
    if (!impl->db.OpenDBF(path)) return;
    DBFClass& d = impl->db;
    int fq1 = d.field_index("QLINE1"), fq2 = d.field_index("QLINE2"), fa = d.field_index("ANSWERA"),
        fb = d.field_index("ANSWERB"), fc = d.field_index("ANSWERC"), fd = d.field_index("ANSWERD"),
        ft = d.field_index("TOPIC"), fi1 = d.field_index("INFO1"), fi2 = d.field_index("INFO2"),
        fcat = d.field_index("CATEGORY");
    // index records first: numbers, one per category id; questions start at the lowest start
    int first_q = d.GetNumRecords();
    for (int r = 0; r < d.GetNumRecords() && r < first_q; r++) {
        std::string tot = d.field(r, fq1, true);
        if (tot.empty()) { impl->cats.push_back(Cat()); continue; }
        if (tot.find_first_not_of("0123456789") != std::string::npos) break;
        Cat c;
        c.id = fcat >= 0 && !d.field(r, fcat, true).empty() ? atoi(d.field(r, fcat, true).c_str()) : r;
        c.total = atoi(tot.c_str());
        c.start = atoi(d.field(r, fq2, true).c_str());
        int vals[8] = {atoi(d.field(r, fq2, true).c_str()), atoi(d.field(r, fa, true).c_str()),
                       atoi(d.field(r, fb, true).c_str()), atoi(d.field(r, fc, true).c_str()),
                       atoi(d.field(r, fd, true).c_str()), atoi(d.field(r, ft, true).c_str()),
                       atoi(d.field(r, fi1, true).c_str()), atoi(d.field(r, fi2, true).c_str())};
        for (int k = 0; k < 4; k++) { c.rstart[k] = vals[2 * k]; c.rcount[k] = vals[2 * k + 1]; }
        if (c.total > 0) first_q = std::min(first_q, c.start);
        impl->cats.push_back(c);
    }
    for (size_t i = 0; i < impl->cats.size(); i++) {
        Cat& c = impl->cats[i];
        if (c.total <= 0) continue;
        xml_gamerandom::PICRAND_record* pr = i < (size_t)ncatrand && catrand ? &catrand[i] : nullptr;
        c.rac = new RandomizedArrayClass(pr, c.total, 0, nullptr);
        if (c.id != 4 || adult) impl->cat_ids.push_back((unsigned short)c.id);
    }
    if (!impl->cat_ids.empty())
        impl->cat_rac = new RandomizedArrayClass(&impl->cat_cycle, (int)impl->cat_ids.size(), 1, impl->cat_ids.data());
}
TriviaClass::TriviaClass(xml_gamerandom::TrivPICRAND_record* r, char* path, unsigned char adult, unsigned char flags, unsigned char) {
    init(r ? r->pics + 49 : nullptr, 140, path, adult, flags);
}
TriviaClass::TriviaClass(xml_gamerandom::BigTrivPICRAND_record* r, char* path, unsigned char adult, unsigned char flags, unsigned char) {
    init(r ? r->pics + 50 : nullptr, 140, path, adult, flags);
}
TriviaClass::~TriviaClass() {
    if (!impl) return;
    for (auto& c : impl->cats) delete c.rac;
    delete impl->cat_rac;
    delete impl;
    impl = nullptr;
}
TriviaClass::Cat* TriviaClass::cat_by_id(int id) {
    for (auto& c : impl->cats) if (c.id == id && c.total > 0) return &c;
    return nullptr;
}
void TriviaClass::ReadData(int recno) {
    DBFClass& d = impl->db;
    impl->used_answers = 0;
    if (!d.GetRecord(recno)) return;
    auto get = [&](const char* f, char* out, size_t n) { std::string s = d.field(recno, d.field_index(f), true); snprintf(out, n, "%s", s.c_str()); };
    get("QLINE1", q1, sizeof q1); get("QLINE2", q2, sizeof q2);
    get("ANSWERA", ans[0], sizeof ans[0]); get("ANSWERB", ans[1], sizeof ans[1]);
    get("ANSWERC", ans[2], sizeof ans[2]); get("ANSWERD", ans[3], sizeof ans[3]);
    get("TOPIC", topic, sizeof topic); get("INFO1", info1, sizeof info1); get("INFO2", info2, sizeof info2);
    char g[16]; get("GIF", g, sizeof g); gif = atoi(g);
}
int TriviaClass::GetQuesData(int cat, int rating) {
    if (!impl || impl->cats.empty()) return 0;
    Cat* c = cat_by_id(cat);
    if (!c) c = cat_by_id(GetRandomCat());
    if (!c || !c->rac) return 0;
    int rec = c->start + c->rac->GetNextValue(-1);
    // a rating asked for: draw until one falls in that rating's range (bounded)
    if (rating >= 0 && rating < 4 && c->rcount[rating] > 0)
        for (int tries = 0; tries < c->total && !(rec >= c->rstart[rating] && rec < c->rstart[rating] + c->rcount[rating]); tries++)
            rec = c->start + c->rac->GetNextValue(-1);
    ReadData(rec);
    return rec;
}
// n calls per question: each a different slot 0..n-1 (the first is where the right answer goes)
unsigned char TriviaClass::GetRandomAns(unsigned char n) {
    if (!impl || !n) return 0;
    if ((impl->used_answers & ((1u << n) - 1)) == ((1u << n) - 1)) impl->used_answers = 0;
    for (;;) {
        unsigned v = (unsigned)rand() % n;
        if (!(impl->used_answers & (1u << v))) { impl->used_answers |= 1u << v; return (unsigned char)v; }
    }
}
int TriviaClass::GetRandomCat() { return impl && impl->cat_rac ? impl->cat_rac->GetNextValue(-1) : 0; }
int TriviaClass::GetAmountOfQuestionsInCatX(short cat) {
    if (!impl || (cat == 4 && !impl->adult)) return 0;
    Cat* c = cat_by_id(cat);
    return c ? c->total : 0;
}

// ------------------------------------------------------------------------------ text helpers
namespace TextUtils {
char* MeritStringToUTF8(char const*);
char* MeritStringToUTF8(char const*, Locale::Languages);
char* ConvertToMarkup(char const*);
void VPatch(char*, int, char const*, char const*, ...);
char* ToUpper(char*);
}
// Latin-1 (the shipped databases) to UTF-8, in rotating static buffers
char* TextUtils::MeritStringToUTF8(char const* s) {
    static char bufs[8][1024];
    static int k;
    char* out = bufs[k++ & 7];
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)(s ? s : ""); *p && o + 3 < sizeof bufs[0]; p++) {
        if (*p < 0x80) out[o++] = (char)*p;
        else { out[o++] = (char)(0xc0 | *p >> 6); out[o++] = (char)(0x80 | (*p & 0x3f)); }
    }
    out[o] = 0;
    return out;
}
char* TextUtils::MeritStringToUTF8(char const* s, Locale::Languages) { return MeritStringToUTF8(s); }
// Callers wrap text in markup first, so tags stay: only a bare '&' is escaped (merit3d.md §1.6)
char* TextUtils::ConvertToMarkup(char const* s) {
    static char bufs[4][2048];
    static int k;
    char* out = bufs[k++ & 3];
    size_t o = 0;
    for (const char* p = s ? s : ""; *p && o + 7 < sizeof bufs[0]; p++) {
        if (*p == '&') {
            const char* e = strchr(p, ';');
            bool entity = e && e - p < 8 && (isalpha((unsigned char)p[1]) || p[1] == '#');
            if (!entity) { strcpy(out + o, "&amp;"); o += 5; continue; }
        }
        out[o++] = *p;
    }
    out[o] = 0;
    return out;
}
// replaces the first `pattern` in buf with the formatted value
void TextUtils::VPatch(char* buf, int size, char const* pattern, char const* fmt, ...) {
    if (!buf || !pattern || !fmt || size <= 0) return;
    char* at = strstr(buf, pattern);
    if (!at) return;
    char val[256];
    va_list ap; va_start(ap, fmt); vsnprintf(val, sizeof val, fmt, ap); va_end(ap);
    std::string s(buf, at - buf);
    s += val;
    s += at + strlen(pattern);
    snprintf(buf, size, "%s", s.c_str());
}
char* TextUtils::ToUpper(char* s) { for (char* p = s; p && *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; return s; }

// Chain-restaurant picture variants: no such content ships (never reached for normal GameIds)
class ChainRestClass { public: char const* GetPicFileName(xml_gameinfo::GameIds, int); };
char const* ChainRestClass::GetPicFileName(xml_gameinfo::GameIds, int) { return "/usr/local/gamedata/gamegraphics/misc/blank.jpg"; }
