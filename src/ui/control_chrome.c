#include "ui/control_chrome.h"
#include "ui/daw_ui_button.h"
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
