# Megatouch ION game ports — run original cabinet games on Linux / WSL2.
#
#   make setup                     build toolchain, runtime and shared cabinet data (once)
#   make                           build megatouch-host + SDL2 backend into shared/bin
#   make new GAME=g_word_dojo_2    scaffold a game from the cabinet into games/<name>
#   make run GAME=g_trix           play it   (debug: make run GAME=g_trix DEBUG=shots|files|sound|profile)
#   make analyze GAME=...          list loader symbols the game still needs
#   make decompile GAME=...        Ghidra C output of the game into games/<name>/decomp
#   make package GAME=... DEST=dir self-contained copy to move to another machine
#   make snapshot                  copy what porting needs out of the disk image (then it can be archived)
#   make games                     after a fresh clone: regenerate lib/ + data/ for every game
#   make publish GAME=...          push a game repo to GitHub (submodule of this repo); GAME=all for everything
#   make stubs GAME=...            placeholder loader functions so a game loads (each logs its first call)
#   make docs                      regenerate the game catalogue; make survey for the porting survey (slow)
#   make help                      this text

R        := $(abspath .)
T        := $(R)/toolchain
CXX      := $(T)/g++32
CC       := $(T)/g++32 -x c
I386     := $(T)/i386
I386LIB  := $(I386)/usr/lib/i386-linux-gnu
SDK      := $(R)/shared/engine-sdk
OUT      := $(R)/build
BIN      := $(R)/shared/bin

CXXFLAGS := -O2 -g -fPIC -std=gnu++17 -D_GLIBCXX_USE_CXX11_ABI=0 -Wall -Wno-unused-parameter \
            -I$(I386)/usr/include -I$(I386)/usr/include/i386-linux-gnu
LINKPATH := -L$(SDK) -L$(I386LIB) -Wl,-rpath-link,$(SDK):$(I386LIB):$(I386LIB)/pulseaudio \
            -Wl,--allow-shlib-undefined

BACKEND_SRC := $(wildcard src/backend/*.cpp)
HOST_SRC    := $(wildcard src/host/*.cpp)
LEGACY_SRC  := $(wildcard src/legacy/*.cpp)
BACKEND_OBJ := $(BACKEND_SRC:src/%.cpp=$(OUT)/%.o) $(OUT)/third_party/stb_vorbis.o
HOST_OBJ    := $(HOST_SRC:src/%.cpp=$(OUT)/%.o)
LEGACY_OBJ  := $(LEGACY_SRC:src/%.cpp=$(OUT)/%.o)
HEADERS     := $(wildcard src/*/*.h)

.PHONY: all build setup new run analyze decompile package snapshot games publish stubs docs survey clean help
all: build

# Empty stand-ins for the cabinet's other backend libraries: some games list them as
# dependencies without using them (everything they did is in libgame_device_sprite.so).
STUBS := $(addprefix $(BIN)/stubs/,libgraphics_sprite.so libinput_sprite.so libsound_sprite.so)

build: $(BIN)/megatouch-host $(BIN)/libgame_device_sprite.so $(BIN)/gameids $(STUBS) $(BIN)/libmega_unity.so $(BIN)/libmerit_legacy.so

$(BIN)/stubs/%.so:
	@mkdir -p $(dir $@)
	echo '/* intentionally empty: superseded by libgame_device_sprite.so */' | $(CC) -shared -fPIC -o $@ -Wl,-soname,$*.so -

$(OUT)/%.o: src/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/third_party/stb_vorbis.o: src/third_party/stb_vorbis.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -fPIC -w -c $< -o $@

# Files a running game has mapped are replaced via rename, never overwritten in place.
$(BIN)/libgame_device_sprite.so: $(BACKEND_OBJ)
	@mkdir -p $(BIN)
	$(CXX) -shared -o $@.new $^ $(LINKPATH) -lgame_device -lgraphics -lcore -linput -lmerit_sound \
	    -lSDL2 -lSDL2_image -lz -ldl && mv $@.new $@

$(BIN)/megatouch-host: $(HOST_OBJ)
	@mkdir -p $(BIN)
	$(CXX) -rdynamic -o $@.new $^ -ldl -lrt -lpthread && mv $@.new $@

