# Gash layout scripts and LinuxWorldClass (breakout, motormatch, racepoker)

Three legacy games don't use the usual `DOSLinuxWorld` setup. They build a `LinuxWorldClass` world
and lay out their screens from an XML-like **Gash** script (`layout.xml`) that the loader parses
into named sprites: `Gash`, `TopGash`, `DoParse`, `Gash::LocateSprite`. Their widgets
(`OneBmpTimer`, `ScoreDisplay`, exit and help buttons) come from that script.

| game | library | game class | world |
|---|---|---|---|
| breakout | `games/breakout/lib/breakout.so` | `BreakOutClass` (new 0x908) | `new(0x228)` + `LinuxWorldClass(true)` at 0x13ac7 |
| racepoker | `games/racepoker/lib/racepoker.so` | `RacePokerClass` (new 0xcc) | `new(0x228)` + `LinuxWorldClass(true)` at 0xc91c |
| motormatch | `games/motormatch/lib/motormatch.so` | same `RacePokerClass` | same |

**motormatch and racepoker are the same binary.** Their dynamic symbol tables are the same except
`RunMotorMatch`, which is `.data` (1) in motormatch and `.bss` (0) in racepoker. Every code address
is the same in both. `objdump` diffs show only different `%ebx`-relative data offsets. All
racepoker addresses below therefore apply to motormatch unchanged. The motormatch-only behaviour
is `if (RunMotorMatch)` branches: other sounds, translations "motormatch", and
`FileLoc = …/gamegraphics/motormatch/`.

Everything here comes from the game libraries only: `objdump -d -C -R`, `readelf -rW`,
`nm -D`, vtables dumped with `tools/vtdump.py`, a GOT/string annotator, and the layout files in
`games/*/data`. The loader binary was not used.

Conventions are the same as [sprite-engine.md](sprite-engine.md): ELF addresses, i386 cdecl
with `this` first, and Itanium ABI. Confidence tags:

- **[C]** confirmed from code, with game and address
- **[L]** likely, inferred from several consistent sites
- **[?]** guess or unresolved

"rp" means racepoker (and so motormatch) and "bo" means breakout.

---

## 0. Why the games crash today

`libmega_stubs.so` defines every unresolved data symbol as 64 KB of zeroes and every function as
"log once, return 0". So:

1. `LinuxWorldClass::LinuxWorldClass(bool)` is a stub. **The game never stores the new world
   anywhere.** It relies on the constructor to publish itself in `MWorld` (§2) [C: rp 0xc92d,
   bo 0x13adc; neither has a store after the call]. `MWorld` stays NULL.
2. `DoParse` is a stub, so `TopGash` stays NULL and every `Gash::LocateSprite` returns 0.
3. rp `RacePokerClass::RacePokerClass(int)` at 0xacdb–0xace8 does
   `List::Push(MWorld->+0x60->+0x8c, new RacePokerSigHand)`. That reads `NULL+0x60` and faults
   at **0xace5** [C]. breakout does the same at 0xdf7f–0xdf8d (`BreakOutClass::LoadGraphics`).
4. Behind that, about 30 `LocateSprite` results are dereferenced without a NULL check (§7), so a
   parser that finds nothing would only move the crash.

---

## 1. Lifecycle

```cpp
// rp __EntryPointV12 0xc8b8 (bo 0x13a3e is the same shape)
for (i = 0; i < G->+0x24 /*players*/; i++) { HighScores(); PlrScore[i] = 0; }
LinuxWorldClass* w = new LinuxWorldClass(true);      // operator new(0x228); no vt[6] init call
if (RunMotorMatch) sprintf(FileLoc, "%s%s/", "/usr/local/gamedata/gamegraphics/", "motormatch");
//  bo: always sprintf(FileLoc, "%s%s/", ".../gamegraphics/", "breakout")
MyRacePoker = new RacePokerClass(-1);                // ctor: DoParse(layout), LocateSprite..., handler push
MyRacePoker->vt[6]();                                // PlayOneGame()   (LinuxGameClass slot 6)
delete MyRacePoker;                                  // vt[1]
if (w) delete w;                                     // vt[1]  (deleting dtor)  -- world FIRST
if (TopGash) delete TopGash;                         // vt[1]  -- Gash tree AFTER the world
//  bo additionally: TopGash = 0;  QueFlush(0); MouseManager::CheckLoc(...)
```

- No `vt[6](0,1)` init and no `vt[7]` shutdown call is made on a LinuxWorldClass, unlike
  DOSLinuxWorld games. The `(bool)` constructor must leave the world fully usable [C rp 0xc92d,
  bo 0x13adc].
- **Teardown order [C]:** the world is deleted before `TopGash` (rp 0xc9b7 → 0xc9c7;
  bo 0x1414c → 0x14167). The Gash destructor runs after the world, and so after the world's
  sprites, are gone. It must not touch sprites (§5.4).
- The game never restores `MWorld` or `MegacGlobals+0x207c`.
- Main loop [C rp 0xd78d, bo 0x138ac]: `while (MWorld->+0x19c <= 1) { …; MWorld->PlayMovie(1, …); }`.

---

## 2. Global data symbols

| symbol | C type | size | use | conf |
|---|---|---|---|---|
| `MWorld` | `LinuxWorldClass*` | 4 | The current world. Read through the GOT as `*(*GOT[MWorld])`: 105 sites in bo, 64 in rp. **Set by the LinuxWorldClass constructor**; games never write it. | C (rp 0xacdb, 0xb102, 0xd78d; bo 0xdf7f, 0x13bc3) |
| `TopGash` | `Gash*` | 4 | The root of the parsed script. Set by `DoParse`; `this` for every `Locate*` (`push (GOT[TopGash])`). Deleted through vt[1] at exit; bo then writes 0. | C (rp 0xad23, 0xc9c7; bo 0xe1a3, 0x14167) |
| `SSIG_NAMES` | `char SSIG_NAMES[N][20]` | 20·N | A table of signal names, indexed by `Group::SpriteSignalType`. rp logs `"Unhandled RacePokerSigHand %s"` with `SSIG_NAMES + 0x14*type` (`imul $0x14,%esi` + `add GOT[SSIG_NAMES]`). It is an **array object, not a pointer table**. The script parser uses it to turn `SSIG_USERn` text into numbers (§3.1). | C (rp 0xa65b) |
| `HelpOnScreen` | `int` | 4 | Non-zero while the engine's help screen is showing. Games only read it (`cmpl $0,(…)`). rp: the timer resume logic skips while it is set (0xcd4b), and the computer player `RPPlayer::Play` does nothing while it is set (0xfeb2). 0 is fine standalone. | C read, L meaning |

