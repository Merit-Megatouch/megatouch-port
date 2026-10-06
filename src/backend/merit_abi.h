// Declarations for the parts of the original Megatouch engine (2013, GCC 4.1, i386)
// that the SDL backend calls directly. Only names and parameter types matter here:
// they produce the mangled symbols exported by libgame_device.so, libgraphics.so,
// libcore.so, etc. Object layouts are accessed through raw offsets recovered from
// the original libgame_device_sprite / libgraphics_sprite / libinput_sprite /
// libsound_sprite binaries (see notes in each .cpp).
#pragma once
#include <string>
#include <vector>
#include <cstdint>

#if _GLIBCXX_USE_CXX11_ABI
#error "must be compiled with -D_GLIBCXX_USE_CXX11_ABI=0 (engine uses the pre-C++11 std::string)"
#endif

namespace xml_gameinfo { enum GameIds : int {}; }
namespace resources { class ResourceLocator; }
namespace TextSystem { class ITextSystem; }
namespace Messaging { class IListener; class IMessage; class IMessageManager; }

namespace Core {
enum TEXTURE_PIXEL_FORMAT : int {};
class Rect {
public:
    Rect(float x, float y, float w, float h);
    ~Rect();
    // layout: vptr, x, y, w, h
    void* vptr_; float x, y, w, h;
};
class Color { public: void* vptr_; float r, g, b, a; };
class Vector2D { public: void* vptr_; float x, y; };
}

namespace Graphics {
class Scene;
class ITexture;
struct Frame {
    int textureIndex;
    Core::Rect rect;
};
class ImageResource {
public:
    void AddTexture(ITexture*);
    ITexture* GetTexture(int);
    int GetTotalFrames();
    int GetTotalTextures();
    void AddFrame(Frame const&);
    Frame* GetFrame(int);
};
}

namespace GameDevice {
class GameConfig {
public:
    void SetWindowWidth(unsigned int);
    void SetWindowHeight(unsigned int);
    void SetScreenWidth(unsigned int);
    void SetScreenHeight(unsigned int);
    unsigned int GetWindowWidth() const;
    unsigned int GetWindowHeight() const;
    void SetGameID(xml_gameinfo::GameIds);
    void SetNumPlayers(int);
    void SetRAMAmountMB(int);
    void SetLanguage(char const*);
    void SetContentRatingLevel(int);
    void SetIsQuestionableContentAllowed(bool);
    void SetCardFanning(bool, float);
};
}
