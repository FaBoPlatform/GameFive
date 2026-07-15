/*
 * Game Five compat for prboom-plus-lineage sources built against our
 * prboom 2.5.0 headers (force-included via -include, so the vendored
 * files stay unmodified).
 *
 * LittleShort: prboom-plus m_swap.h name; 2.5.0 spells it doom_wtohs
 * (WAD-to-host short — identity on this little-endian target).
 */
#pragma once
#ifndef LittleShort
#define LittleShort(x) ((signed short)(x))
#endif
#ifndef LittleLong
#define LittleLong(x) ((signed int)(x))
#endif