### 2.1 Signal numbers (`SSIG_USERn = 8 + n`) [L, strong]

Two games independently fit `SSIG_USERn → 8+n`, matching what the layouts bind against what the
handlers do:

| name | value | bo binding → `BreakOutClass::Signal(SpriteSignal*)` (jump table 0xa281) | rp binding → `RacePokerClass::Signal(type,int)` (jump table 0xa519) |
|---|---|---|---|
| SSIG_USER2 | 0x0a | Timer `HurrySignal` → 0xa49d shows "Hurry Up!" | Timer `HurrySignal` → 0xa563 HurryUpMessage1/2 |
| SSIG_USER5..8 | 0x0d–0x10 | – | `SelectZone1..4` → horse 0..3 (0xa524/0xa530/0xa53c/0xa73e) |
| SSIG_USER9 | 0x11 | – | `Intro` window default → ignored (0xa759) |
| SSIG_USER10 | 0x12 | `HelpBut` `ButtonSignal` → InitHelpScreen1 (HOWTOPLAY) | – |
| SSIG_USER11 | 0x13 | `Inventions` → InitHelpScreen2 (INVENTIONS) | `HitCard(0)` (signal set by the game via `CardHand::SetPos`) |
| SSIG_USER12/13 | 0x14/0x15 | `left`/`right` → no-op (0xa671) | `HitCard(1)`/`HitCard(2)` |
| SSIG_USER14 | 0x16 | paddle touch zone (game-made sprite, +0xa0 = 0x16) | – |
| SSIG_USER15 | 0x17 | `PLAYNOW` → close help, `OneBmpTimer::Resume` | – |

Indices 0–8 are presumably engine signals (none is referenced) [?]. Only `SSIG_USER*` names
appear in any layout under `games/` or `cabinet/root/usr/local/gamedata`. **Minimal table:**
rows 0..0x1f, with row 8+n = `"SSIG_USER<n>"` for n = 1..23 and anything for rows 0..8 (for
example `"SSIG_NONE"`, `"SSIG_SYS1"`…). Every value the engine can raise must have a row,
because rp formats `SSIG_NAMES[type]` for any unhandled type.

---

## 3. The Gash script format

### 3.1 Files

| game | path passed to `DoParse(path, false)` | site |
|---|---|---|
| bo | `/usr/local/gamedata/gamegraphics//breakout/layout.xml` (literal, double slash) | `LoadGraphics` 0xe1ba / 0xe28c, called once; only for the first `BreakOutClass` in 2-player mode |
| rp, mm | `sprintf("%sgamegraphics/racepoker/layout_uk.xml", "/usr/local/gamedata/")` if `!RunMotorMatch && Country()==3`, or if `Country()==4`; otherwise `…/racepoker/layout.xml` | ctor 0xacbf (and the C1 copy at 0xdf9f) |

- Paths are **absolute cabinet paths**. They must go through the port's file redirection
  (as other `/usr/local/gamedata` opens do).
- **motormatch loads racepoker's layout** [C, the string is the same in both binaries]. On the
  cabinet both directories exist (`cabinet/root/usr/local/gamedata/gamegraphics/racepoker/`).
  In the port, `games/motormatch/data/usr/local/gamedata/gamegraphics/` contains only
  `motormatch/`, so the parse fails and motormatch crashes on unchecked lookups.
  - Fix: provide `gamegraphics/racepoker/layout.xml` in motormatch's data, as a copy or symlink
    of `motormatch/layout.xml`. The two differ only by a `NameArabic` text in `HiScoreBox`.
- `layout.xml` and `layout_uk.xml` differ in one line (`intro/intro.pcx` → `intro_uk.pcx`).
- Graphics named in the script resolve against the game's graphics directory (`FileLoc`), the
  same way `WorldClass::LoadBmp` resolves names. Every graphic referenced by the three layouts
  exists as `<name>.spr[.gz]` in both racepoker's and motormatch's directories, and in
  breakout's [C, checked].

### 3.2 Syntax (from the files; the parser itself is not in any game)

The format is not real XML. It is a tag stream:

```
<comment> … </comment>            ignored (also <Comment>, <info>…</info>)
<Tag>Name                          element open; the NAME is the text up to the next '<', trimmed
    <Attr>value</Attr>             leaf attribute; value trimmed ("<Font> Bureau</Font>")
    <Tag>Child … </Tag>            nested element
</Tag>
<EOF>EOF</EOF>                     end marker (rp/mm only)
```

- **Tags are case-insensitive.** The files mix `<sprite>`/`<Sprite>` and contain
  `<active>On</Active>` (open and close tags differ in case) [C, files].
- Names may be empty (anonymous decoration), may contain spaces (`Exit in Help Menu`,
  `intro text`) or slashes (`intro/slider.tgaAnimation`), and **may repeat**: rp has two
  `Window 2_horse_line` and two `3_horse_line`, and many `White_Line`s.
- Values are strings, integers (`-240`), decimals (`25.2`, `2.5`) and `On`/`Off`/`NO` (any case).
- Several attributes can share a line (`<Xcoord>1</Xcoord> <Ycoord>422</Ycoord>`).

### 3.3 Elements

| tag | engine object (what `LocateSprite` returns) | attributes seen | used by |
|---|---|---|---|
| `Window` | a container Sprite; children are relative to it | Active, Xcoord/Ycoord/Zcoord, graphic (rp `Help_Screen`), DefaultSpriteSignal (rp `Intro`), info | bo, rp |
| `Sprite` | a Sprite with `graphic` (or none) | Active, graphic, DefaultSpriteSignal, X/Y/Zcoord, info, Comment; may contain Text, ScoreBox, Sprite | bo, rp |
| `Text` | a Sprite carrying a `String` (§8.6) | String, Font, PointSize, Bold, Align, Color *or* ColorRed/ColorGreen/ColorBlue, Spacing, Xsize/Ysize, X/Y/Zcoord, Active | bo, rp |
| `ScoreBox` | `ScoreDisplay` (§8.2) | String (`#,###,###` digit mask), Font, Bold, PointSize, ColorRed/Green/Blue, Align, X/Y/Zcoord, Xsize/Ysize | bo, rp |
| `Timer` | `OneBmpTimer` (§8.1) | graphic, TotalTime (minutes: 2.5, 3, 1), HurrySignal, ShowGameOver (NO), Direction (1 bo / −1 rp), Active, X/Y/Zcoord, info | bo, rp |
| `Button` | a clickable Sprite raising `ButtonSignal` | graphic, ButtonSignal, X/Y/Zcoord; bo `HelpBut` also has HelpBkg, HelpExitButton | bo |
| `ExitButton` | a clickable Sprite: quit (§8.3) | graphic, ExitSound (`quit.wav`), X/Y/Zcoord | bo, rp |
| `HelpButton` | a clickable Sprite: engine help screen (§8.4) | graphic, HelpBkg, HelpExitButton, HelpText, X/Y/Zcoord | rp |
| `HiScoreBox` | a Sprite whose `Text` children `Name` / `NameArabic` / `Score` the engine fills with the high score ("Generated Automatically if the text is named Score") | X/Y/Zcoord, Text children | bo, rp |
| `Background` | sets the world background (`SetBack`) | graphic | bo (`Bkg`) |
| `HotKey` | (debug) attribute block applied to its parent's sprite (§5.3) | – | rp code only, in no layout |

