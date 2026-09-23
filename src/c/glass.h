// glass.h — the hourglass body: the lattice it is built from and its outline.
// All coordinates are relative to the centre of the body.

#pragma once

#include <pebble.h>

// One rung of the size ladder. The row count is the same at every rung; only
// the scale differs.
typedef struct {
  int cell;     // lattice pitch: one grain plus its gap
  int grain;    // grain side: odd centres the row in the odd cavity
  int width;    // grains in a full row
  int straight; // full rows before the funnel starts
  int taper;    // rows inside the funnel
  int neck_w;   // neck half-width, on the cavity
  int neck_h;   // neck half-height, on the cavity
  int stroke;   // glass thickness, must be ODD: it is stroked
} Lattice;

// Call once from init(): the path keeps a pointer to its points, and relaying
// the body rewrites them in place.
void glass_init(void);
void glass_deinit(void);

int glass_rows(const Lattice *l);
int glass_in_row(const Lattice *l, int row);

// Free room inside one cell, on the near side of the grain.
int glass_grain_inset(const Lattice *l);

int glass_chamber_top(const Lattice *l);

// Outer size, glass included. Both are odd.
int glass_w(const Lattice *l);
int glass_h(const Lattice *l);

// Largest rung no taller than max_h. Never NULL: when none fits, the smallest
// is returned anyway.
const Lattice *glass_pick(int max_h);

// Strokes the outline along the mid-line of the wall.
void glass_draw(GContext *ctx, const Lattice *l, GPoint centre, int32_t angle);
