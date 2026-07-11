#ifndef BOMBERMAN_TILES_H
#define BOMBERMAN_TILES_H

#include <stdint.h>

/* Tile indices */
#define TILE_FLOOR    0u
#define TILE_WALL     1u
#define TILE_SOFT     2u
#define TILE_P1       3u
#define TILE_P2       4u
#define TILE_BOMB     5u
#define TILE_EXPLODE  6u
#define TILE_DEAD     7u
#define TILE_COUNT    8u

/* Initialise OBJ tiles and palette. Call once after ex_video_init(). */
void tiles_init(void);

/* Reset per-frame OBJ allocation before drawing arena tiles. */
void tiles_begin_frame(void);

/* Draw one 8x8 OBJ tile at tile-space position (tx, ty). */
void tile_set(uint8_t tx, uint8_t ty, uint16_t tile_idx);

#endif
