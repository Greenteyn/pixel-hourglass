// sand.h — the grains inside the body.

#pragma once

#include <pebble.h>

#include "glass.h"

// Draws a full frame of sand: the upper chamber has lost `rows_gone` rows and
// the lower one has grown by exactly as many, so the grain count never changes.
// Fills with the context's current fill colour.
void sand_draw(GContext *ctx, const Lattice *l, GPoint centre, int rows_gone,
               int32_t angle);

// Draws the sand on its way between the heaps: the grains that have left the
// upper chamber and not yet joined the lower one. They keep to the lattice they
// fell from, so the breaks in the neck read as the breaks between rows.
void sand_draw_stream(GContext *ctx, const Lattice *l, GPoint centre,
                      int rows_gone);