**Coordinates and z [L].** A child's X/Y are relative to its parent element. In rp, the
`Scores` window is at (536,56) and `Player2ScoreBox` is at Y=31 inside it. The Gash sprite tree
is built with real parent links (`Sprite(bmp, flags, parentSprite)`): rp `ChangeInstructText`
0x863e creates a sibling of `Instruction_Box` with `new Sprite(0, 0, box->+0x8)`, which is
Sprite's Group owner field (the parent sprite), and positions it with the box's own z
(`box->+0x48`) [C].

Z must **accumulate down the tree** (absolute z = sum of ancestors' Zcoord). Otherwise the rp
`Intro` window (Z 23000) could not cover the track (`Bottom_fence` Z 2000, `Timer` Z 5000) while
its children use Z 1490/1500, and bo's top bars (window Z 20000, children Z 900) would sit under
the play field. Elements without a Zcoord inherit their parent's.

**Active.** `Off` means the sprite is created disabled (flag 0x80). Disabling a Window must hide
and un-click its whole subtree, through every nesting level. rp disables `Intro`, which contains
`Select` › `PostSelection` › `SelectPrompt1…` [C rp 0xcac1]. bo swaps the ONEPLAYER/TWOPLAYER
windows [C bo 0xe1ea–0xe220].

**Signals.** An element with `DefaultSpriteSignal`/`ButtonSignal` gets that value in `Sprite+0xa0`
and is clickable (flags |= 0x100). rp toggles 0x100 on the `SelectZone%d` sprites itself (0x60ea
`orl $0x100`, 0x60f3 `andl ~0x100`). The engine's `SpriteClick` then raises `+0xa0` (§9) [L].
Whether a Window's `DefaultSpriteSignal` is inherited by its children is unknown [?]. For rp's
`Intro` it would only raise USER9, which rp ignores.

**Graphic-less clickable sprites [?].** rp's `SelectZone1..4` have no `graphic` and no size, yet
they are the horse-selection touch targets, and rp never calls `SetSize` on them (every rp
`SetSize` site is accounted for). The original engine must size them somehow. The
`DarkHorseSelect%d` sprites at the same coordinates are 119×104
(`intro/darkhorse1.spr.gz`). Use one of two heuristics: give a graphic-less sprite that has a
signal the size of the next same-position sibling with a graphic, or take Xsize/Ysize when
present.

---

## 4. LinuxWorldClass

### 4.1 Size and type

- `operator new(0x228)` [C rp 0xc91c, bo 0x13ac7]. Constructor `_ZN15LinuxWorldClassC1Eb`, always
  called with `true`. The meaning of the bool is unknown [?].
- **It is a WorldClass.** Games call the non-virtual `WorldClass::LoadBmp`, `PlayMovie`,
  `ShowAll`, `SetBack`, `GetFrame` and `FrameNumber` with `this = MWorld` [C: rp 13 LoadBmp and 9
  PlayMovie calls, bo 70 LoadBmp calls]. So `LinuxWorldClass` must have exactly WorldClass's
  layout for 0..0x207, which means `class LinuxWorldClass : public WorldClass` (0x208) plus 0x20
  bytes of its own. No game touches 0x208..0x227 [C: complete scan of `MWorld`-based accesses].
- No game imports its vtable or typeinfo. The only virtual call is vt[1], the deleting
  destructor [C rp 0xc9c1, bo 0x14161].

### 4.2 Constructor duties

1. Run `WorldClass::WorldClass()`, including `fps` (+0x17c), the clock and the sprite set.
2. `MWorld = this` [C, §0].
3. **`MegacGlobals+0x207c = this`** [L, strong]. These games also use the MegacGlobals world:
   - rp calls `WorldClass::SetBack(G->+0x207c, "intro/intro", 0)` from the constructor (0xa933).
   - bo calls `LoadBmp`/`ShowAll`/`DeleteBmp` through `G->+0x207c` (14 sites) and mixes
     `Sprite::LastEventTime()` with `G->+0x207c->+0x170` (0xb114).

   Our sprite engine also registers new sprites in `W() = G->+0x207c`. If the two worlds
   differed, sprites would be registered in a world that `MWorld->PlayMovie` never runs.
   Save the previous value and restore it in the destructor, because the game won't.
4. Create the **root sprite** at +0x60 (§4.4). Its +0x04 child list and +0x8c handler list must
   already be valid `List*`s.
5. Set +0x19c = 0.

The destructor deletes the remaining sprites (our `~WorldClass` already does). It must leave the
Gash tree alone, which is deleted afterwards, and restore `MWorld` (NULL or previous) and
`G->+0x207c`.

### 4.3 Fields games touch

| off | type | access | meaning | conf |
|---|---|---|---|---|
| +0x00 | vptr | vt[1] | deleting dtor | C |
| +0x60 | `Sprite*` | read | **root sprite** (§4.4) | C rp 0xace5, 0x651e, 0x65f9, 0x6683; bo 0xdf8a, 0xe4a1 |
| +0x170 | u32 | read | world clock in ms; the same clock `OneBmpTimer` +0xe0 uses | C rp 0x9d6c, 0xcc16, 0xcd9b, 0xfe8d; bo 0xb11a (via G) |
| +0x17c | int | read | fps. **Used as a divisor** (`idivl` rp 0x8663…, `1000/fps`) and must not be 0 | C |
| +0x19c | int | r/w | **game state**, see below | C |

**+0x19c game state** [C for game-side reads and writes; L for meanings]:

| value | written by | meaning |
|---|---|---|
| 0 | game (rp 0xca2a at PlayOneGame entry, 0x9064; bo 0x1170f…) | playing |
| 1 | **the engine only**. rp reads `==1` at 0xcff8 and 0xd22b but never writes 1 | **timer expired**. rp then runs its time-up or ContinueControl path (0xd22b → 0xd28a). bo ignores 1 (its loop continues while ≤ 1). So `OneBmpTimer` sets `MWorld->+0x19c = 1` when it reaches 0 [L] |
| 2 | game (rp 0x9082, 0xd08b; bo 0x13566) | round or game over |
| 3 | rp on ESC (0xd3aa after `G->+0x22dc == 0x1b`); bo only when `SetStartLevel` fails (0x113b5). **bo has no ESC check**, so the engine `ExitButton` is bo's only way out and must set this value [L] | quit |
| 4 | game (rp 0x9d14, 0xd362; bo 0x13193) | time-up handled / end |

**Conflict with our WorldClass:** `src/legacy/sprite.h` names +0x19c `frozen`, and
`WorldClass::frame()` drops all touches while it is non-zero (sprite.cpp:928). In these games
that freezes input in states 1–4. State 1 matters: rp's continue prompt is shown while the state
is 1. For a LinuxWorldClass, either don't treat +0x19c as a click freeze or only freeze for ≥ 2 [L].

### 4.4 The root sprite (`MWorld->+0x60`)

A plain enabled `Sprite` at (0,0) with no bitmap, created by the world. Games use it as:

- **the signal sink**: `List::Push(root->+0x8c, new XxxSigHand)` [C rp 0xace8, bo 0xdf8d];
- **a parent**: `new Sprite(0, 0x100, root)` for the rp card touch zones (`CardHand::SetPos`
  0x6529); `new Sprite(0, 0x900, root)` for the bo full-screen paddle zone (0xe4ac);
- **a draw list**: `List::Push(root->+0x04, card)` for rp `NewCardClass` (0x65ff, 0x6689). Those
  cards are built with `parent = 0` and pushed by hand.

Rendering implication: everything parented or pushed under root must still be **z-sorted
globally** with the top-level sprites. If root is drawn as an ordinary parent (z 0, children in
its `DoDraw`), all its children land beneath every top-level sprite. Treat root as transparent,
flattening its subtree into the global z order. The Gash tree (§3.3) should hang under root too,
so that clicks on Gash sprites bubble to `root->+0x8c` (§9).

---

## 5. Gash

### 5.1 Layout

```
class Gash : public Group        // [L] polymorphic; +4/+8 match Group's fields
  +0x00 vptr                     vt[1] = deleting dtor [C rp 0xc9d9, bo 0x14179]
  +0x04 List*  daughters         child Gash list; rp logs "Has DaughterList" when non-NULL [C rp 0xd575]
  +0x08 Gash*  parent            [L] rp hotkey path reads parent->+0x18 as the sprite to animate
  +0x0c..+0x14 ?                 (tag / name / attributes; never touched) [?]
  +0x18 Sprite* sprite           the sprite built for this element [L]: LocateSprite == LocateGash()->+0x18;
                                 rp writes LocateGash("Timer",0)->+0x18 = 0 in MegaLink mode [C rp 0xafe8]
  size: unknown (never allocated by games)
```

Games never construct a Gash, never call its virtuals other than the destructor, and never read a
`SpriteSignal`'s `Gash*` (+0x18) [C: bo `Signal(SpriteSignal*)` reads only +0xc; rp
`RacePokerSigHand::DoIt` 0x10988 reads only +0xc and +0x10].

