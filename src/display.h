#pragma once
#include "SDL.h"
#include <string>

void display_init();

void handle_input(uint8_t* ports);

void draw_video_ram(uint8_t* memory);