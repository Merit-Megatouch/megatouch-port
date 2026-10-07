// TrueType text (src/legacy/ttf.cpp)
#pragma once
struct BITMAP;
namespace legacy {
int ttf_measure(const char* text, int px, bool bold, const char* family, int spacing);
void ttf_draw(BITMAP* dst, const char* text, int x, int y, int w, int h, int r, int g, int b, int px,
              int align, bool bold, const char* family, int spacing, int outline);
}