# The cabinet loader's legacy 2D engine, for pre-2009 games (preloaded by megatouch-host).
$(BIN)/libmerit_legacy.so: $(LEGACY_OBJ)
	@mkdir -p $(BIN)
	$(CXX) -shared -o $@.new $^ $(LINKPATH) -lSDL2 -lz && mv $@.new $@

# Preloaded into the Unity player (Unity-family games): routes FMOD's sound output to PulseAudio.
$(BIN)/libmega_unity.so: src/unity/fmod_output.cpp src/host/fs_shim.cpp src/common/gl_shots.cpp src/common/env.h
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -shared -o $@.new src/unity/fmod_output.cpp src/host/fs_shim.cpp src/common/gl_shots.cpp -ldl && mv $@.new $@


$(BIN)/gameids: tools/gameids.cpp
	@mkdir -p $(BIN)
	$(CXX) -D_GLIBCXX_USE_CXX11_ABI=0 $< -o $@ -L$(SDK) -lenums -Wl,-rpath-link,$(SDK) -Wl,--allow-shlib-undefined

setup:
	scripts/setup.sh

new:
	@test -n "$(GAME)" || { echo "usage: make new GAME=<dllname>"; exit 1; }
	scripts/new-game.sh $(GAME) $(if $(FORCE),--force)

DEBUG_ENV_shots   := MEGA_SHOT_DIR=$(R)/games/$(GAME)/notes/shots MEGA_SHOT_EVERY=90
DEBUG_ENV_files   := MEGA_TRACE_FILES=1
DEBUG_ENV_sound   := MEGA_DEBUG_SOUND=1
DEBUG_ENV_profile := MEGA_PROFILE=$(R)/games/$(GAME)/notes/game.prof MEGA_HITCH_MS=40 MEGA_FRAME_STATS=1
run:
	@test -n "$(GAME)" || { echo "usage: make run GAME=<name> [DEBUG=shots|files|sound|profile]"; exit 1; }
	@$(if $(filter shots,$(DEBUG)),mkdir -p games/$(GAME)/notes/shots;) true
	$(DEBUG_ENV_$(DEBUG)) games/$(GAME)/run

analyze:
	tools/analyze.sh games/$(GAME)

decompile:
	tools/decompile.sh games/$(GAME)

package:
	@test -n "$(DEST)" || { echo "usage: make package GAME=<name> DEST=<dir>"; exit 1; }
	tools/package.sh games/$(GAME) $(DEST)

snapshot:
	scripts/snapshot-cabinet.sh

# A fresh clone has each game's config and notes (submodules) but not its cabinet files.
games:
	@for g in games/*/game.conf; do d=$${g%/game.conf}; n=$${d#games/}; \
	  { [ -d $$d/lib ] || [ -d $$d/player ]; } && echo "$$n: ok" || scripts/new-game.sh $$n; done

publish:
	@test -n "$(GAME)" || { echo "usage: make publish GAME=<name>|all"; exit 1; }
	scripts/publish.sh $(if $(filter all,$(GAME)),--all,$(GAME))

# Placeholder definitions for every unresolved symbol (tools/stubgen.py), so a game loads while its
# loader services are written; each stub logs its first call. Re-run after adding stand-ins.
stubs:
	@test -n "$(GAME)" || { echo "usage: make stubs GAME=<name>"; exit 1; }
	@mkdir -p $(OUT)/stubs
	tools/analyze.sh games/$(GAME) >/dev/null
	$(T)/venv/bin/python -I tools/stubgen.py games/$(GAME) $(OUT)/stubs/$(GAME).c
	$(CC) -O0 -fPIC -shared -w -o games/$(GAME)/lib/libmega_stubs.so $(OUT)/stubs/$(GAME).c
	@grep -q 'libmega_stubs.so' games/$(GAME)/game.conf || \
	  { grep -q '^PRELOAD=' games/$(GAME)/game.conf && sed -i 's/^PRELOAD=\(.*\)/PRELOAD=\1 libmega_stubs.so/' games/$(GAME)/game.conf || echo 'PRELOAD=libmega_stubs.so' >> games/$(GAME)/game.conf; }
	@grep '^PRELOAD' games/$(GAME)/game.conf

docs:
	tools/catalog.sh

survey:
	tools/survey.sh

clean:
	rm -rf $(OUT)

help:
	@sed -n '3,15p' Makefile | sed 's/^# \{0,1\}//'
