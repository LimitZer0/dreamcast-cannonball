/* Minimal SDL config for compiling the headers with Emscripten (the render
   tool never calls SDL). */
#ifndef SDL_config_h_
#define SDL_config_h_
#include "SDL_platform.h"
#define HAVE_LIBC 1
#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MATH_H 1
#define HAVE_INTTYPES_H 1
#define SDL_AUDIO_DRIVER_DUMMY 1
#define SDL_JOYSTICK_DISABLED 1
#define SDL_HAPTIC_DISABLED 1
#define SDL_VIDEO_DRIVER_DUMMY 1
#define SDL_TIMER_UNIX 1
#define SDL_THREADS_DISABLED 1
#define SDL_LOADSO_DISABLED 1
#define SDL_FILESYSTEM_DUMMY 1
#endif
