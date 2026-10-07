// RandomizedArrayClass: a shuffled deck of 0..count-1 whose state lives in a PICRAND_record
// {seed, pos, max}, so pictures/questions don't repeat across games (libsettings saves the record).
// The order is a deterministic function of (seed, count): bookmarks replay it. Uses its own
// generator — games re-seed libc rand() right after building their decks.
// docs/reference/gendef-records.md §4.
#include "random.h"
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
uint32_t splitmix(uint64_t& s) {
    uint64_t z = (s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}
int fresh_seed(int old) {
    static uint32_t counter;
    int s;
    do s = (int)(((uint32_t)time(nullptr) * 2654435761u) ^ (++counter * 40503u)) & 0x7fffffff; while (s == 0 || s == old);
    return s;
}
}

void RandomizedArrayClass::shuffle() {
    delete[] order_;
    order_ = nullptr;
    if (count_ <= 0) return;
    order_ = new unsigned short[count_];
    for (int i = 0; i < count_; i++) order_[i] = (unsigned short)i;
    uint64_t st = (uint32_t)rec_->seed;
    for (int i = count_ - 1; i > 0; i--) {
        int j = (int)(splitmix(st) % (uint32_t)(i + 1));
        unsigned short t = order_[i]; order_[i] = order_[j]; order_[j] = t;
    }
    dealt_seed_ = rec_->seed;
}

void RandomizedArrayClass::init(xml_gamerandom::PICRAND_record* rec, int count, unsigned char flag, unsigned short* values) {
    rec_ = rec ? rec : &own_;
    count_ = count;
    flag_ = flag;
    values_ = values;
    order_ = nullptr;
    dealt_seed_ = 0;
    memset(bookmarks_, 0, sizeof bookmarks_);
    if (count <= 0) { rec_->max = count; return; }
    if (rec_->seed == 0 || rec_->max != count || rec_->pos < 0 || rec_->pos >= count) {
        rec_->seed = fresh_seed(rec_->seed);
        rec_->pos = 0;
        rec_->max = count;
    }
    shuffle();
}

RandomizedArrayClass::RandomizedArrayClass(xml_gamerandom::PICRAND_record* rec, int count, unsigned char flag, unsigned short* values) {
    init(rec, count, flag, values);
}
RandomizedArrayClass::RandomizedArrayClass(int count) { init(nullptr, count, 0, nullptr); }
RandomizedArrayClass::~RandomizedArrayClass() { delete[] order_; order_ = nullptr; }

unsigned short RandomizedArrayClass::GetNextValue(int limit) {
    if (count_ <= 0) return 0;
    if (dealt_seed_ != rec_->seed) shuffle();
    for (int tries = 0; tries <= count_ * 2; tries++) {
        if (rec_->pos >= count_) {                // deck used up: a new shuffle
            rec_->seed = fresh_seed(rec_->seed);
            rec_->pos = 0;
            shuffle();
        }
        unsigned short v = order_[rec_->pos++];
        if (limit >= 0 && v >= limit) continue;   // kids mode: only the first `limit` values
        return values_ ? values_[v] : v;
    }
    return 0;
}
void RandomizedArrayClass::SetBookMark(unsigned char n) {
    if (n < kBookmarks) { bookmarks_[n][0] = rec_->seed; bookmarks_[n][1] = rec_->pos; }
}
void RandomizedArrayClass::GotoBookMark(unsigned char n) {
    if (n >= kBookmarks || !bookmarks_[n][0]) return;
    rec_->seed = bookmarks_[n][0];
    rec_->pos = bookmarks_[n][1];
    if (dealt_seed_ != rec_->seed) shuffle();
}
