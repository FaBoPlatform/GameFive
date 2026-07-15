/*
 * Game Five shim: prboom-plus's dbopl.h includes SDL_stdinc.h only for the
 * C99 fixed-width integer types (it typedefs its own Bit8u..Bitu from
 * uint8_t..uintptr_t). No SDL here — plain stdint/stddef cover it.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
