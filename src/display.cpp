#include <cassert>
#include <iostream>
#include "display.h"

std::string const TITLE = "Space Invaders";
int const HEIGHT = 256;
int const WIDTH = 224;

// Globals

SDL_Surface* surf;
int resizef;
SDL_Window* win;
SDL_Surface* winsurf;

int HandleResize(void* userdata, SDL_Event* ev) {
	if (ev->type == SDL_WINDOWEVENT) {
		if (ev->window.event == SDL_WINDOWEVENT_RESIZED) {
			resizef = 1;
		}
	}

	return 0;  // Ignored
}

void display_init() {
	// Init SDL
	if (SDL_Init(SDL_INIT_VIDEO)) {
		printf("%s\n", SDL_GetError());
		exit(1);
	}

	// Create a window
	win = SDL_CreateWindow(
		TITLE.c_str(),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		2*WIDTH, 2*HEIGHT, SDL_WINDOW_SHOWN
	);
	if (!win) {
		puts("Failed to create window");
		exit(1);
	}

	// Get surface
	winsurf = SDL_GetWindowSurface(win);
	if (!winsurf) {
		puts("Failed to get surface");
		assert(false);
	}

	// Handle resize events
	SDL_AddEventWatch(HandleResize, NULL);

	// Create backbuffer surface
	surf = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, 32, 0, 0, 0, 0);
}

void draw_video_ram(uint8_t *memory) {
	uint32_t* pix = (uint32_t*) surf->pixels;

	int i = 0x2400;  // Start of Video RAM
	for (int col = 0; col < WIDTH; col++) {
		for (int row = HEIGHT; row > 0; row -= 8) {
			for (int j = 0; j < 8; j++) {
				int idx = (row - j) * WIDTH + col;

				if (memory[i] & 1 << j) {
					pix[idx] = 0xFFFFFF;
				}
				else {
					pix[idx] = 0x000000;
				}
			}

			i++;
		}
	}

	if (resizef) {
		winsurf = SDL_GetWindowSurface(win);
	}

	SDL_BlitScaled(surf, NULL, winsurf, NULL);

	// Update window
	if (SDL_UpdateWindowSurface(win)) {
		puts(SDL_GetError());
	}
}

void handle_input(uint8_t *ports) {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_KEYDOWN:
			switch (ev.key.keysym.sym) {
			case 'c':  // Insert coin
				ports[1] |= 1;
				break;
			case 's':  // P1 Start
				ports[1] |= 1 << 2;
				break;
			case 'w': // P1 Shoot
				ports[1] |= 1 << 4;
				break;
			case 'a': // P1 Move Left
				ports[1] |= 1 << 5;
				break;
			case 'd': // P1 Move Right
				ports[1] |= 1 << 6;
				break;
			case SDLK_LEFT: // P2 Move Left
				ports[2] |= 1 << 5;
				break;
			case SDLK_RIGHT: // P2 Move Right
				ports[2] |= 1 << 6;
				break;
			case SDLK_RETURN: // P2 Start
				ports[1] |= 1 << 1;
				break;
			case SDLK_UP: // P2 Shoot
				ports[2] |= 1 << 4;
				break;
			}
			break;

		case SDL_KEYUP:
			switch (ev.key.keysym.sym) {
			case 'c': // Insert coin
				ports[1] &= ~1;
				break;
			case 's': // P1 Start
				ports[1] &= ~(1 << 2);
				break;
			case 'w': // P1 shoot
				ports[1] &= ~(1 << 4);
				break;
			case 'a': // P1 Move left
				ports[1] &= ~(1 << 5);
				break;
			case 'd': // P1 Move Right
				ports[1] &= ~(1 << 6);
				break;
			case SDLK_LEFT: // P2 Move Left
				ports[2] &= ~(1 << 5);
				break;
			case SDLK_RIGHT: // P2 Move Right
				ports[2] &= ~(1 << 6);
				break;
			case SDLK_RETURN: // P2 Start
				ports[1] &= ~(1 << 1);
				break;
			case SDLK_UP: // P2 Shoot
				ports[2] &= ~(1 << 4);
				break;

			case 'q':  // Quit
				exit(0);
				break;
			}
			break;

		case SDL_QUIT:
			exit(0);
			break;
		}
	}
}