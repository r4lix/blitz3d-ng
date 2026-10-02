#ifndef BB_RUNTIME_SDL_OVERLAY_H
#define BB_RUNTIME_SDL_OVERLAY_H

// In-game settings overlay (Dear ImGui drawn over a frozen copy of the last game frame).
// Opened by holding "-" (or L3+R3) on the Switch, F10 on a keyboard. While it is open the game
// does not run: input is diverted here, audio is paused and the game clock is held.

#include <SDL.h>

struct OverlayPad{
	bool btn[SDL_CONTROLLER_BUTTON_MAX];
	float axis[SDL_CONTROLLER_AXIS_MAX];
	bool pulse[SDL_CONTROLLER_BUTTON_MAX]; // presses from a keyboard / injected events (one frame)
};

extern OverlayPad overlayPad;     // filled by the runtime every idle()
extern bool overlayRequested;     // the runtime asks for the menu
extern bool overlayActive;        // the menu is open: the game gets no input

// provided by the runtime
void overlayReleaseGameInput();   // let go of every emulated key/button before the menu opens
void overlayMouse( int x,int y,int button,bool down,bool motion ); // touch / mouse while open

// called from bbFlip (graphics) once per game frame
void overlayFrameHook();

#endif
