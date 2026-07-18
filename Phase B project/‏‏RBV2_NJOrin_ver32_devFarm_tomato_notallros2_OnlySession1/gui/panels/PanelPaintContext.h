#pragma once


#include <SDL2/SDL.h>
#include <functional>
#include <string>


struct PanelPaintContext
{
    SDL_Renderer* renderer = nullptr;


    std::function<void(int, int, const std::string&, SDL_Color, int)> drawText;
    std::function<std::string(double, int, int)> trimNumber;
};



