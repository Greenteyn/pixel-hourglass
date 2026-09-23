#include "sand.h"

// Row r of a chamber, relative to the centre of the body.
static int row_y(const Lattice *l, int r) {
  return glass_chamber_top(l) + r * l->cell + glass_grain_inset(l);
}

// Mirrored about the same half-pixel every grain is turned about, so that a
// half turn undoes the mirror exactly. An even grain has no middle pixel, and
// the heap below then sits a pixel closer to the neck than a true mirror would
// put it.
static int mirror_y(const Lattice *l, int y0) {
  const int half = l->grain / 2;
  return -(y0 + half) - half;
}

// Draws one chamber. `flip` mirrors it vertically: the lower chamber is the
// mirror of the upper one, so its heap grows upwards from the wide bottom.
static void draw_chamber(GContext *ctx, const Lattice *l, GPoint centre,
                         int first_row, int last_row, bool flip,
                         int32_t angle) {
  // The pixel a grain is turned about. An even grain has no middle pixel, so
  // this one sits half a pixel past it — the same half pixel for every grain,
  // which is why the lattice stays a lattice at any angle.
  const int half = l->grain / 2;
  const int32_t sin_a = sin_lookup(angle), cos_a = cos_lookup(angle);

  for (int r = first_row; r < last_row; r++) {
    const int n = glass_in_row(l, r);
    if (n <= 0) continue;
    const int y0 = row_y(l, r);
    for (int i = 0; i < n; i++) {
      // Every grain is placed from the centre outwards rather than stepped
      // along the row, so nothing drifts. Doubling the column index keeps it
      // integer: a row holds an odd number of grains, so col2 * cell stays even.
      const int col2 = 2 * i - (n - 1);
      int gx = (col2 * l->cell) / 2 - half;
      int gy = flip ? mirror_y(l, y0) : y0;

      if (angle != 0) {
        // Only the centre is rotated; the grain stays an axis-aligned square.
        // A square this small covers the same pixels at any angle, and there is
        // no offscreen surface in the SDK to rotate a raster with.
        const int cx = gx + half, cy = gy + half;
        const int rx = (cx * cos_a - cy * sin_a) / TRIG_MAX_RATIO;
        const int ry = (cx * sin_a + cy * cos_a) / TRIG_MAX_RATIO;
        gx = rx - half;
        gy = ry - half;
      }
      graphics_fill_rect(ctx, GRect(centre.x + gx, centre.y + gy, l->grain,
                                    l->grain),
                         0, GCornerNone);
    }
  }
}

// The lower chamber takes the mirrored rows [0, rows_gone) — the very ones that
// left the upper chamber.
void sand_draw(GContext *ctx, const Lattice *l, GPoint centre, int rows_gone,
               int32_t angle) {
  draw_chamber(ctx, l, centre, rows_gone, glass_rows(l), false, angle);
  draw_chamber(ctx, l, centre, 0, rows_gone, true, angle);
}

void sand_draw_stream(GContext *ctx, const Lattice *l, GPoint centre,
                      int rows_gone) {
  const int rows = glass_rows(l);
  // A falling grain leaves from the middle column of the row.
  const int gx = -(l->grain / 2);
  // The clear run the column has to fill: from the sand it has just left down
  // to whatever it lands on.
  const int top = row_y(l, rows - 1) + l->grain;
  // A heap is landed ON: the lowest grain may reach into its top row, and both
  // are sand. An empty chamber is landed on where its heap will start.
  const int landing = mirror_y(l, row_y(l, rows_gone > 0 ? rows_gone - 1 : 0));
  const int run = landing - top;
  const int gap = l->cell - l->grain;
  // The pitch of the lattice almost never divides the run, and a remainder
  // spent at one end shows: grains stuck together where the column leaves the
  // sand, or a hole where it meets the heap. Every gap takes an equal share.
  int n = (run - gap + l->cell / 2) / l->cell;

  if (n < 1) n = 1;
  while (n > 0 && n * l->grain > run) n--;
  const int slack = run - n * l->grain, slots = n + 1;
  int y = top;

  for (int i = 0; i < n; i++) {
    // Each gap is the step between two running totals rather than a share
    // rounded on its own, so the rounding cannot accumulate and the last grain
    // lands where the arithmetic says it should.
    y += (slack * (i + 1)) / slots - (slack * i) / slots;
    graphics_fill_rect(
        ctx, GRect(centre.x + gx, centre.y + y, l->grain, l->grain), 0,
        GCornerNone);
    y += l->grain;
  }
  // An empty chamber has only its floor, so the column would stop a gap above
  // bare glass. The grain that fills it goes exactly where the first row of the
  // heap will, and the run above ends at that row rather than covering it, so
  // the loop could not have drawn it.
  if (rows_gone == 0) {
    graphics_fill_rect(
        ctx, GRect(centre.x + gx, centre.y + landing, l->grain, l->grain), 0,
        GCornerNone);
  }
}
