// RandomizedArrayClass (0x3c bytes, not polymorphic; games read the count at +4).
#pragma once
#include "records.h"
#include <cstddef>
#include <cstdint>

class RandomizedArrayClass {
public:
    RandomizedArrayClass(xml_gamerandom::PICRAND_record* rec, int count, unsigned char flag, unsigned short* values);
    explicit RandomizedArrayClass(int count);
    ~RandomizedArrayClass();
    unsigned short GetNextValue(int limit);
    void SetBookMark(unsigned char);
    void GotoBookMark(unsigned char);

    int count() const { return count_; }
    // (public: offsets are checked below)
    void init(xml_gamerandom::PICRAND_record* rec, int count, unsigned char flag, unsigned short* values);
    void shuffle();
    static const int kBookmarks = 2;
    xml_gamerandom::PICRAND_record* rec_;        // +0x00 the persisted deck state
    int count_;                                  // +0x04 (read inline by games)
    unsigned char flag_;                         // +0x08
    unsigned short* values_;                     // +0x0c optional value table (mysteryphraze categories)
    xml_gamerandom::PICRAND_record own_;         // +0x10 state when no record is given
    unsigned short* order_;                      // +0x24 the shuffled deck
    int dealt_seed_;                             // +0x28 seed order_ was built from
    int bookmarks_[kBookmarks][2];               // +0x2c {seed, pos}
};
static_assert(sizeof(RandomizedArrayClass) == 0x3c, "RandomizedArrayClass");
static_assert(offsetof(RandomizedArrayClass, count_) == 4, "RAC +4");
