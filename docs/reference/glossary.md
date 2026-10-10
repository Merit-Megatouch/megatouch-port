# Glossary

| Term | Meaning |
| --- | --- |
| **`.spr`** | Megatouch RLE sprite animation format. |
| **ABI (old C++)** | The GCC 4.1 C++ library ABI the engine uses (copy-on-write strings). Selected with `-D_GLIBCXX_USE_CXX11_ABI=0`. |
| **Asset dir** | A game's art and sound folder, e.g. `/usr/local/ion_only/games/g_trix`. The game runs with it as its working directory. |
| **Attract mode** | What a game shows when nobody is playing. Clicking starts a game. |
| **Backend** | `libgame_device_sprite.so`: the library that does graphics, input and sound for a GameDevice game. Ours is SDL2. |
| **Base class / base vtable** | Engine classes like `Graphics::BaseTexture` whose code lives in normal libraries. We build objects from them. |
| **Cabinet** | The Megatouch ION bar-top machine, and here its disk image. |
| **cabinet-libs** | The 2008 third-party libraries taken from the image (Pango 1.14, OpenSSL 0.9.8, SQLite 2…). |
| **Closure** | A library plus everything it needs, recursively (NEEDED entries). |
| **D2 / C2** | Itanium C++ ABI names for the base-object destructor and constructor (`_ZN…D2Ev`, `_ZN…C2Ev`). We call them directly. |
| **data/** | A game's private copy of the cabinet filesystem the shim maps paths into. |
| **Debug flag** | An empty file under `/var/merit/debug/<category>/<flag>` that turns on an engine feature. |
| **debugfs** | e2fsprogs tool that reads ext2/3 partitions in an image file without mounting it. |
| **DLLName** | The game library's name in `gamedata.xml` (`g_trix` → `g_trix.so`). Also the folder name under `games/`. |
| **drvfs** | WSL's mount of Windows drives (`/mnt/c`, `/mnt/e`). Slow, 64-bit inode numbers, rejected by SDL file opening. |
| **Engine family** | GameDevice, Unity, Merit3D or legacy sprite: how a game was built, which decides how to port it. |
| **engine-sdk** | The engine libraries the backend links against (`shared/engine-sdk`). |
| **Event log** | `fakeio/events.jsonl`: one JSON line per thing the cabinet did with its hardware, plus games starting and ending ([events](events.md)). |
| **fs shim** | Our replacements for `open`, `stat`… that rewrite cabinet paths. |
| **gamedata.xml** | The cabinet's game table. |
| **GameDevice** | The 2009+ engine family; also its central class `GameDevice::BaseGameDevice`. |
| **GameId** | `xml_gameinfo::GameIds` enum value (`G_TRIX` = 245). |
| **GameIds** | The loader's enum of game IDs (0–299) and its own programs (10000–10063), e.g. `G_TRIX`, `IDLE`, `OPSETUP`. |
| **Hardware bridge** | `scripts/hwbridge.py`: passes events on to commands, MQTT, webhooks and WLED, and feeds real inputs into the fake board ([connectors](../guides/connectors.md)). |
| **Hitch** | A frame that took much longer than 33 ms. |
| **Host** | `megatouch-host`, our program that loads a game. |
| **ION** | The Megatouch cabinet line these games come from (this image is the 2014 software release). |
| **Legacy (sprite) engine** | The pre-2009 2D engine inside the loader (`Sprite`, `Bitmap`, `WorldClass`, Allegro). 143 games use it. |
| **Light-show kit** | Optional LED lighting for ION cabinets, run by the I/O board's PSoC; the loader's `LightShowManager` plays sequences on it. |
| **llvmpipe** | Mesa's software OpenGL. Used because WSLg's GPU path is 64-bit only. |
| **Loader** | `/usr/local/bin/loader`, the cabinet's main program. Packed. It runs every game inside its own process. |
| **Loader services** | Functions and data the loader exported to games (`Translator`, `PlrScore`, …). We provide stand-ins. |
| **Merit3D** | 3D engine family (ODE physics, OpenGL). |
| **Minidrucker** | German for "mini printer": the small printer operators plugged in to print a cabinet's books. |
| **Pango modules** | Pango 1.14's shaping engines. Without them text renders as boxes. |
| **PlrScore** | Loader array of player scores. |
| **PSoC** | The programmable microcontroller on the I/O board; its version (status byte 17) switches on the light show and the improved amplifier. |
| **Resource locator** | Engine class that finds files by game, resolution and language. |
| **Seam** | The single point where the game meets the cabinet backend: `GameDevice::CreateNewGameDevice()`. |
| **Slot** | Index of a virtual function in a vtable (after offset-to-top and typeinfo). |
| **Snapshot** | `cabinet/`: what porting needs, copied out of the image by `make snapshot`. |
| **Stand-in** | Our implementation of a loader service. |
| **Stub library** | An empty `lib*_sprite.so` that satisfies a NEEDED entry. |
| **Submodule** | A git repo inside another. Each game is one. |
| **tcache** | glibc's per-thread malloc cache; turned off so freed memory behaves like the 2008 allocator. |
| **Unity family** | Games built with Unity 3.2, run through `LinuxPlayer`. |
| **Vtable cloning** | Our technique: copy an engine base vtable, patch some slots, point an engine-constructed object at the copy. |
| **WSLg** | WSL's built-in support for Linux windows and sound on Windows. |
