# Gendef records, Settings, RandomizedArrayClass, TriviaClass (legacy loader classes)

Legacy games import a family of classes that only the loader defined:

- the generated record classes `xml_gamerandom::*_record`;
- `RandomizedArrayClass`, a picture/question index generator that never repeats an index;
- `TriviaClass`, a question-database reader;
- `ChainRestClass::GetPicFileName`.

This page specifies their ABI and behaviour so they can be written by hand. The evidence comes from:

- the game libraries and their Ghidra decompiles;
- the cabinet's shipped `libgendef_common.so`, `libgendef_xml.so` and `libsettings.so`;
- other cabinet libraries that ship generated records: `golf.so`, `castlebandits.so`, `libami.so`;
- the game libraries' own generated `xml_gamesettings` records;
- saved settings files found in the disk image.

The loader binary was not used. This page is a companion to [sprite-engine.md](sprite-engine.md).

Conventions:

- **Addresses** are ELF addresses, as objdump prints them. Ghidra decompiles add 0x10000; where a decompile address is quoted it says "decomp". A bare address belongs to the library named in the same sentence.
- **Confidence:** **[C]** confirmed from code at several sites, **[L]** likely, **[?]** guess or unresolved.
- **ABI:** i386, g++ 3.4/4.x, Itanium. Methods are cdecl with `this` pushed first. A `_ZTV…` symbol points at `[offset-to-top][typeinfo*][slot 0]…`, so the object's vptr is `&_ZTV + 8`. "vptr+0xN" means slot N/4.
- **Usage counts** ("N games") are the number of game `.so` files, under `games/*/lib/`, that import the symbol.
- **Decompiles added for this work:** `games/{pixmix,phunt_new,tritowers,trivia,kid_phunt,boxxi,mysteryphraze,powertrivia,quizshow_new,tic_tac_trivia,reversetriv}/decomp/`. Scratchpad Ghidra runs covered libgendef_common, libgendef_xml, libsettings and golf.so.

---

## 0. Summary: what to build, in priority order

1. **Use the shipped base classes.** Load `libgendef_common.so`, `libgendef_xml.so` and `libsettings.so` from the cabinet image globally, before the game. Derive the new classes from `abstract_xml_record`. Section 1 gives the exact layout and vtable.
2. **Typeinfo and vtables must be real** for every `xml_gamerandom` record below. Each one is a `__si_class_type_info` whose base is `abstract_xml_record`. Today's zero-filled placeholders are why pixmix crashes in `Settings::Load()`: libsettings cross-casts through the game's vmi typeinfo into the record typeinfo (§3.2). The games' own generated containers (spotmatch, boxxi, castlebandits, quizshow…) `dynamic_cast` children to `PICRAND_record` / `TrivPICRAND_record` / `MystPICRAND_record` in `AddChild`.
3. **`sizeof` values must match exactly.** Games embed these records inside their own objects and step through them at fixed strides.

| Class | sizeof | Contents | Users |
|---|---|---|---|
| `xml_gamerandom::PICRAND_record` | **0x14** [C] | hdr(8) + `int seed, pos, max` | 26 games import it; ~36 use it through RAC |
| `xml_gamerandom::MystPICRAND_record` | **0x120** [C] | hdr + `PICRAND[13]` (per category) + `PICRAND all` | mysteryphraze ×3, g_mystery_phraze_hd |
| `xml_gamerandom::TrivPICRAND_record` | **0xee0** [C] | hdr + `RatingRand[49]` + `CatRand[140]` + `DBRand` | 8 trivia games |
| `xml_gamerandom::BigTrivPICRAND_record` | **0xef4** [C] | hdr + 191 PICRANDs (split [?]) | reversetriv |
| `xml_gamerandom::GameSettingsPixMix_record` | **0xa8** [C] | hdr + `PICRAND[8]` | pixmix, cepixmix, chpixmix, pepixmix |
| `xml_gamerandom::GameSettingsPhotoHunt_record` | **0xe4** [C] | hdr + `PICRAND[11]` | phunt_new, ephunt_new, pephunt_new, phphunt_new |
| `xml_gamerandom::GameSettingsHighRun_record` | **0xc** [C] | hdr + `uint32 highRun` | tritowers, stairs, symbolstairs, magiccharms, symboltritowers |
| `xml_gamerandom::GameSettingsTriviaRandom_record` | **0xee8** [C] | hdr + `TrivPICRAND_record` | trivia, brainquest, powertrivia, psh2htrivia, tic_tac_trivia |
| `xml_gamerandom::GameSettingsRandom_record` | **0x1c** [C] | hdr + `PICRAND_record` | kid_phunt, snapshot |
| `RandomizedArrayClass` | **0x3c** [C] | not polymorphic; `+4 int count` is read inline | 37 games |
| `TriviaClass` | **0x29ac** [C] | current-question struct at +0, flag at +0x29a9 | 8 games |
| `Settings` (shipped) | 0x14 | the game's `GameSettings` = `Settings` + record at +0x14 | |

4. **Behaviour.** `RandomizedArrayClass` deals a shuffled deck of `0..count-1` that is a deterministic function of `PICRAND{seed,pos,max}`. That way libsettings persists the deck position in `/var/merit/settings/never/<name>.xml`, and pictures and questions don't repeat across games (§4). `TriviaClass` reads XOR-encrypted dBase question files (§5).

---
## 1. The gendef base classes

The record classes are produced by Merit's "gendef" generator (source paths in log strings:
`/devel/megatouch/apps/libs/gendef_common/abstractdata.cpp`,
`/devel/megatouch/apps/libs/gendef_xml/abstractdata_xml.cpp`, and per-schema files such as
`xml_gamesettings.cpp`). Two base classes ship with the cabinet and must be **used, not
reimplemented**: link (or `dlopen` with `RTLD_GLOBAL`) the cabinet's `libgendef_common.so` and
`libgendef_xml.so`, then derive the missing `xml_gamerandom` classes from them.

### 1.1 Hierarchy and typeinfo chain [C]

```
abstract_record            libgendef_common.so  sizeof 4    typeinfo: __class_type_info
 └── abstract_xml_record   libgendef_xml.so     sizeof 8    typeinfo: __si_class_type_info(base abstract_record)
      └── <ns>::<Name>_record   generated        sizeof 8+fields   typeinfo: __si_class_type_info(base abstract_xml_record)
```

Evidence: relocations at libgendef_common `0x5208` (`_ZTVN10__cxxabiv117__class_type_infoE`),
libgendef_xml `0xb088`/`0xb090` (`__si_class_type_info`, base `_ZTI15abstract_record`), and every
generated record in libami.so / golf.so, e.g. `typeinfo for xml_ami_books::Deposit_record` at
libami `0x15a3ac` = `{__si_class_type_info vtable+8, _ZTSN13xml_ami_books14Deposit_recordE,
_ZTI19abstract_xml_record}`. Single, public, non-virtual inheritance throughout, so the
`dynamic_cast`s that the generated code and libsettings perform only need the
`si_class_type_info` chain to be real and the `_ZTI…`/`_ZTS…` symbols to be the single exported
definition (pointer-equal across the game, libsettings and our host).

**This is the crash fix.** Each game's `GameSettings` multiply inherits `Settings` and the
loader-defined record, so its own `typeinfo for GameSettings` is a `__vmi_class_type_info`
whose second base points at `_ZTIN14xml_gamerandom25GameSettingsPixMix_recordE` (pixmix `0xb420`).
`SettingsManager::Load` cross-casts `Settings*` → `abstract_xml_record*` (§3.2), which
walks into that record typeinfo; a zero-filled placeholder has a null type_info vptr, so the walk
crashes. Compiling a real `class GameSettingsPixMix_record : public abstract_xml_record` in
namespace `xml_gamerandom` (with a key function defined out of line, so g++ emits the
typeinfo/vtable in our object, and exported with default visibility) produces the correct
symbols automatically.

