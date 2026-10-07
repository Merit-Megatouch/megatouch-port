// xml_gamerandom records (loader-only gendef classes). Sizes must match exactly: games embed
// them in their own objects. docs/reference/gendef-records.md §2.
#pragma once
#include "gendef.h"

namespace xml_gamerandom {

class PICRAND_record : public abstract_xml_record {      // 0x14
public:
    int seed, pos, max;                          // +8 shuffle seed (0 = never dealt), +0xc drawn, +0x10 deck size
    PICRAND_record();
    PICRAND_record(const PICRAND_record&);
    ~PICRAND_record() override;
    PICRAND_record& operator=(const PICRAND_record&);
    bool operator==(const PICRAND_record&) const;
    bool operator!=(const PICRAND_record&) const;
    const abstract_metadata* Metadata() const override;
    abstract_record* clone() const override;
    bool equal(const abstract_record*) const override;
    bool not_equal(const abstract_record*) const override;
    void copy(const abstract_record*) override;
    void AdjustTime(int, long) override;
    const char* cTag() const override;
    int eTag() const override;
};
static_assert(sizeof(PICRAND_record) == 0x14, "PICRAND_record");

struct PicGroup { const char* name; int count; };

// a record holding only PICRAND children (contiguous)
#define DECLARE_PIC_CONTAINER(Class, Count)                                          \
    class Class : public abstract_xml_record {                                        \
    public:                                                                           \
        static const int N = Count;                                                   \
        PICRAND_record pics[N];                                                       \
        Class();                                                                      \
        Class(const Class&);                                                          \
        ~Class() override;                                                            \
        Class& operator=(const Class&);                                               \
        bool operator==(const Class&) const;                                          \
        bool operator!=(const Class&) const;                                          \
        const abstract_metadata* Metadata() const override;                           \
        abstract_record* clone() const override;                                      \
        bool equal(const abstract_record*) const override;                            \
        bool not_equal(const abstract_record*) const override;                        \
        void copy(const abstract_record*) override;                                   \
        void AdjustTime(int, long) override;                                          \
        const char* cTag() const override;                                            \
        int eTag() const override;                                                    \
        bool AddChild(abstract_xml_record*, const char*, unsigned int) override;      \
        abstract_xml_record* NewChild(char*) override;                                \
        std::vector<const abstract_xml_record*> GetChildren() const override;         \
        int ChildCount() const override;                                              \
        const char* ChildName(int) const override;                                    \
    };

DECLARE_PIC_CONTAINER(GameSettingsPixMix_record, 8)
DECLARE_PIC_CONTAINER(GameSettingsPhotoHunt_record, 11)
DECLARE_PIC_CONTAINER(GameSettingsRandom_record, 1)
DECLARE_PIC_CONTAINER(TrivPICRAND_record, 190)       // RatingRand[49], CatRand[140], DBRand
DECLARE_PIC_CONTAINER(BigTrivPICRAND_record, 191)    // RatingRand[50], CatRand[140], DBRand
DECLARE_PIC_CONTAINER(MystPICRAND_record, 14)        // CatRand[13], AllRand
static_assert(sizeof(GameSettingsPixMix_record) == 0xa8, "PixMix");
static_assert(sizeof(GameSettingsPhotoHunt_record) == 0xe4, "PhotoHunt");
static_assert(sizeof(GameSettingsRandom_record) == 0x1c, "Random");
static_assert(sizeof(TrivPICRAND_record) == 0xee0, "TrivPICRAND");
static_assert(sizeof(BigTrivPICRAND_record) == 0xef4, "BigTrivPICRAND");
static_assert(sizeof(MystPICRAND_record) == 0x120, "MystPICRAND");

class GameSettingsHighRun_record : public abstract_xml_record {   // 0xc
public:
    unsigned int highRun;                        // +8 <HighRunLength>
    GameSettingsHighRun_record();
    GameSettingsHighRun_record(const GameSettingsHighRun_record&);
    ~GameSettingsHighRun_record() override;
    GameSettingsHighRun_record& operator=(const GameSettingsHighRun_record&);
    const abstract_metadata* Metadata() const override;
    abstract_record* clone() const override;
    bool equal(const abstract_record*) const override;
    bool not_equal(const abstract_record*) const override;
    void copy(const abstract_record*) override;
    void AdjustTime(int, long) override;
    const char* cTag() const override;
    int eTag() const override;
    bool AddChild(abstract_xml_record*, const char*, unsigned int) override;
    abstract_xml_record* NewChild(char*) override;
    std::vector<const abstract_xml_record*> GetChildren() const override;
    int ChildCount() const override;
    const char* ChildName(int) const override;
};
static_assert(sizeof(GameSettingsHighRun_record) == 0xc, "HighRun");

class GameSettingsTriviaRandom_record : public abstract_xml_record {   // 0xee8
public:
    TrivPICRAND_record Random;                   // +8
    GameSettingsTriviaRandom_record();
    GameSettingsTriviaRandom_record(const GameSettingsTriviaRandom_record&);
    ~GameSettingsTriviaRandom_record() override;
    GameSettingsTriviaRandom_record& operator=(const GameSettingsTriviaRandom_record&);
    const abstract_metadata* Metadata() const override;
    abstract_record* clone() const override;
    bool equal(const abstract_record*) const override;
    bool not_equal(const abstract_record*) const override;
    void copy(const abstract_record*) override;
    void AdjustTime(int, long) override;
    const char* cTag() const override;
    int eTag() const override;
    bool AddChild(abstract_xml_record*, const char*, unsigned int) override;
    abstract_xml_record* NewChild(char*) override;
    std::vector<const abstract_xml_record*> GetChildren() const override;
    int ChildCount() const override;
    const char* ChildName(int) const override;
};
static_assert(sizeof(GameSettingsTriviaRandom_record) == 0xee8, "TriviaRandom");

}  // namespace xml_gamerandom
