#include "ui/control_chrome.h"
#include "ui/daw_ui_button.h"
#include "ui/font.h"
#include <math.h>
DawSliderChrome daw_slider_chrome(SDL_Rect track,int position_x) {
    if(track.w<=0 || track.h<=0)return (DawSliderChrome){0};
    int center=track.y+track.h/2;
    if(position_x<track.x+1)position_x=track.x+1;
    if(position_x>track.x+track.w-1)position_x=track.x+track.w-1;
    return (DawSliderChrome){.rail={track.x,center-1,track.w,2},
        .marker={position_x-1,center-4,2,9},.hit={track.x,center-8,track.w,16}};
}
bool daw_slider_hit(SDL_Rect track,int x,int y) {
    DawSliderChrome chrome=daw_slider_chrome(track,track.x);
    SDL_Point point={x,y};return SDL_PointInRect(&point,&chrome.hit);
}
void daw_slider_draw(SDL_Renderer* renderer,SDL_Rect track,int position_x,SDL_Color rail,SDL_Color marker) {
    DawSliderChrome chrome=daw_slider_chrome(track,position_x);
    if(!renderer || chrome.rail.w<=0)return;
    SDL_SetRenderDrawColor(renderer,rail.r,rail.g,rail.b,rail.a);SDL_RenderFillRect(renderer,&chrome.rail);
    SDL_SetRenderDrawColor(renderer,marker.r,marker.g,marker.b,marker.a);SDL_RenderFillRect(renderer,&chrome.marker);
}
// One shared compact-rounded frame replaces existing rectangular chrome.
void daw_control_frame(SDL_Renderer* renderer,const SDL_Rect* rect,SDL_Color fill,SDL_Color border) {
    DawUiButtonStyle style={.fill={fill.r,fill.g,fill.b,fill.a},.outline={border.r,border.g,border.b,border.a}};
    (void)daw_ui_button_draw_frame(renderer,rect,&style);
}

// Fits captions vertically and clips horizontally within the rounded control inset.
void daw_control_text(SDL_Renderer* renderer, SDL_Rect rect, const char* text,
                      SDL_Color color, float scale, bool centered) {
    if (!renderer || !text || rect.w <= 0 || rect.h <= 0) return;
    KitUiButtonAppearance appearance;
    kit_ui_button_appearance_preset(KIT_UI_BUTTON_APPEARANCE_COMPACT_ROUNDED, &appearance);
    int padding = (int)fminf(appearance.padding_x, rect.w / 4.0f);
    int width = rect.w - 2 * padding;
    int height = ui_font_line_height(scale);
    if (height > rect.h && height > 0) {
        scale *= (float)rect.h / height;
        height = ui_font_line_height(scale);
    }
    int measured = ui_measure_text_width(text, scale);
    int x = rect.x + padding;
    if (centered && measured < width) x += (width - measured) / 2;
    int y = rect.y + (rect.h - height) / 2;
    ui_draw_text_clipped(renderer, x, y, text, color, scale, rect.x + rect.w - padding - x);
}
