// TrueType text (src/legacy/ttf.cpp)
#pragma once
#include <vector>
struct BITMAP;
namespace legacy {
int ttf_measure(const char* text, int px, bool bold, const char* family, int spacing);
void ttf_draw(BITMAP* dst, const char* text, int x, int y, int w, int h, int r, int g, int b, int px,
              int align, bool bold, const char* family, int spacing, int outline);
void ttf_coverage(const char* markup, int w, int h, int px, int halign, int valign, bool bold, const char* family,
                  int line_gap, std::vector<unsigned char>& out);
void ttf_text_size(const char* markup, int px, bool bold, const char* family, int line_gap, int max_w, int& w, int& h);
int ttf_em_px(const char* family, bool bold, float pt);
}