### 1.2 Object layout [C]

| Offset | Type | Field | Set by |
|---|---|---|---|
| +0x00 | `void**` | vptr | ctors |
| +0x04 | `char*` | `mLastXMLError` (strdup'd, freed in dtor; `LastXMLError()` returns it) | `abstract_xml_record()` sets 0 (libgendef_xml `0x3130`) |
| +0x08… | — | generated fields (see 1.6) | derived ctor + `Clear()` |

`abstract_record` has no data (`abstract_record::abstract_record()` at libgendef_common `0x1a30`
only stores the vptr). `abstract_xml_record(const&)` (`0x2f00`) copies only the error string;
`operator=` (`0x2ec0`) likewise. Dtors: `D1` (complete) and `D0` (deleting); `D2` exported too.

### 1.3 Vtable layout [C]

The symbol `_ZTV…` points at `[offset-to-top 0][typeinfo*][slots…]`; the vptr is `&_ZTV + 8`.
Offsets below are from the vptr (as used in `call *off(%eax)`).

| Slot | vptr off | Method | abstract_record | abstract_xml_record | generated record |
|---|---|---|---|---|---|
| 0 | 0x00 | `~T()` complete (D1) | own | own | own |
| 1 | 0x04 | `~T()` deleting (D0) | own | own | own |
| 2 | 0x08 | `abstract_metadata const* Metadata() const` | returns an empty static metadata | inherited | **own** (`return ClassMetadata();`) |
| 3 | 0x0c | `abstract_record* clone() const` | pure | pure | `return new T(*this);` |
| 4 | 0x10 | `bool equal(abstract_record const*) const` | pure | pure | dyncast + `operator==` |
| 5 | 0x14 | `bool not_equal(abstract_record const*) const` | pure | pure | dyncast + `operator!=` |
| 6 | 0x18 | `void copy(abstract_record const*)` | pure | pure | dyncast + `operator=` |
| 7 | 0x1c | `void AdjustTime(int, long)` | no-op (`0x1a70`) | inherited | own (see 1.8) |
| 8 | 0x20 | `char const* cTag() const` | — | pure | returns element tag name |
| 9 | 0x24 | `int eTag() const` | — | pure | returns record kind code |
| 10 | 0x28 | `bool AddChild(abstract_xml_record*, char const*, unsigned)` | — | returns 0 (`0x2df0`) | own if record has children |
| 11 | 0x2c | `abstract_xml_record* NewChild(char*)` | — | returns 0 (`0x2e00`) | own if record has children |
| 12 | 0x30 | `std::vector<abstract_xml_record const*> GetChildren() const` | — | returns empty vector (`0x2e40`) | own if children |
| 13 | 0x34 | `int ChildCount() const` | — | returns 0 (`0x2e10`) | own if children |
| 14 | 0x38 | `void AdjustTimeFields(int, long)` | — | no-op (`0x2e20`) | inherited (never overridden in libami/golf) |
| 15 | 0x3c | `char const* ChildName(int) const` | — | returns 0 (`0x2e30`) | own if children |

Sources: `tools/vtdump.py` on libgendef_common (`_ZTV15abstract_record`, 10 entries),
libgendef_xml (`_ZTV19abstract_xml_record`, 18 entries) and libami/golf records (18 entries).
Return types come from the call sites: `Metadata()` result is passed to
`abstract_metadata::FieldDefinition`; `cTag()` result to `strcmp`; `GetChildren` uses the
hidden-sret convention (`ret $4`) and is declared `std::vector<abstract_xml_record const*>` in
golf's demangled body (`push_back` on `vector<abstract_xml_record const*>`, golf `0xf95c`);
`equal`/`not_equal` return `bool` in `%al`.

Declaration order to reproduce this layout in C++ (all `virtual`, in this order):

```cpp
class abstract_record {                         // libgendef_common.so
public:
  abstract_record();
  virtual ~abstract_record();
  virtual const abstract_metadata* Metadata() const;
  virtual abstract_record* clone() const = 0;
  virtual bool equal(const abstract_record*) const = 0;
  virtual bool not_equal(const abstract_record*) const = 0;
  virtual void copy(const abstract_record*) = 0;
  virtual void AdjustTime(int, long);
  // + non-virtual: SetField, ParseLine, ParseLineFromString, AsString, PipeDelimited,
  //   static Get_*/Put_* helpers (inline/weak in the header, see 1.7)
};
class abstract_xml_record : public abstract_record {   // libgendef_xml.so
public:
  abstract_xml_record(); abstract_xml_record(const abstract_xml_record&);
  abstract_xml_record& operator=(const abstract_xml_record&);
  virtual ~abstract_xml_record();
  virtual const char* cTag() const = 0;
  virtual int eTag() const = 0;
  virtual bool AddChild(abstract_xml_record*, const char*, unsigned);
  virtual abstract_xml_record* NewChild(char*);
  virtual std::vector<const abstract_xml_record*> GetChildren() const;
  virtual int ChildCount() const;
  virtual void AdjustTimeFields(int, long);
  virtual const char* ChildName(int) const;
  // non-virtual: ReadFile(...), WriteFile(...), ReadString, WriteString, ParseString,
  //   ParseStream, ReadDelineated*, AppendDelineatedFile, LastXMLError()
protected:
  char* mLastXMLError;   // +4
};
```

(Return type of `AddChild` is `bool` in golf — it returns 0/1 in `%eax` and the parser ignores
it [L]; `int` is ABI-identical.)

### 1.4 `abstract_metadata` and `db_common_field_def` [C]

`Metadata()` returns a function-local static `abstract_metadata` built once by
`ClassMetadata()` (guarded static + `__cxa_atexit`). Constructor
(libgendef_common `0x3120`/`0x3420`):

```cpp
abstract_metadata(const char* tableName,           // the record name, e.g. "GameSettings"
                  const db_common_field_def* fields, int nFields,
                  const char* databaseName,         // the schema namespace, e.g. "xml_gamesettings"
                  const db_common_index_def* idx, int nIdx,   // 0,0 for XML records
                  const char* const* triggers, int nTrig,     // 0,0 for XML records
                  bool skipDefaults);               // see WriteFile below
```

Object layout (`sizeof` ≥ 0x48; generated code reserves the static, never `new`s it):
`+0 tableName, +4 databaseName, +8 bool skipDefaults, +0xc fields, +0x10 nFields,
+0x14..+0x28 std::map<std::string,int> name→index (by field name), +0x2c std::string
"a,b,c", +0x30 std::string "Table.a,Table.b", +0x34 std::string "a|b|c", +0x38 idx, +0x3c nIdx,
+0x40 triggers, +0x44 nTrig`. Fields whose type string is `"ignore"` are left out of the
comma/pipe lists. `FieldDefinition(i)` returns `fields + i*0xa8` (`0x2c60`).

`db_common_field_def` (stride **0xa8**), from libgendef_common `AsString`/`SetField`,
libgendef_xml `WriteString`, and the arrays in golf.so (`0x2c8a0`, `0x2c960`):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | `char[0x32]` | field (C++/DB column) name; key of the name→index map |
| 0x32 | `char[0x36]` | XML element/attribute name (matched with `strcmp` by the parser; usually equal to 0x00) |
| 0x68 | `char[0xc]` | optional printf format (`"%hu"` for castlebandits' `HighRunLength`, empty in golf) — informational |
| 0x74 | `char[0x24]` | type string: `"INT1"`, `"INT2"`, `"INT4"`, `"TEXT"`, `"VARCHAR"`, `"TIME"`, `"ignore"`… (informational for XML; `"ignore"` drops it from SQL lists) |
| 0x98 | `void (*)(abstract_record*, const char*)` | `Get_<f>`: parse text into the field |
| 0x9c | `void (*)(const abstract_record*, std::string*, bool)` | `Put_<f>`: append text form of the field |
| 0xa0 | `bool (*)(const abstract_record*)` | `Default_<f>`: true if field equals `sDefault()`'s value |
| 0xa4 | `bool` | 1 = write as an XML **attribute** of the record's element, 0 = write as a child **element** `<name>value</name>` |
| 0xa5 | `bool` | passed as the last argument of `Put_<f>` (unused by the integer helpers) [L] |
| 0xa6 | `char[2]` | padding |

Example (golf.so `0x2c960`, `HoleInfo` field 1): `"lowest_strokes"`, `"lowest_strokes"`,
`"INT2"`, `&HoleInfo_record::Get_lowest_strokes`, `&…::Put_lowest_strokes`,
`&…::Default_lowest_strokes`, 0, 0.

### 1.5 How libgendef_xml drives a record (the virtual-call contract) [C]

Only these virtuals are ever called by libgendef_xml (all indirect calls in the library, tallied
from `objdump`): `Metadata` (0x08), `cTag` (0x20), `AddChild` (0x28), `NewChild` (0x2c),
`GetChildren` (0x30), `ChildName` (0x3c), deleting dtor (0x04) — plus the field-def function
pointers at 0x98/0x9c/0xa0. **`eTag`, `ChildCount`, `clone`, `equal`, `not_equal`, `copy`,
`AdjustTime` are never called by the XML layer**; they are used by libsettings (section 3) and
the games.

Reading (`ReadFile` → expat; start handler at libgendef_xml `0x5560`, end handler `0x42a0`):

1. Root element: its name must `strcmp`-equal `root->cTag()`, else the parse fails and logs.
2. Each child element: first `child = current->NewChild(name)`; non-null → it becomes the
   current record (pushed on a `std::deque<abstract_xml_record*>`). Null → the name is looked up
   among the current record's fields (XML name at `+0x32`) and the element text is later passed to
   that field's `Get_` (`+0x98`); no match → the subtree is ignored (logged). Attributes go
   through the same field lookup. **So `NewChild` must return 0 for field names.**
3. On the child's end tag: `parent->AddChild(child, name, n)` where `n` is how many earlier
   siblings had the same element name (a per-level `std::map<std::string,int>` counter), then
   **`delete child`** (vtable +4). So `AddChild` must copy the data out (generated code does
   `member[n] = *dynamic_cast<Child*>(child)`), never keep the pointer.

Writing (`WriteString`/`WriteFile`, `0x6460`/`0x7350`): element name = `cTag()` (or the name the
parent passes from `ChildName(i)`); attributes = fields with `+0xa4`≠0; then elements for fields
with `+0xa4`=0; then for each `GetChildren()[i]` a nested element named `ChildName(i)`. With
`skipDefaults` (metadata `+8`) and the caller's flag, fields whose `Default_` returns true are
omitted.

Golf's settings file shows the result (cabinet `usr/local/gamedata/config/golf.xml`):

```xml
<GameSettings>
    <longest_distance>533</longest_distance>
    <Course>
        <Hole longest_distance="9600" lowest_strokes="4" />   <!-- 18 per Course -->
        …
    </Course>                                                 <!-- 3 Courses -->
</GameSettings>
```

(the hand-written file uses attributes for `Hole` although golf's field defs have `+0xa4`=0; the
reader accepts either form.)

### 1.6 What a generated record looks like — the golf.so template [C]

golf.so is a cabinet game library that **ships its own generated schema** `xml_gamesettings`
(`GameSettings_record` 0x2ac bytes, `CourseInfo_record` 0xe0, `HoleInfo_record` 0xc) and uses it
with `Settings`. It is the reference for hand-writing the `xml_gamerandom` classes. Every method
below is from golf.so (decomp addresses minus 0x10000):

```cpp
namespace xml_gamesettings {
extern const char* cTag_GameSettings;   // "GameSettings"  (exported data symbols, char* to rodata)
extern const char* cTag_Course;         // "Course"
extern const char* cTag_CourseInfo;     // "CourseInfo"
extern const char* cTag_Hole;           // "Hole"
extern const char* cTag_HoleInfo;       // "HoleInfo"

class HoleInfo_record : public abstract_xml_record {        // sizeof 0xc
public:
  short longest_distance;   // +8
  short lowest_strokes;     // +0xa
  HoleInfo_record() { longest_distance = lowest_strokes = 0; Clear(); }      // 0xf13c
  HoleInfo_record(const HoleInfo_record& o) : abstract_xml_record(o) { …copy fields… }
  HoleInfo_record& operator=(const HoleInfo_record& o) { if (this!=&o) {…fields…} return *this; }
  bool operator==(const HoleInfo_record& o) const { return this==&o || (fields equal); }
  bool operator!=(const HoleInfo_record& o) const { return this!=&o && (any field differs); }
  void Clear() { longest_distance = 0; lowest_strokes = 0; }    // defaults live here
  static const HoleInfo_record& sDefault();                       // guarded static instance
  static abstract_metadata* ClassMetadata();                      // guarded static (see 1.4)
  const abstract_metadata* Metadata() const { return ClassMetadata(); }
  abstract_record* clone() const { return new HoleInfo_record(*this); }      // new(0xc)
  bool equal(const abstract_record* r) const {
    const HoleInfo_record* p = r ? dynamic_cast<const HoleInfo_record*>(r) : 0;
    return p ? (*p == *this) : false; }
  bool not_equal(const abstract_record* r) const {
    const HoleInfo_record* p = r ? dynamic_cast<const HoleInfo_record*>(r) : 0;
    return p ? (*p != *this) : true; }
  void copy(const abstract_record* r) {
    if (r) if (const HoleInfo_record* p = dynamic_cast<const HoleInfo_record*>(r)) *this = *p; }
  void AdjustTime(int delta, long when) { if (delta) AdjustTimeFields(delta, when); }
  const char* cTag() const { return cTag_HoleInfo; }
  int eTag() const { return 4; }
  // field accessors referenced from the field-def table:
  static void Get_lowest_strokes(abstract_record* r, const char* s)
      { Get_s16(&((HoleInfo_record*)r)->lowest_strokes, 0, s); }
  static void Put_lowest_strokes(const abstract_record* r, std::string* out, bool b)
      { Put_s16(((const HoleInfo_record*)r)->lowest_strokes, 0, out, b); }
  static bool Default_lowest_strokes(const abstract_record* r)
      { return ((const HoleInfo_record*)r)->lowest_strokes == sDefault().lowest_strokes; }
};

class GameSettings_record : public abstract_xml_record {   // sizeof 0x2ac
public:
  short longest_distance;          // +8
  CourseInfo_record Course[3];     // +0xc, stride 0xe0 — fixed-size child array, embedded
  int ChildCount() const { return 3; }
  const char* ChildName(int i) const { return (unsigned)i < 3 ? "Course" : 0; }
  abstract_xml_record* NewChild(char* name)
      { return strcmp(name, cTag_Course) == 0 ? new CourseInfo_record : 0; }
  bool AddChild(abstract_xml_record* c, const char* name, unsigned idx) {
    if (strcmp("Course", name) == 0) {
      CourseInfo_record* p = c ? dynamic_cast<CourseInfo_record*>(c) : 0;
      if (idx < 3) { Course[idx] = *p; return true; }
      return false;
    }
    Logger::Log(... c->eTag(), cTag(), c->cTag() ...);      // unknown child
    return false;
  }
  std::vector<const abstract_xml_record*> GetChildren() const
      { std::vector<const abstract_xml_record*> v; for (i<3) v.push_back(&Course[i]); return v; }
  void AdjustTime(int d, long w)
      { if (d) { for (i<3) Course[i].AdjustTime(d, w); AdjustTimeFields(d, w); } }
  int eTag() const { return 2; }
  // dtor destroys Course[2..0] in reverse, then ~abstract_xml_record()
};
}
```

`ClassMetadata()` for these: `abstract_metadata("GameSettings", GameSettings_fields, 1,
"xml_gamesettings", 0,0,0,0, false)` and `abstract_metadata("HoleInfo", HoleInfo_fields, 2,
"xml_gamesettings", 0,0,0,0, true)`. The record with children but no own fields
(`CourseInfo_record`, eTag 1) passes `nFields = 0`.

**`eTag()` codes** (never read by libgendef_xml; returned by every record): libami uses 0, 1, 2;
golf uses 1 (`CourseInfo`: only children), 2 (`GameSettings`: root with fields + children), 4
(`HoleInfo`: leaf with attribute-style fields). castlebandits' root `GameSettings_record` (PICRAND children)
returns 0 and spotmatch's returns 0. Treat it as an opaque generator constant; use 0 for the
gamerandom records [L].

### 1.7 Field helper functions [C]

`abstract_record::Get_<type>(T* dst, unsigned width, const char* s)` and
`Put_<type>(T v, unsigned width, std::string* out, bool)` are **inline in the gendef header**:
each library that uses them carries a weak copy (golf.so `0xfca2`/`0xfc08` for `s16`). The set
(union of weak copies found in cabinet libs): `u8 s16 u16 s32 u32 bool float text cppstring`
(+ `timestamp`, `ipaddr` exported from libgendef_common). Integer forms are
`snprintf("%d")` / `strtol(s, 0, 0)` (so hex `0x…` and octal parse too). Our hand-written
records may implement `Get_/Put_` however they like — nothing outside the record calls them
except through the field-def table.

### 1.8 `AdjustTime` / `AdjustTimeFields` [C]

`AdjustTime(int delta, long when)` is meant for cabinet clock changes (libsettings exports
`SettingsManager::AdjustTime(long,long)`); neither libsettings' load/save path nor libgendef_xml
calls it on a record [L]. Generated records: if
`delta != 0`, recurse into embedded child records, then call `this->AdjustTimeFields(delta,
when)` (vtable +0x38), which is a no-op unless the record has `TIME` fields. For records without
time fields a no-op is correct.

### 1.9 Second shipped template: castlebandits.so (PICRAND children) [C]

castlebandits.so (and cardbandits, g_panty_bandits, g_hunks_card_bandits, g_penthouse_castle_bandits) ships `xml_gamesettings::GameSettings_record`, sizeof **0xac**. The game's `GameSettings` is `new(0xc0)`, which is 0x14 for `Settings` plus 0xac. Its children are **loader** `PICRAND_record`s, so this is the exact shape the gamerandom GameSettings records need. Disassembly 0x85ec–0x8a66:

- **Layout:** +8 `PICRAND_record child[8]` (stride 0x14); +0xa8 `u8 HighRunLength`.
  - The field def is at 0x170e0: `"HighRunLength"`, `"HighRunLength"`, `"%hu"`, `"INT1"`, Get/Put/Default, +0xa4 = 0.
- **`cTag()`** returns `*cTag_GameSettings` = `"GameSettings"`. **`eTag()`** returns 0.
- **`ChildCount()`** returns 8. **`ChildName(i)`** returns `"Random"` for `i <= 7`, else 0.
- **`GetChildren()`** returns `&child[0..7]`.
- **`NewChild(n)`** returns `new(0x14) PICRAND_record` when `n` equals `cTag_Random` (`"Random"`), else 0.
- **`AddChild(c, "Random", n)`:** `p = dynamic_cast<PICRAND_record*>(c)` (source type `abstract_xml_record`, hint 0); if `n <= 7`, `child[n] = *p`, return 1.
- **`copy` / `operator=`:** per-child `PICRAND_record::operator=`, then the scalar field. `copy` is at 0x88e6.
- **`AdjustTime(a, b)`:** if `a`, call `child[i].AdjustTime(a, b)` (vptr+0x1c), then `AdjustTimeFields(a, b)`.
- **`Clear()`:** assigns a default-constructed `PICRAND_record` to each child; `HighRunLength = 0`.

Saved form (`g_panty_bandits.xml` in the image):
`<GameSettings><HighRunLength>15</HighRunLength><Random><seed>…</seed><pos>…</pos><max>…</max></Random>×8</GameSettings>`.

---

## 2. The `xml_gamerandom` records

All of these derive directly from `abstract_xml_record` and follow the template in §1.6/§1.9. Use `"xml_gamerandom"` as the metadata database name [L]. Defaults are 0 for every field: `Clear()` zeroes them, and untouched decks in saved files are `0/0/0` [L].

### 2.1 Saved files: ground truth for the XML shape [C]

Real files from cabinets are in `shared/data-common/var-template/merit/settings/never/`, copied under `games/*/data/var/merit/settings/never/`. Examples: `boxxi.xml`, `brainquest.xml`, `snapshot.xml`, `h2hgenderbender.xml`, `g_panty_bandits.xml`, `penthousephotopop.xml`. They are single-line ("compact") XML:

```xml
<!-- snapshot.xml: GameSettingsRandom_record -->
<GameSettings><Random><seed>7254894</seed><pos>7</pos><max>999</max></Random></GameSettings>

<!-- brainquest.xml: GameSettingsTriviaRandom_record (abbreviated) -->
<GameSettings><Random>
  <RatingRand><seed>1309678</seed><pos>0</pos><max>201</max></RatingRand>   ×49
  <CatRand>…</CatRand>                                                     ×140
  <DBRand><seed>1309681</seed><pos>0</pos><max>1000</max></DBRand>
</Random></GameSettings>

<!-- boxxi.xml: the game's own record, 12 × Random + HometownRandom -->
<GameSettings><Random><seed>28336526</seed><pos>0</pos><max>185</max></Random>…
  <HometownRandom><seed>0</seed><pos>0</pos><max>0</max></HometownRandom></GameSettings>
```

What these files establish:

- PICRAND's fields are the elements `seed`, `pos` and `max`, in that order. Their field defs have `+0xa4` = 0.
- A child's element name comes from its parent's `ChildName(i)`: `Random`, `HometownRandom`, `RatingRand`, `CatRand`, `DBRand`, `SafariRand`, `DDCRandom`, `PSRandom`, and so on. It never comes from the child's own `cTag()`.
- No pixmix, phunt_new, tritowers, trivia or kid_phunt file ships, so those child names are inferred.

### 2.2 `PICRAND_record` (sizeof 0x14) [C]

```cpp
namespace xml_gamerandom {
class PICRAND_record : public abstract_xml_record {
public:
  int seed;   // +0x08  <seed>  0 = deck never initialised; else the shuffle seed
  int pos;    // +0x0c  <pos>   values already drawn from the current shuffle
  int max;    // +0x10  <max>   deck size = count given to RandomizedArrayClass
  ...
};
}
```

**Size [C].** These all agree on 0x14:

- spotmatch `GameSettings_record::NewChild` (decomp 0x56758) and boxxi `NewChild` (decomp 0x17c32) both call `new(0x14)` and then `PICRAND_record()`.
- Embedded arrays step by 0x14 in `operator=`, `Clear` and `GetChildren` (spotmatch decomp 0x567a6–0x56d36).
- Container sizes fit `8 + N*0x14`:

| Game | Container size | N |
|---|---|---|
| boxxi | 0x10c | 13 |
| lookout | 0xbc | 9 |
| h2hgenderbender | 0x58 | 4 |
| spotmatch | 0x44 | 3 |
| safari | 0x1c | 1 |

**Field order and int type [L].** The 12 payload bytes give 3 dwords, and `max` reaches 3000 in phunt_new. No game reads the fields inline; only our RandomizedArrayClass touches them.

**`max` [C].** It is the count the game passes to RAC. boxxi `GAME_InitPicsDeck` (0xc56a) passes {185,125,0,160,30,0,1271,…}, which are exactly the `<max>` values in boxxi.xml, in the same order.

**`seed` [L].** Time-derived. Decks created together get consecutive or equal values (boxxi: 28336526 ×5, then 28336527).

Exports the games call, plus the virtuals they need:

| Member | Imported by | Required behaviour |
|---|---|---|
| `PICRAND_record()` | 26 | zero all three fields (`Clear()`) |
| `~PICRAND_record()` | 26 | base dtor. Containers destroy embedded children via vptr slot 0 (spotmatch decomp 0x56970) |
| `operator=(const&)` | 23 | copy 3 fields; return `*this` |
| `operator!=(const&) const` | 23 | any field differs. Containers' `operator==/!=` use it, and so does libsettings change detection |
| `PICRAND_record(const&)` | 10 (boxxi family, brainquest, hollywood_match, safari) | copy |
| `AdjustTime(int d, long t)` | same 10 | `if (d) AdjustTimeFields(d, t);`. Called directly for a scalar member (boxxi decomp 0x17b3a, `this+0xf8`) |
| typeinfo | 23 | si → `abstract_xml_record`. The containers' `AddChild` does `__dynamic_cast(child, &typeid(abstract_xml_record), &typeid(PICRAND_record), 0)` (spotmatch decomp 0x56842) |
| `Metadata()` | via vtable | **3 field defs: `seed`, `pos`, `max`** (INT4, `+0xa4` = 0). The XML reader fills the child through it, so this must be right or state is lost |
| `clone/equal/not_equal/copy` | via vtable | standard pattern (§1.6) |
| `cTag()` / `eTag()` | via vtable | never visible in the files. Use `"PICRAND"` [?] and 0 [?] |
| child virtuals | — | inherit the `abstract_xml_record` defaults (no children) |

### 2.3 `GameSettings*_record`: the roots of the per-game settings files

Every user game declares:

```cpp
class GameSettings : public Settings, public xml_gamerandom::GameSettingsXxx_record { … };
GameSettings::GameSettings() : Settings("<name>", 0) { Settings::Load(); }
GameSettings::~GameSettings() { Settings::Cleanup(); }
```

Layout [C]: `Settings` at +0 (0x14 bytes), the record at **+0x14**. The game's own typeinfo is `__vmi_class_type_info`, flags 0, with 2 bases: `{&typeid(Settings), 0x2}` and `{&typeid(record), 0x1402}` (public, offset 0x14). Evidence: pixmix `typeinfo for GameSettings` 0xb420.

The game's vtable has 25 slots. The secondary vtable for the record starts at slot 7 (offset-to-top −0x14), and every record slot points at the loader's `GameSettingsXxx_record::*` imports. That is why each record's whole virtual set is imported. No game overrides `Init`, `Key` or `Restore`, so all defaults come from the record ctor. Settings names, all with flags 0, so files live in `/var/merit/settings/never/<name>.xml`:

| Record | Games → Settings name | sizeof | Layout | Evidence |
|---|---|---|---|---|
| `GameSettingsPixMix_record` | pixmix, cepixmix, chpixmix, pepixmix → `"pixmix"` | 0xa8 [C] | +8 `PICRAND_record Random[8]`, `ChildName` = `"Random"` [L] | `GAME_globals` = `new(0xf7c)`, GameSettings at +0xec0. RAC loop over `globals+0xedc + i*0x14`, i<8, with counts {200,200,100,394,106,1354,999,858} (pixmix 0x677e) |
| `GameSettingsPhotoHunt_record` | phunt_new, ephunt_new, pephunt_new, phphunt_new → `"phunt_new"` | 0xe4 [C] | +8 `PICRAND_record Random[11]` [L names] | `PhuntClass` = `new(0x27298)`, GameSettings at +0x271a0. `InitPicsDeck` (decomp 0x1be38) RACs at +0x271bc ×11 with counts {292,232,76,440,120,3000,370,0,0,196,296} |
| `GameSettingsHighRun_record` | tritowers, stairs/symbolstairs → `"stairs"`, magiccharms, symboltritowers | 0xc [C] | +8 `uint32 highRun`, element name `HighRunLength` [L, by analogy with castlebandits]. **No children**: the game's vtable uses the `abstract_xml_record::` child defaults | `GameSettings` = `new(0x20)` (tritowers 0x8c9e). `SetHighRun(int)` (0x6b3c) stores the value if it is larger (unsigned compare). The ctor zeroes it after `Load` when `HighScoresManager::HighestScore()==0` |
| `GameSettingsTriviaRandom_record` | trivia, brainquest, powertrivia, psh2htrivia (`"h2htrivia"`), tic_tac_trivia | 0xee8 [C] | +8 `TrivPICRAND_record Random` (one child, name `"Random"`) | `GameSettings` = `new(0xefc)` (trivia decomp 0x2aae2, brainquest 0x10709). `TriviaClass` gets `GetInstance()+0x1c` |
| `GameSettingsRandom_record` | kid_phunt (`"kids_phunt"`), snapshot | 0x1c [C] | +8 `PICRAND_record Random` | `new(0x30)`. `RAC(GetInstance()+0x1c, 150, 0, 0)` in kid_phunt `Photo::InitPhotos` (0x45dc). snapshot.xml confirms the shape |

For every row:

- `cTag()` returns `"GameSettings"`. This is required: it is the root element, and `ReadFile` rejects a mismatch.
- `eTag()` returns 0, as castlebandits does.
- `ChildCount`/`ChildName`/`GetChildren`/`NewChild`/`AddChild` follow §1.9.
- Metadata has 0 fields, except HighRun, which has 1.

### 2.4 `TrivPICRAND_record` (0xee0), `BigTrivPICRAND_record` (0xef4), `MystPICRAND_record` (0x120)

These are records whose only content is PICRAND children. Each needs its own `NewChild`/`AddChild`/`GetChildren`/`ChildCount`/`ChildName` for the XML round trip: games don't import these virtuals, but libgendef_xml calls them through the vtable. The games' containers call `operator=`, `operator!=`, the copy ctor and `AdjustTime` on them (quizshow_new decomp 0x190a8). They also `dynamic_cast` them in `AddChild` (quizshow_new decomp 0x19262), so the typeinfo must be real.

| Record | Size evidence | Layout |
|---|---|---|
| `TrivPICRAND_record` | `new(0xee0)` in quizshow_new 0x91a7 and htquizshow 0x6826. 8 + 190×0x14 = 0xee0, matching brainquest.xml's 49+140+1 children [C] | +0x008 `RatingRand[49]`, +0x3dc `CatRand[140]`, +0xecc `DBRand`, in XML order [L]. `CatRand[c].max` = question count of category c (brainquest.xml `CatRand[0..4]` = 201/200/200/198/201, the category sizes of `grade2.dat`) [C]. `RatingRand` indexing [?]; `DBRand` purpose [?] (max 1000) |
| `BigTrivPICRAND_record` | `new(0xef4)` in reversetriv 0x831b → 191 children [C] | Suggested: `RatingRand[50]`, `CatRand[140]`, `DBRand` [?] |
| `MystPICRAND_record` | `new(0x120)` in mysteryphraze 0x8ba6 and g_mystery_phraze_hd 0x160c1 → 14 children [C] | +0x008 `PICRAND cat[13]`, one per myst.dat category, used by `MystClass` ctor 0xa98a–0xaa48 as `RAC(&cat[i], count_i, 0, 0)`. +0x10c `PICRAND all` over all phrases, used by `GetPhrasesNotByCat` [C]. Child XML names unknown (no saved file); use `CatRand`×13 + `AllRand` [?] |

The game-side containers that hold these records:

| Game | Child tags | Record type |
|---|---|---|
| quizshow_new | `Random`, `PSRandom` | TrivPICRAND |
| htquizshow | `Random`, `PSRandom`, `DDCRandom` | TrivPICRAND |
| reversetriv | `Random` | BigTriv |
| mysteryphraze family | `Random`, `PSRandom`, `DDCRandom` | Myst |
| g_mystery_phraze_hd | `Random` | Myst |

---

## 3. `Settings` / `SettingsManager` (libsettings.so)

The library ships with the cabinet. Load it and do not reimplement it. Source strings: `/devel/megatouch/apps/libs/settings/settings{,_manager}.cpp`.

### 3.1 `Settings` object (sizeof 0x14) [C]

Vtable (0x9c60): [0] `Init()` (empty, 0x24d0), [1] `Key(unsigned,bool) const`, [2] `Restore(abstract_xml_record const*)`, [3] D1, [4] D0.

| Off | Field |
|---|---|
| 0 | vptr |
| 4 | `char* name` (strdup'd by the ctor at 0x2980) |
| 8 | `uint flags`. The low byte is the location code |
| 0xc | `time_t lastSaved` |
| 0x10 | `time_t loadTime` |

Flag bits:

| Bit | Meaning |
|---|---|
| 0x8 | error / duplicate. Save is refused |
| 0x400 | untracked (no snapshot) |
| 0x800 | encrypted. `Key()` returns `"newsettingskey"` (Blowfish) |
| 0x1000 | loaded |
| 0x2000 | dirty (set by `SaveSoon`) |
| 0x8000 | no `.bak` backup or fallback |
| 0x20000 | location-8 copy is plaintext |
| 0x80000 | never save |

Location codes, from `SettingsManager::Filename` (0x2e50):

| Code | Directory |
|---|---|
| 0 | `/var/merit/settings/never/` |
| 1 | `…/twobutton/` |
| 2 | `…/install/` |
| 0x10 | `…/restore/` |
| 8 | `/usr/local/gamedata/config/` |

`.xml` is appended to the name.

### 3.2 Call sequence

**`Settings::Load()`** calls `SettingsManager::GetInstance()->Load(this)` (0x4f40):

1. A duplicate name sets `flags |= 8` and returns 0.
2. `rec = __dynamic_cast(this, &typeid(Settings), &typeid(abstract_xml_record), -2)`. This is a **cross-cast** through the game's vmi typeinfo into the record's typeinfo (**crash site with placeholder typeinfo**). On failure, `flags |= 8` and return.
3. Try the preferred location (`flags & 0xff`), then 0, 1, 2, 8. For each, `LoadXML` (0x2b00) calls, if the file exists, `rec->abstract_xml_record::ReadFile(path, key or NULL, !(flags&0x8000))`, then `LoadedFrom(path)`. A file found at a non-preferred location is re-saved to the preferred one and the old file removed.
4. Unless `flags & 0x400`: register the name and keep a snapshot, `dynamic_cast<abstract_xml_record*>(rec->clone())` (vptr+0xc).
5. If nothing loaded (or it loaded from location 8), call the virtual `Init()`. Then `if (Changed()) Save()`.
6. If `/var/merit/settings/restore/<name>.xml` exists:
   - `tmp = rec->clone()`;
   - `ReadFile` into `tmp`;
   - `this->Restore(tmp)`. `Settings::Restore` (0x2580) cross-casts and calls `rec->copy(tmp)` (vptr+0x18);
   - delete `tmp`, then unlink the file.

**`Changed()`** (0x2bd0):

- false if flags has 0x80000 or 8;
- true if 0x2000;
- false if 0x400;
- otherwise `snap->not_equal(rec)` (vptr+0x14). Note that `rec` is the subobject at GameSettings+0x14, so the record's `not_equal` has to `dynamic_cast` it back. That works only with correct typeinfo.

**`Settings::SaveNow()`** calls `Save(this)` (0x45d0). Unless flags has 0x80000 or 8, it calls `rec->WriteFile(Filename(name,loc,true), Key(loc,true), !(flags&0x8000), true /*compact*/)` (call at 0x468f), then `snap->copy(rec)` and `SavedAs()`.

**`Settings::Cleanup()`** calls `Unload(this)` (0x5df0). Unless flags has 8 or 0x400:

- **`Save()` unconditionally**;
- unregister;
- delete the snapshot.

**None of the PICRAND/HighRun games call `SaveNow`/`SaveSoon`.** Deck positions and the high run reach disk at `Cleanup` (game exit), or through the manager's dtor, which saves everything for which `Changed()` is true.

**`WriteFile`** (libgendef_xml 0x7540) writes `<path>.tmp`, fsyncs, renames the old file to `<base>.bak.xml` when backup is on, then renames `.tmp` into place. **`ReadFile`** renames an unparsable file to `<path>-<time>` and falls back to the `.bak`.

### 3.3 What libsettings needs from a record

| Member (vptr off) | Called by | Must be correct? |
|---|---|---|
| typeinfo chain | every cross-cast and the `not_equal`/`copy` casts | **yes: the crash** |
| `clone()` (0xc) | snapshot, Restore | yes: a heap copy whose dynamic type is the record |
| `not_equal()` (0x14) | `Changed()` | yes, or early saves are skipped (`Cleanup` still saves) |
| `copy()` (0x18) | Save (snapshot update), Restore | yes |
| `cTag()` (0x20) | root element | yes: `"GameSettings"` |
| `Metadata()` (0x8) | ReadFile/WriteFile | yes. Must be non-NULL; `WriteString` reads metadata `+8` and `+0x10` unconditionally |
| `NewChild/AddChild/GetChildren/ChildName` | ReadFile/WriteFile | yes, for persistence |
| `equal`, `ChildCount`, `AdjustTime`, `AdjustTimeFields`, `eTag` | not called by libsettings or the XML layer | keep sensible |

---

## 4. `RandomizedArrayClass`

### 4.1 ABI [C]

| Item | Value | Evidence |
|---|---|---|
| sizeof | **0x3c** | `new(0x3c)` before every ctor call: spotmatch 0x36730, boxxi, mysteryphraze 0xa4b6/0xa9f4, monstermadness 0x9f0d |
| polymorphic | **no** | only C1/D1 are imported; games call `~RandomizedArrayClass()` and then `operator delete` (mysteryphraze decomp 0x1a4ee) [L] |
| `+0x04` | `int count` | read inline: mysteryphraze `GetPhrasesNotByCat` (decomp 0x1a3a4, `i < *(rac+4)`) and pixmix "picturecycle" debug (decomp ~0x15400) |
| rest | private | no other inline access |

Suggested private layout:

| Off | Field |
|---|---|
| +0 | `PICRAND_record* rec` |
| +4 | `int count` (**fixed**, read inline) |
| +8 | `uchar flag` |
| +0xc | `ushort* values` |
| +0x10 | `PICRAND_record own` (0x14) |
| +0x24 | bookmark `{seed,pos}` |
| +0x2c | `ushort* order` (heap) |
| … | |

### 4.2 Methods

- **`RandomizedArrayClass(PICRAND_record* rec, int count, uchar flag, ushort* values)`** (36 games).
  - **Common form:** in 35 games it is always `(rec, count, 0, NULL)`. `rec` points into the game's GameSettings record (persistent), or into a game-side container's children.
  - **Mystery-phraze form:** the only other form is `MystClass::CreateCatList` (mysteryphraze 0xa4b3): `(this+0xd8, nCats, 1, this+0xb4)`. Here `values` is a `short[]` of non-empty category ids, and `GetNextValue` returns `values[i]` (the caller uses the result directly as a category id). The record at +0xd8 is not persisted.
- **`RandomizedArrayClass(int count)`** (monstermadness only, count = 9, no record).
  - The caller fills `arr[GetNextValue(-1)] = i` for i = 0..8 (`GAME_Grid::InitGrid` 0x6d36). **9 draws must return a permutation of 0..8**, or slots stay uninitialised.
- **`unsigned short GetNextValue(int arg)`.** Every caller zero-extends the result (`movzwl`) [C].
  - **`arg` values:** 86 of 87 sites pass −1. The exception is monstermadness 0x9f3d, `GetNextValue(3)` in kids mode, where only monsters 0–2 may appear. So `arg >= 0` presumably restricts results to `< arg` [?].
  - **Result:** a 0-based index `< count`:
    - spotmatch: `vector[v]`;
    - boxxi: picture `v+1` → `%spics/%s%04d`, hometown `dlbx-%03d`;
    - mysteryphraze: `v + categoryFirstRecord`.
- **`SetBookMark(uchar n)` / `GotoBookMark(uchar n)`.** `n` is always 0.
  - boxxi, pixmix and lookout call `SetBookMark(0)` right after building each deck (boxxi 0xc717, pixmix 0x6869, lookout 0x11822).
  - "Jump to picture N" (debug / unscramble modes) is written as `GotoBookMark(0)`, then N `GetNextValue(-1)` calls (spotmatch `BoardReader::JumpToPicture` 0x35c3a). So a bookmark must restore a cursor that **replays the same sequence** [C].
- **`~RandomizedArrayClass()`:** free; write the state back to `rec`.

### 4.3 Required semantics and a suggested algorithm

The record keeps only `{seed, pos, max}`, so the order of the deck has to be a deterministic function of `(seed, max)` [C, by necessity]:

```
ctor(rec, count, flag, values):
    store; if count <= 0 { rec->max = count; return; }       // boxxi decks with count 0 stay 0/0/0
    if rec->seed == 0 || rec->max != count || rec->pos >= count:
        rec->seed = fresh_seed(); rec->pos = 0; rec->max = count
    order = FisherYates(0..count-1, OwnPRNG(rec->seed))       // own LCG: not rand()
GetNextValue(arg):
    if count <= 0: return 0
    if rec->pos >= count: rec->seed = fresh_seed() (≠ old); rec->pos = 0; reshuffle
    v = order[rec->pos++]          // update rec in place: libsettings persists it
    (arg >= 0: skip values >= arg)
    return values ? values[v] : v
SetBookMark(n):  bm[n] = {rec->seed, rec->pos}
GotoBookMark(n): if bm[n].seed != rec->seed: reshuffle(bm[n].seed); rec->seed, rec->pos = bm[n]
ctor(int count): rec = &own (zeroed), then as above
```

- **Do not touch libc `rand()`.** Games re-seed it right after building decks (boxxi `Randomize(SystemTimer())`).
- **Immediate repeats:** games guard against these themselves (boxxi `GAME_LoadPicture` 0xa2f8 redraws when the value equals the last picture), so a strict permutation is enough.
- **Bit-compatibility:** our sequences need not match the original cabinet's. Only the state shape matters.

---

## 5. `TriviaClass`

**Template.** `MystClass`, shipped inside mysteryphraze.so (and the ht/ps variants), has the same design:

- a dBase question file;
- a per-category index;
- one RAC per category backed by a `PICRAND` child;
- an "adult category 4" flag.

Read it before implementing TriviaClass. Key addresses: ctor 0xa65e/0xa8a4, `ReadData` 0xa1b6, `CreateCatList` 0xa454, `GetRandomCat` 0xa428, `GetPhraseData` 0xcd60, dtor 0xa4ee.

### 5.1 ABI

sizeof **0x29ac** [C]: `new(0x29ac)` in all 8 users (trivia 0x1ac37, powertrivia 0x4554, brainquest 0x1112c, psh2htrivia 0x7e4d, tic_tac_trivia 0x1bfe2, reversetriv 0x10a28, quizshow_new 0x106c8, htquizshow 0xc264). It is not polymorphic [L].

**Constructors:** `TriviaClass(TrivPICRAND_record* rec, char* dbPath, uchar adultCat4, uchar flags, uchar a5)`, plus a `BigTrivPICRAND_record*` overload (reversetriv). Arguments were checked in disassembly:

| Caller | dbPath | adultCat4 | flags | a5 |
|---|---|---|---|---|
| trivia (`CardPool::init`, decomp 0x2ab7a) | `/usr/local/gamedata/gamegraphics/misc/<lang>/trivia2.dat` | gameId==0x4f (G_ETRIVIA) | 1 | 0 |
| powertrivia (decomp 0x144c8) | trivia2.dat | gameId==0x50 (G_ENEWTRIV) | 0x81 | 0 |
| psh2htrivia | blh2h.dat [L] | ? | 0x81 | 0 |
| brainquest | `brainquest/database/gradeN.dat` | 1 | 1 | 0 |
| reversetriv | revtriv.dat | 1 | 1 | 0 |
| quizshow_new (`GetGameDBInfo`, decomp 0x205c8) / htquizshow | `trivia2` or `qsps` | 0 | 1 | 0 |
| tic_tac_trivia | trivia2.dat | 0 | 1 | 0 |

Argument meanings:

- **`rec`** is `GameSettings::GetInstance()+0x1c`, or a child of the game's container.
- **`adultCat4` [L, strong]:** allow category 4. That is "Sex & the Media" in trivia2.dat; MystClass's equivalent skips "SEX".
- **`flags` bit 0 [L]:** DB encrypted. MystClass passes its matching value to `DBFClass::SetEncryption`.
- **`flags` 0x80 [?]:** set only by powertrivia and psh2htrivia.
- **`a5` [?]:** always 0.

**Fields games read inline [C].** The "current question" lives at offset 0:

| Off | Type | Source DB field | Readers |
|---|---|---|---|
| 0x000 | `char[0xfe]` | QLINE1 | all |
| 0x0fe | `char[0xfe]` | QLINE2 | all |
| 0x1fc + i×0x8c | `char[0x8c]` ×4 | ANSWERA..D. **[0] is the correct answer**, unshuffled | all |
| 0x42c | `char[0x40]` | TOPIC | trivia, powertrivia |
| 0x46c | `char[0xfe]` | INFO1 | trivia, powertrivia |
| 0x56a | `char[0xfe]` | INFO2 | trivia, powertrivia |
| 0x668 | `int` | `atoi(GIF)`. Powertrivia loads `misc/jpegs/g%04ld` from it (decomp 0x17962) | powertrivia |
| 0x29a9 | `uchar` | Callers compute `needsConv = T[0x29a9]^1` and, if it is set, pass strings through `TextUtils::MeritStringToUTF8`. The shipped DBs are single-byte (Latin-1/CP1251), **so leave it 0** [L] | all |

Bytes 0x66c–0x29a8 are free for internal state: the DB handle, the per-category `{count, start, RAC*}` table, the per-rating tables and the GetRandomAns state.

**Methods:**

- **`int GetQuesData(int cat, int rating)`.** Draws the next unused question and fills the struct.
  - `rating` −1 means any rating. tic_tac passes a difficulty (3 on the AI's turn, 0x173b8); the quizshows pass a level from a table.
  - **Returns the absolute DB record number [C].** tic_tac and powertrivia send it to network peers, which call `ReadData(recno)` (tic_tac decomp 0x2738e, 0x1d9aa; powertrivia decomp 0x176e0).
  - Suggested implementation [L], modelled on `MystClass::GetPhraseData`:
    - if `cat` is invalid or empty, use `GetRandomCat()`;
    - for rating −1, `rec = catStart[cat] + CatRandRAC[cat].GetNextValue(-1)`;
    - otherwise use the rating sub-range and its RatingRand RAC.
- **`ReadData(int recno)`:** reads the record into the struct and resets the `GetRandomAns` state (2 users) [L].
- **`uchar GetRandomAns(uchar n)`:** called n times per question (usually 4). Each call returns a distinct random slot in `0..n-1`, and the first call is where the correct answer goes (powertrivia decomp 0x176e0, tic_tac 0x1741d) [C usage]. trivia and quizshow shuffle the answers themselves.
- **`int GetRandomCat()`:** next category from a no-repeat cycle over the non-empty categories, with category 4 only if `adultCat4` [L]. `Quizshow_QuestionBank::LoadEnabledCats` calls it until it sees a repeat in order to enumerate categories, so it **must cycle** [C]. MystClass does this with a RAC over a `ushort[]` of category ids.
- **`int GetAmountOfQuestionsInCatX(short cat)`:** the category's question count. brainquest skips categories that return 0 (0x10baa); tic_tac disables categories with fewer than 2 (decomp 0x18294). It probably returns 0 for category 4 when adult content is off [?].
- **`~TriviaClass()`:** deletes the RACs and the DB handle.

### 5.2 Question database format [C]

`trivia2.dat`, `revtriv.dat`, `blh2h.dat`, `vwhiz.dat`, `gradeN.dat`, `myst.dat` and `mpps.dat` are **dBase III files (version 0x03), XOR-encrypted with a 20-byte repeating key**:

```
KEY = 81 88 83 85 87 82 84 9e 94 9d 91 93 8b 99 96 9a 98 90 8c 92
plain[i] = cipher[i] ^ KEY[(i + 2) % 20]     // file offset 0x12 uses KEY[0]
```

Every listed file was decoded with this key. The `*.dbf` files (newtriv.dbf, h2htriv.dbf…) are plain translation tables, not question banks.

**Trivia layout.** Header 417 bytes, record 263 bytes. Field widths: QLINE1 35, QLINE2 35, ANSWERA–D 25 each, TOPIC 15, INFO1 35, INFO2 35, GIF 4, CATEGORY 2, RATING 1. They map 1:1 onto §5.1. A backquote stands for an apostrophe (MystClass converts 0x60 to `'`).

**Index records.** The first N records index the categories, one per category id; a blank record means the category is empty. N is 39 for trivia2, blh2h, vwhiz and gradeN, and 140 for revtriv. Questions start at record N, contiguous per category and sorted by rating. In an index record the values are numbers:

| Field | Meaning |
|---|---|
| QLINE1 | total questions in the category |
| QLINE2 | start (= rating 0 start) |
| ANSWERA | rating 0 count |
| ANSWERB / ANSWERC | rating 1 start / count |
| ANSWERD / TOPIC | rating 2 start / count |
| INFO1 / INFO2 | rating 3 start / count |
| CATEGORY | category id |

Example, trivia2.dat record 0: total 842, start 2034, counts 0/97/429/316.

Category counts:

- trivia2.dat (english) has 7 categories and 5133 records.
- gradeN.dat files have 5–6 categories, all rating 0.
- revtriv.dat has 85 categories.

**myst.dat.** Header 225 bytes, record 81 bytes. Fields: CATEGORY 3, HINT 25, PHRASE1..4 13 each. Records 0..11/12 are the index:

- CATEGORY is a code: GEN, SPO, MUS, ENT, SEX (= category 4), POT (= category 5, which `CreateCatList` always skips).
- HINT holds the count and PHRASE1 the start record.
- The MystClass ctor reads them with `atoi`.

---

## 6. `ChainRestClass::GetPicFileName(xml_gameinfo::GameIds, int)` (21 games)

- **Calling convention [C].** Non-static. The caller passes `this = *(ChainRestClass**)(MegacGlobals::GetInstance()+8)`, the GameId of the chain variant, and `0xff`, then cleans 0xc bytes. Call sites: boxxi `GAME_LoadPicture` 0xa438, pixmix `Debug` 0x5192, lookout `GObject::DrawScreen` 0x8f60, cardbandits `GAME_GetJPEGName` 0x9f99.
- **GameIds** (from libenums `enum_GameIds_tag`):

  | Id | Name |
  |---|---|
  | 0xad | g_chain_boxxi |
  | 0xae | g_chain_lookout |
  | 0xaf | g_chain_pixmix |
  | 0xb0 | g_chain_ecardbandits |
  | 0xb1 | g_chain_trivia |

- **Return value [C].** A `const char*` full picture path, used as `snprintf(buf, 0xff, ret)` and then `Bitmap::LoadJPEG(buf)`. It must therefore contain no `%`.
- **When it is reached.** Only when the game runs as the chain variant (`MegacGlobals+0x2038` == one of those ids). The image has no chain-restaurant content, so a stub that returns a static path is enough [?].
- **Infinite loop hazard.** boxxi's `GAME_LoadPicture` loops forever if the load keeps failing, so don't launch games under chain GameIds.

Also loader-only and found alongside these classes (out of scope here):

- **`DBFClass`.** Members: ctor, `SetEncryption(bool)`, `OpenDBF`, `GetRecord(int)`, `nGetField`, `GetNumRecords`, `CloseDBF`. sizeof 0x220 (`new(0x220)`, mysteryphraze 0xa90e); virtual dtor at slot 1. Used by mysteryphraze ×3, g_mystery_phraze_hd, h2hgenderbender, hollywood_match, safari and snapshot. It reads the XOR dBase format of §5.2.
- **`TextUtils::MeritStringToUTF8(const char*)`.**

---

## 7. Open questions

| Question | Impact |
|---|---|
| PICRAND's own `cTag()`/`eTag()` values; signedness of seed/pos/max | none for compatibility (never serialised) |
| Child element names for PixMix (8), PhotoHunt (11) and Myst (14) children; HighRun's field name (`HighRunLength`?) | only matters for loading old cabinet saves; none ship for these games |
| `RatingRand[49]` indexing, `DBRand` purpose, BigTriv's extra child | old-save compatibility; question choice by rating |
| RAC `flag` argument and `GetNextValue(arg >= 0)` | monstermadness kids mode only |
| TriviaClass `flags & 0x80`, `a5`, and whether category 4 is hidden from `GetAmountOfQuestionsInCatX` | powertrivia/psh2htrivia details |
| `ChainRestClass` picture paths | chain variants only; no content ships |
| `SettingsManager::AdjustTime` → record `AdjustTime` path | none (no time fields) |