### 5.2 Functions

| signature (mangled) | conf | semantics |
|---|---|---|
| `void DoParse(char* path, bool)` `_Z7DoParsePcb` | C | Parse the script and **create every sprite immediately**: results are used right after the call (rp `ProcessAssignedString` on Text sprites, then `->+0x60`). Create `TopGash` if NULL. The bool is always 0 and the return value is never used [C bo 0xe1ba, rp 0xacbf]. Must not fail for the shipped files (see §7 for what crashes otherwise). |
| `Gash* Gash::LocateGash(char* tag, char* name)` `_ZN4Gash10LocateGashEPcS0_` | C | Depth-first search of `this`'s subtree for an element of type `tag` named `name`. `name == NULL` means the first element of that type [C rp 0xafdc `("Timer", 0)`]. Returns NULL if nothing matches. rp only. |
| `Sprite* Gash::LocateSprite(char* tag, char* name)` `_ZN4Gash12LocateSpriteEPcS0_` | C | `LocateGash(tag,name)` → its `+0x18` sprite, or NULL. The returned object's class follows the tag: `Timer` → `OneBmpTimer*`, `ScoreBox` → `ScoreDisplay*`, the others → `Sprite*` (subclasses for the button tags). Called 19× (bo) and 68× (rp). |
| `void Gash::ParseSpriteAttribs(Sprite*, Gash::GashFlags)` `_ZN4Gash18ParseSpriteAttribsEP6SpriteNS_9GashFlagsE` | C site, ? body | Apply this element's attribute daughters (graphic, coordinates, …) to an existing sprite. Called once, in rp's debug hotkey path with flags 0 (0xd5ed). Since no layout has a `HotKey`, a no-op is fine. |

**Matching rules:**

- **Tag:** case-insensitive [L]. The files mix cases, and the games pass `"Sprite"`, `"Window"`,
  `"Text"`, `"ScoreBox"`, `"Timer"`, `"ExitButton"`, `"HelpButton"`, `"HiScoreBox"`, `"HotKey"`.
- **Name:** probably case-insensitive [L]. For language 0x13, rp looks up `"leftdraw"`,
  `"rightdraw"`, `"leftdrawbold"` and `"rightdrawbold"` (0xad9b–0xae7c), resizes them to 65×15
  and moves two of them to (130,310). Elsewhere it looks up `"LEFTDRAW"`/`"RIGHTDRAWBOLD"` in
  upper case (the layout's spelling), and that code only does something if matching ignores
  case. Both styles are NULL-checked, so either rule is crash-safe.
- **Duplicates [?]:** rp's track has two `Window 2_horse_line`. The first is `Active Off` and
  the second `Active On`, and both put their line at absolute y = 127. For 3 or 4 players rp
  calls `Disable(LocateSprite("Window","2_horse_line"))`, then `Enable` on the `3_` or `4_`
  window (0xbf5f/0xbf83, 0xc140/0xc164). Only a lookup that returns the **later** duplicate
  hides the visible line. First-match leaves a stray line, which is cosmetic only.

### 5.3 Hotkeys (rp `PlayOneGame` 0xd537–0xd6a5, bo `LinuxGameClass::CheckHotKey`) [C site]

```cpp
char k[2] = { G->+0x22dc, 0 };
Gash* g = TopGash->LocateGash("HotKey", k);
if (g) {
  Sprite* s = g->parent(+8)->sprite(+0x18);
  if (g->daughters(+4)) { if (s) g->ParseSpriteAttribs(s, 0); }
  else if (s) { s->Enable(0); s->RunAnim(s->+0x60, 0, 999999, 1, 0, 1.0f, 0, -1); … }
}
```

Debug only, with no `HotKey` in any shipped layout. bo's `LinuxGameClass::CheckHotKey(char)`
(non-virtual, called from `LevelEditor` 0x12280, result unused) is the same feature, so a no-op
is fine.

### 5.4 Ownership

Gash sprites are ordinary world sprites, so `~WorldClass` deletes them first (§1). `~Gash`
should free only the Gash tree and strings, or check that each sprite is still registered before
touching it.

---

## 6. Engine-side behaviour per tag (what the parser builds)

| tag | build | flags | extras |
|---|---|---|---|
| Window | `new Sprite(graphic or 0, flags, parentSprite)`, SetCord(x, y, absZ) | 0x80 if Off | children use it as parent |
| Sprite | `new Sprite(LoadBmp(graphic), flags, parent)` | 0x80 if Off; 0x100 if it has a signal | `+0xa0 = DefaultSpriteSignal` |
| Text | `new Sprite(0, …)` + `AssignString(new String(font, size, text, align, spacing, r,g,b, …, Bold?0x08:0))` + `SetSize(Xsize, Ysize)` + `ProcessAssignedString()` | | **`+0x60` must be a valid Bitmap** once it exists [C rp 0xaf09/0xaf23 read `->+0x60` of `RIGHTDRAWBOLD`/`RIGHTDRAW` and hand both bitmaps to `SuperFlash`]. `ChangeString` must work, so `+0x64` must be non-NULL. |
| ScoreBox | `ScoreDisplay` | | §8.2 |
| Timer | `OneBmpTimer` | | §8.1 |
| Button | Sprite, `+0xa0 = ButtonSignal`, 0x100 | | |
| ExitButton | Sprite subclass | 0x100 | §8.3 |
| HelpButton | Sprite subclass | 0x100 | §8.4 |
| HiScoreBox | Sprite + Text children; fill `Name`/`Score` from `HighScoresManager` | | |
| Background | `MWorld->SetBack(graphic, 1)` | | |

`Align`: left, center, right map to the String justification codes 1, 4, 2
([sprite-engine.md §5.4](sprite-engine.md)). `Color` names (White, Yellow, Red, …) and the numeric
`ColorRed/Green/Blue` follow the **offset-from-white** convention: bo's `PlayerName` uses
(0,−23,−135), a warm yellow, and the scores use 255 (clamped to white) [L]. Fonts: Bureau,
Betula, Olea.

---

## 7. Named elements each game requires

"unchecked" means the result is dereferenced or passed on with no NULL test. A missing element
crashes there.

### breakout (`BreakOutClass::LoadGraphics` 0xde48, `InitHelpScreen1/2`)

| tag, name | stored / use | unchecked |
|---|---|---|
| Timer `Timer` / `Timer2` / `Timer3` / `MeritTimer` (by mode) | `game+0xc`; then `vt[0x28]` Reset (0xe3c0), `Halt`/`Resume`/`TimeLeft`/`SetTimeLeft` | **yes** |
| ScoreBox `PlayerScore1` / `PlayerScore12` / `PlayerScore2` / `PlayerScore1m` | `game+0x10` (2nd player `+0x14`); `Set`/`Increase`, reads `+0x178` | **yes** |
| Window `ONEPLAYER`, `TWOPLAYER` | Enable/Disable | checked (except 0xe2bd, value unused) |
| Window `MERIT` | Enable (0xe37a) | **yes** (Merit mode only) |
| Text `PlayerName` | ChangeString(HighestName) | checked |
| HiScoreBox `HiScoreBox` | `game+0x1c` | stored |
| Window `HOWTOPLAY`, `INVENTIONS` | Enable (help screens) | **yes** (stored, then Enable) |

### racepoker / motormatch

| tag, name | where | use | unchecked |
|---|---|---|---|
| Text `HorseName1..4` | ctor 0xad33 | ChangeString("HORSE %d") | checked |
| Text `leftdraw`, `rightdraw`, `leftdrawbold`, `rightdrawbold` | ctor, language 0x13 only | SetSize 65×15, SetCord | checked |
| Text `RIGHTDRAWBOLD`, `RIGHTDRAW` | ctor 0xaeb3–0xaf23 | ProcessAssignedString; `+0x60` saved as `game+0xc`/`+0x10` | **yes** |
| Text `LEFTDRAW[BOLD]`, `RIGHTDRAW[BOLD]` (built with strcpy/strcat) | PlayOneGame 0xccc8/0xcd1e | SuperFlash | checked |
| ScoreBox `PlayerScore1..4` | ctor 0xaf3f–0xaf99 | `game+0x14..+0x20`; Increase, `+0x178` | **yes** |
| Timer `Timer` | ctor 0xafb7 | `game+0x24`; Reset (vt+0x28), Halt, Resume, TimeLeft, Award; fields §8.1 | **yes** |
| (LocateGash) `Timer`, NULL | ctor 0xafdc (MegaLink only) | `->+0x18 = 0` | checked |
| Window `2_horse_line`, `3_horse_line`, `4_horse_line` | ctor 0xbf5f… | Disable/Enable by player count | **yes** |
| Sprite `Player3ScoreBox`, `Player4ScoreBox`, `Player%dScoreBox` | ctor | Enable | **yes** (3/4) |
| HelpButton `HelpBut` | ctor 0xc51c (only if `G->+0x30` ∈ {2,3} and `G->+0x2041 & 0x40`) | replaces `+0xc8` help text | checked |
| Window `Intro` | PlayOneGame 0xcac1 | Disable | checked |
| ExitButton `ExitBut` | 0xcbde, 0x9025, 0x9c08 | flags `|= 0x100` / `&= ~0x100` | checked |
| Text `InstructionText` | 0x82a4 (unchecked), 0xd14d | Enable, ChangeString | **yes** (0x82a4) |
| Text `HandValue%d`, `HandName%d` | ChangeInstructText | ColorIt | checked |
| Sprite `Instruction_Box` | 0x860e | `->+0x8` (parent) and `->+0x48` read | checked |
| Sprite `SelectZone1..4` | 0x60d5, 0x8fcd, 0xa414 | flags 0x100, `List::Clear(+0x88)`, Enable/DisableAllClicks | checked |
| Sprite `SelectPrompt%d` | 0x79ce | Disable | checked |
| Sprite `SelectBox%d`, `SelectBox%dHorse` | 0x9e87, 0x9eb4, 0xa14b, 0xa334 | Disable / Enable / SetBmp | **yes** (GetData) |
| Sprite `DarkHorseSelect%d` | 0xa0c1 | Enable | checked |
| Sprite `CompHandBox` | 0x90df | FadeOut | checked |
| Text `RoundNumber` | 0x92fc | ChangeString | checked |
| Text `HurryUpMessage1`, `HurryUpMessage2` | Signal(0xa) 0xa57a | Enable/Disable with delays | **yes** |

Every name above exists in the shipped layouts with exactly that spelling.

---

## 8. Engine widget classes

None of these is ever `new`ed by a game. Only the imported methods and the inline offsets
below matter.

### 8.1 `OneBmpTimer` (from `<Timer>`; a Sprite subclass)

| member | conf | semantics |
|---|---|---|
| vt slot 10 (+0x28) `Reset()` | C rp 0xb0a9, 0xd2e1; bo 0xe3c0 | Restart the timer with the full duration (`+0xe0 = now`) and leave it running. rp sets `+0xb0` *before* Reset (0xb050, `players × 120000`), so Reset uses `+0xb0` and does not re-read `TotalTime`. |
| `+0xac` u32 | C rp 0xd974 | A "now" snapshot, the clock value the timer measures against (the halt time while halted) [L] |
| `+0xb0` int | C rp 0xb050, 0x9d78 | Total duration in ms. Initialise from `TotalTime × 60000` (bo 2.5 → 150000; rp 3 → 180000) [L] |
| `+0xe0` u32 | C rp 0x9d7e, 0x9d2a, 0xd46c, 0xd982 | Start time on the world clock. Games write it: `now + left − total` (sync), `1` (force time-up), `−= 1000` (debug key) |
| `int TimeLeft()` | C | `max(0, total − (now − start))` in **ms** (bo compares with 24999 at 0x1127f and uses `TimeLeft()−1000` at 0x11976) |
| `void SetTimeLeft(int ms)` | C bo 0x11290, 0x1198c | `start = now − (total − ms)` |
| `void Halt()` | C | Freeze: snapshot now into `+0xac`; idempotent (bo calls it 12×) |
| `bool Resume()` | C rp 0xcdac (tests `%al`) | Unfreeze, shifting `start` by the halted span. Return value: true when it actually resumed? [L] (rp plays "karing" on true) |
| `void Award(ScoreDisplay* sd, int a, int b)` | C site rp 0xd9a4 `(sd, 10000, 1300)`, 0xd9d7 `(sd, 10000, 0)` | End-of-game time bonus into `sd`. Exact formula unknown [?]. Suggestion: `sd->Increase(a * TimeLeft / total)`, animated over `b` ms when b > 0. |
| hurry | L | Raise `HurrySignal` (USER2 → 0xa) once when time runs low. The threshold is unknown [?]; ~10 s is a guess. |
| expiry | L | At 0: set `MWorld->+0x19c = 1` (§4.3). Draw nothing further. |
| drawing | L | Show its one bitmap ("One Bmp") cropped to `TimeLeft/total` of its width. Direction 1 or −1 picks the fill or shrink direction. |

### 8.2 `ScoreDisplay` (from `<ScoreBox>`; a Sprite subclass)

| member | conf | semantics |
|---|---|---|
| `+0x178` int | C bo 0xd9fa, 0xdd38, 0x139c1; rp 0xd11f, 0xd7fc, 0xda5c | **Current score**, read directly by the games, so it must be updated synchronously |
| `void Set(int)` | C bo 0xfcc9, 0xfdb2 | `+0x178 = v`; re-render |
| `void Increase(int)` | C bo ×6, rp ×3 | `+0x178 += v`; re-render (may count up visually; the field changes at once) |

Render `+0x178` through the `<String>` digit mask `#,###,###`, using the element's font, colour
and alignment.

### 8.3 ExitButton

A clickable Sprite (rp toggles its `0x100` flag). When clicked: play `ExitSound` and set
`MWorld->+0x19c = 3`. rp does the same on ESC, and breakout has no other way to quit (no ESC
check anywhere; its loops end on state > 1, and state 3 skips the post-game logic at 0x1411e)
[L]. A confirm dialog might come
first, as in `SystemClass::ConfirmExit` in other games [?].

### 8.4 HelpButton

| member | conf | semantics |
|---|---|---|
| `+0xc8` `char*` | C rp 0xc52e–0xc59a | Help text, **malloc-family heap memory**: rp `free()`s the old value and stores a `calloc` buffer with the text of `GetHelpFileName(…, ".usa_skill")`. This only happens with `G->+0x30` ∈ {2,3} and `G->+0x2041 & 0x40` |
| click | L | Show `HelpBkg` with a `HelpExitButton` and the `HelpText`/`+0xc8` text. Set `HelpOnScreen = 1` while open and 0 on close. A no-op click is acceptable for booting. |

### 8.5 Button, Window, Sprite

These are plain Sprites with the §6 flags. bo's help buttons are `<Button>`s with `ButtonSignal`,
and bo opens its help screens itself (§2.1).

### 8.6 Text

A plain Sprite with `+0x64 = String*` and a rendered `+0x60`. Our
`Sprite::ProcessAssignedString` already sets `bmp = text bitmap` (sprite.cpp:389).

---

## 9. Signals

- **Handlers.** bo `BreakOutSigHand` and rp `RacePokerSigHand` are `SpriteSigHand`s (new 0xc,
  emitted weakly). Games push them onto `root->+0x8c` (§4.4).
  - bo `DoIt(Sprite*, SpriteSignal* s)` 0x14220 returns `game->vt[2](s)`, which is
    `BreakOutClass::Signal(SpriteSignal*)`, switching on `s->+0xc`.
  - rp `DoIt` 0x10968 builds an `RPSignal` (a SpriteSignal plus an int at +0x1c, size 0x20)
    from `s->+0xc` and `s->+0x10`, with `+0x1c = game->+0x2c`. It forwards only when
    `game->+0xc8 == 1` or the current player matches, through vt slot 7 `Signal(RPSignal*)` →
    vt slot 8 `Signal(type, int)`. Handlers return 1.
- **Raising.** A clicked sprite with `+0xa0 ≠ 0` raises `SpriteSignal(+0xa0, from = sprite, 0,
  gash)`, and the signal must reach `root->+0x8c`. Our `Sprite::Signal` bubbles through
  `parent_of()`. That works only for sprites whose parent chain (through the constructor
  argument) ends at root. Both games' touch zones are built that way, and the Gash tree must be
  too.
  - Alternative: deliver every raised signal straight to `MWorld->+0x60->+0x8c`.
- **Continuous signal (bo paddle) [C game side, ? flag meaning].** bo's full-screen 640×480 zone
  (flags **0x900**, `+0xa0 = 0x16`, z 1.0, parent root; 0xe4ac–0xe4ea) handles 0x16 at 0xa421.
  The handler sets "touch mode" and moves the paddle **17 px per signal** toward
  `MegacGlobals+0x5b60` (touch X), snapping when it is within 22 px. Two consequences:
  - The engine must keep `G->+0x5b60/+0x5b64` (x, y) and `+0x5b68` (down) current. Nothing in
    `src/` writes them today; only `MouseManager::CheckLoc` writes `+0x5ae4`.
  - It must **re-raise the signal every frame while the finger is held or dragging** on a 0x800
    sprite. With one signal per touch-down, the paddle crawls 17 px per tap.
- `SpriteSignal`, `RPSignal` and `SpriteSigHand` vtables, typeinfos and destructors are emitted
  weakly in the games (`vtable for SpriteSignal`: bo 0x18220, mm and rp 0x13200). The engine's
  classes must add no virtuals ([sprite-engine.md §2](sprite-engine.md)).
- **Group fall-through:** `BreakOutClass::Signal(SpriteSignalType)` 0x83d0 builds
  `SpriteSignal(t,0,0,0)` on the stack and calls `vt[2]`. `LinuxGameClass` (Group plus vt slot 6
  `PlayOneGame`, weak in the games) adds nothing the engine must provide.

---

## 10. The other unresolved imports

| symbol | games | conf | ABI and semantics; what a stub must return |
|---|---|---|---|
| `bool SendPacket(char* pkt)` `_Z10SendPacketPc` | all | C bo 0xbd97, rp 0x8202 | MegaLink transmit. Only reached when `G->+0x30 == 1` (SendData returns early otherwise: bo 0xbc7a, rp 0x80a0). Packet: `[0] dst id, [1] src id, [2] 1, [3] 5, [4..5] u16 len = 0x18 + 4n, … +0x10 type, +0x14 n, +0x18 int data[n]`. **A no-op must return true (1).** On false the caller retries in a busy loop for 3000 ms. |
| `USBIO::JoystickFound()` `_ZN5USBIO13JoystickFoundEv` | bo | C 0x126ca | `this = *(USBIO**)(G+0x22d8)`, which merit3d.md lists as 0 (no joystick). So **`this` may be NULL: don't dereference it.** Return false, and bo uses its touch control (§9). |
| `USBIO::ReadJoystick(short* x, short* y, uchar* btn, short xr, short yr, uchar* extra)` | bo | C 0x12717 | Called as `(&x, &y, &btn, 1000, 1000, NULL)` only after JoystickFound and `!IsPlatformForce()`. `x` ∈ [−xr, xr] is a **velocity**: `paddleX += x · speed(17) · 0.001 · dt_ms(≤100) · 0.05`, clamped to [122, 511]; 0 means no move. To drive the paddle from a mouse, return true from JoystickFound and set `x = clamp((mouseX − 320) · k, −1000, 1000)`. That gives joystick-style steering (not absolute), so the touch path is the better mapping. |
| `int uszprintf(char* buf, int size, const char* fmt, ...)` | bo ×67 | C 0x7dc1 `(buf, 0xff, "PLAYER %d", n)`, 0x8e9b `(buf, 0x400, …)` | Allegro 4 Unicode `snprintf` (size in bytes, always NUL-terminated). ASCII/UTF-8 text, so `vsnprintf`. **The stub returning 0 leaves stack buffers uninitialised**, so player labels, help pages and level texts are garbage, and long garbage may crash in String layout. |
| `char* ustrzcpy(char* dst, int size, const char* src)` | bo | C 0xcae3 `(dst, 0x100, src)` | Allegro bounded copy; always terminates; returns dst. |
| `int GlobalDebugLevel()` | rp | C 0x10d1f, 0x10f01 | `> 9` enables `ListT::Clear` logging. Return 0. |
| `int HighScoresManager::LowestScore(int)` | bo | C 0xda0d | `this = HighScores()` (= `HighScoresManager::Instance()`), arg `0x7fffffff`. Compared **unsigned** with the score (`ja`): a higher score goes to the "new high score" path (0xdde9), otherwise "GAME OVER". Return the table's lowest score (0 is harmless). Implement next to `HighestScore` in megatouch-host. |
| `void LinuxGameClass::CheckHotKey(char)` | bo | C 0x12280 | Debug hotkeys (§5.3). No-op. |
| `void NetGlobals::NetSpriteUnRegister(NetSprite*, uchar)` | bo | C 0x1628e | Only called when `NetGlob != NULL`, which never happens standalone. No-op. Side note: breakout's `DeltaButtonClass` (a NetSprite) reads a **u16 at NetSprite+0xaa** (0x16273), inside Sprite's tail padding. Check that our `NetSprite` keeps 0xa9–0xab free or puts its id there. |
| `KeyManager::GetInstance()`, `KeyManager::Country() const` | rp, mm | C sites | **Out of scope (key/anti-tamper hook); leave as is.** Call sites: weak `Key()` 0x108e0 wraps `GetInstance`; `Country(Key())` at ctor 0xa8fd, 0xa913, 0xac78, 0xac8e (and the C1 copy at 0xdb98). 3 or 4 selects the UK intro (`intro/intro_uk`) and `layout_uk.xml`. With the current stub (0) the game takes the non-UK path, which is fine. |
| `OneBmpTimer::*`, `ScoreDisplay::*`, `Gash::*`, `DoParse`, `LinuxWorldClass(bool)` | | | §4, §5, §8 |
| `MWorld`, `TopGash`, `SSIG_NAMES`, `HelpOnScreen` | | | §2 |

---

## 11. Symbols libmerit_legacy already exports whose Gash-game semantics need checking

These are provided today (`nm -D --defined-only shared/bin/libmerit_legacy.so`), but these games
rely on behaviour the DOSLinuxWorld games don't exercise. libmerit_legacy exports **no**
`Gash::*`, `LinuxWorldClass`, `OneBmpTimer` or `ScoreDisplay` symbols.

| symbol(s) | what these games need | status in `src/legacy` |
|---|---|---|
| `WorldClass` layout and the methods called with `this = MWorld` | LinuxWorldClass must be a WorldClass with the same 0..0x207 layout (§4.1) | OK if derived |
| WorldClass `+0x60` (`void* root`) | must be the root Sprite with valid `+4` and `+0x8c` lists | **never set** (memset to 0) |
| WorldClass `+0x19c` (`frozen`) | game state 0–4; engine writes 1 at time-up | **conflict:** treated as a click freeze (sprite.cpp:928) |
| `MegacGlobals+0x207c` world slot | must point at the LinuxWorldClass while the game runs | `install_loader_world()` puts a DOSLinuxWorld there; the LinuxWorldClass ctor must replace it |
| `Sprite::Sprite(Bitmap*, ulong, Sprite* parent)` | `parent = root` for zones; Gash parent links; children relative to parent; nested Disable hides the subtree | parent links exist (`parents()`); the click test only checks the **immediate** parent's 0x80 (sprite.cpp:933). Nested Gash windows need every ancestor checked |
| `Sprite::Signal(SpriteSignal*)` / `Signal(type)` / `SpriteClick` | the raised `+0xa0` must reach `root->+0x8c` handlers; 0x800 sprites re-raise while held | bubbles via `parent_of()`: OK for ctor-parented sprites; no hold-repeat |
| `List::Push(Group*)` on `root->+4` / `root->+0x8c` | lists must exist | root missing |
| Rendering of root's subtree | global z-sort across root's children and top-level sprites | children are drawn inside their parent's DoDraw, so root's subtree would sit under everything (sprite.cpp:841–873) |
| `SpriteSignal::SpriteSignal(type, Sprite*, Sprite*, Gash*)` C1 (bo) / C2 (rp) | +0xc type, +0x10 from; Gash* stored, never read | OK |
| `SpriteSigHand::SpriteSigHand()` C2, `_ZTV13SpriteSigHand`, `_ZTI13SpriteSigHand` | as in sprite-engine.md | OK |
| `Sprite::ProcessAssignedString`, `ChangeString`, `SetSize` on Gash Text sprites | `+0x60` valid afterwards; `+0x64` String present | OK once the parser assigns a String |
| `Sprite::Enable`/`Disable`/`SetBmp`/`FadeOut`/`ColorIt`/`EnableAllClicks`/`DisableAllClicks` on Gash sprites | ordinary | OK |
| `IsScreenTouched()` | bo busy-waits `while (IsScreenTouched()) Delay(10)` inside signal handlers | pumps events: OK |
| `MouseManager::CheckLoc` | bo reads `G+0x5b60/+0x5b64` (17 sites) | does not write them |

---

## 12. Open questions

- The `LinuxWorldClass(bool)` argument; the 0x20 bytes at 0x208–0x227.
- Gash +0x0c..+0x14 and the object size (internal only).
- Exact hurry threshold, `Award` formula, timer drawing direction, and whether time-up also needs
  `ShowGameOver`.
- Sizing of graphic-less `SelectZone` sprites; whether `DefaultSpriteSignal` on a Window is
  inherited; first vs last match for duplicate names.
- Engine meanings of signals 0–8; the meaning of Sprite flag 0x800 (taken here as "repeat signal
  while held").
- Whether `ExitButton` asks for confirmation before setting state 3.

---

## Implementation checklist

Minimal behaviour for breakout, racepoker and motormatch to boot and be playable, in dependency
order. Leave KeyManager alone.

1. **Globals:** define `LinuxWorldClass* MWorld`, `Gash* TopGash` (both 4-byte, NULL),
   `int HelpOnScreen = 0`, and `char SSIG_NAMES[32][20]` with row 8+n = `"SSIG_USER<n>"`.
2. **LinuxWorldClass** (`_ZN15LinuxWorldClassC1Eb`): `class LinuxWorldClass : public WorldClass`,
   sizeof 0x228, with a virtual deleting dtor at vt[1]. The ctor does `WorldClass()`, saves and
   sets **both** `MWorld` and `MegacGlobals+0x207c`, creates the root Sprite at +0x60 (enabled,
   no bitmap, `+4` and `+0x8c` Lists allocated) and zeroes +0x19c. The dtor restores both slots.
3. **Don't treat +0x19c as an input freeze** for this world, or freeze only for ≥ 2.
4. **Root rendering:** draw root's whole subtree (children, Gash tree, manually pushed cards) in
   the global z order, with positions relative to parents.
5. **DoParse** (`_Z7DoParsePcb`): redirect the absolute path, tokenise the tag stream (§3.2,
   case-insensitive tags, trimmed names and values, `<comment>`/`<Comment>`/`<info>` ignored,
   stop at `<EOF>`), build the `Gash : Group` tree (+4 daughters, +8 parent, +0x18 sprite,
   virtual dtor) under a new `TopGash`, and create every sprite immediately (§6). Use parent
   chains ending at root, relative coordinates, accumulated z, `Active Off` → 0x80, and
   signals → `+0xa0` plus 0x100. Text gets a String and `ProcessAssignedString()`. Graphic names
   are resolved by stripping the extension and calling `MWorld->LoadBmp(name, 1, 1.0, 3, 999999,
   1, 0, 0)`.
6. **Data:** give motormatch `gamegraphics/racepoker/layout.xml` (copy or symlink of
   `motormatch/layout.xml`). Without it, motormatch crashes.
7. **Gash::LocateGash / LocateSprite** (case-insensitive, depth-first, NULL name = first of the
   tag, prefer the later duplicate) and **ParseSpriteAttribs** (a no-op is acceptable).
   **~Gash** must not touch sprites.
8. **OneBmpTimer** (`<Timer>`): a Sprite subclass whose vtable overrides slot 10 `Reset`. Fields
   +0xac/+0xb0/+0xe0 as in §8.1, on the world clock (+0x170). Implement `TimeLeft` (ms),
   `SetTimeLeft`, `Halt`, `Resume` (bool), `Award` (best effort), the hurry signal (USER2) and
   `MWorld->+0x19c = 1` at expiry. Draw the bar proportionally.
9. **ScoreDisplay** (`<ScoreBox>`): int at **+0x178**, `Set`, `Increase`; render with the
   `#,###,###` mask.
10. **ExitButton** → ExitSound plus state 3. **HelpButton** → a no-op or help overlay, with
    `+0xc8 char*` (malloc'd) and `HelpOnScreen` toggled. **HiScoreBox** → fill Name/Score (or
    leave the text).
11. **Signal delivery:** a click on a sprite with `+0xa0` reaches `root->+0x8c` handlers.
    **0x800 sprites re-raise every frame while touched.** Maintain `G+0x5b60/+0x5b64/+0x5b68`
    (touch x, y, down) every pump; breakout's paddle depends on both.
12. **Small imports:** `SendPacket` → return true; `USBIO::JoystickFound` → false (NULL `this` is
    allowed); `ReadJoystick` → zero the outputs; `uszprintf` = `vsnprintf`; `ustrzcpy` = bounded
    copy plus NUL; `GlobalDebugLevel` → 0; `HighScoresManager::LowestScore` → the lowest table
    score or 0; `LinuxGameClass::CheckHotKey` and `NetGlobals::NetSpriteUnRegister` → no-ops.
13. Smoke test after each step: rp gets past 0xace5 once steps 1–2 are done; it reaches
    `PlayOneGame` with 5 and 7–9 done. bo reaches `GameLogic` with 1, 2, 5, 7–9 and 12, and its
    paddle follows the finger with 11.
