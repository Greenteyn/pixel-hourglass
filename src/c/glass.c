#include "glass.h"

// The size ladder. Nine rows at every rung: what shrinks is the whole body, so
// the neck and the glass scale with it. The thickness drops to 3 low down —
// held at 5 a small body reads as twice as thick-walled, and a stroke has no
// even widths to put in between.
static const Lattice LADDER[] = {
    {7, 5, 11, 3, 6, 8, 5, 5},
    {6, 4, 11, 3, 6, 7, 4, 5},
    {5, 2, 11, 3, 6, 6, 3, 5},
    {4, 2, 11, 3, 6, 5, 2, 3},
    {3, 2, 11, 3, 6, 4, 1, 3},
};

#define LADDER_LEN ((int)(sizeof(LADDER) / sizeof(LADDER[0])))

// --- The lattice ------------------------------------------------------------
//
// Past `straight` the funnel closes in by two grains a row. The row pitch
// equals the column pitch, which is what puts the funnel wall at 45 degrees.

int glass_rows(const Lattice *l) { return l->straight + l->taper; }

int glass_in_row(const Lattice *l, int row) {
  if (row < l->straight) return l->width;
  const int n = 2 * (l->taper - (row - l->straight)) - 1;
  return n > 0 ? n : 0;
}

int glass_grain_inset(const Lattice *l) { return (l->cell - l->grain) / 2; }

// The chamber sits against the funnel: the bottom row of the lattice meets the
// point where the funnel begins.
int glass_chamber_top(const Lattice *l) {
  return -l->neck_h - glass_rows(l) * l->cell;
}

// --- The mid-line of the wall -----------------------------------------------
//
// The stroke follows the mid-line of the wall rather than the cavity: centred
// on the cavity it would eat half its width inwards and collapse the neck.
//
// The mid-line sits half a stroke out from the cavity everywhere except
// vertically at the neck, where two funnels meet in a mitre and it runs inside
// the corner instead.

static int half_stroke(const Lattice *l) { return (l->stroke + 1) / 2; }
static int p_neck(const Lattice *l) { return l->neck_w + half_stroke(l); }
static int h_neck(const Lattice *l) { return l->neck_h - 1; }

static int p_wall(const Lattice *l) {
  return p_neck(l) + (l->taper - 1) * l->cell;
}

static int h_funnel(const Lattice *l) {
  return h_neck(l) + (l->taper - 1) * l->cell;
}

// The room a row leaves beside itself. Nothing chooses it: the cavity is odd
// because the stroke is, the row is as wide as its grains and the gaps between
// them, and the rest splits in two.
static int side_gap(const Lattice *l) {
  const int cavity = 2 * p_wall(l) - l->stroke;
  const int row = (l->width - 1) * l->cell + l->grain;
  return (cavity - row) / 2;
}

// The sand keeps the same distance from the dome as it keeps from the wall, so
// the body only owes the part of it the cell does not already hold.
static int slack(const Lattice *l) {
  return side_gap(l) - glass_grain_inset(l);
}

static int h_top(const Lattice *l) {
  return l->neck_h + glass_rows(l) * l->cell + slack(l) + half_stroke(l);
}

// A stroke of odd width is symmetric about the centre of a pixel, so the
// outline always covers an odd number of columns.
int glass_w(const Lattice *l) { return 2 * p_wall(l) + l->stroke; }
int glass_h(const Lattice *l) { return 2 * h_top(l) + l->stroke; }

const Lattice *glass_pick(int max_h) {
  for (int i = 0; i < LADDER_LEN - 1; i++) {
    if (glass_h(&LADDER[i]) <= max_h) return &LADDER[i];
  }
  return &LADDER[LADDER_LEN - 1];
}

// --- The outline ------------------------------------------------------------
//
// Twelve bends: four outer corners of 90 degrees, four wall-to-funnel and four
// funnel-to-neck joints of 135. GPath has no curves, so a bend is given two
// bevels rather than one — a single bevel reads as a cut, two in a row read as
// an arc — which puts three points on a bend and 36 on the body.
//
// The neck mitres come out sharp all the same: no rung leaves them a radius
// their sagitta survives rounding at, so their three points collapse onto the
// corner and its two straight runs. They are kept because a stroke five pixels
// wide joins differently over them — dropping them costs each mitre a pixel.

#define QUAD_PTS 9
#define MID_PTS  (4 * QUAD_PTS)

static GPoint s_mid[MID_PTS];
static GPathInfo s_mid_info = {.num_points = MID_PTS, .points = s_mid};
static GPath *s_mid_path;
static const Lattice *s_laid_out;

// The bevels scale with the body, as the neck and the stroke already do: held
// at one size they would leave a small body reading as a rounder object.
static int r_corner(const Lattice *l) { return l->cell; }
static int r_wall(const Lattice *l) { return 2 * l->cell / 3; }

// Both neck bevels eat into the same straight run, so neither may take more
// than half of it. The neck is what runs out first down the ladder, and at the
// last rung there is nothing left to take.
static int r_neck(const Lattice *l) {
  const int r = l->cell / 3, half = h_neck(l);
  return r < half ? r : half;
}

// Rounded r/sqrt(2): the leg a bevel takes off a 45-degree run. Rounded once
// and mirrored into both coordinates, which is what lands the point the bevel
// returns on exactly on the funnel.
static int leg45(int r) { return (r * 181 + 128) >> 8; }

// A bevel puts its middle point on the bisector of the bend, one sagitta out.
// The bisector of a 135-degree bend is not diagonal, so the two coordinates
// take different shares of a sagitta that is a fifth of the radius.
static int sag_x(int r) { return (r * 47 + 128) >> 8; }
static int sag_y(int r) { return (r * 19 + 128) >> 8; }

// The top right quadrant, clockwise. The other three are its negations: a
// routine run per bend would break the symmetry, because integer division
// truncates towards zero and the middle points of opposite bends would then
// part by a pixel.
static void quadrant(const Lattice *l, GPoint *q) {
  const int P = p_wall(l), T = h_top(l);
  const int N = p_neck(l), Hn = h_neck(l), F = h_funnel(l);
  const int rc = r_corner(l), rw = r_wall(l), rn = r_neck(l);
  const int m = rc - leg45(rc);

  q[0] = GPoint(P - rc, -T);
  q[1] = GPoint(P - m, -T + m);
  q[2] = GPoint(P, -T + rc);

  q[3] = GPoint(P, -F - rw);
  q[4] = GPoint(P - sag_x(rw), -F - sag_y(rw));
  q[5] = GPoint(P - leg45(rw), -F + leg45(rw));

  q[6] = GPoint(N + leg45(rn), -Hn - leg45(rn));
  q[7] = GPoint(N + sag_x(rn), -Hn + sag_y(rn));
  q[8] = GPoint(N, -Hn + rn);
}

// Bevels that met, and a bend left sharp, name the same point twice; kept, they
// would stroke segments of zero length.
static int push(GPoint p, int n) {
  if (n > 0 && p.x == s_mid[n - 1].x && p.y == s_mid[n - 1].y) return n;
  s_mid[n] = p;
  return n + 1;
}

// Rewrites the points IN PLACE: gpath_create copies the pointer rather than the
// array, so the path created in glass_init keeps looking at this very memory.
// Keyed on the rung itself — the peek swaps rungs while the watchface runs.
static void glass_layout(const Lattice *l) {
  GPoint q[QUAD_PTS];
  int n = 0;

  if (l == s_laid_out) return;
  quadrant(l, q);
  // A mirrored quadrant is walked backwards, or the ring would stop being
  // clockwise and the stroke would cross itself at the neck.
  for (int k = 0; k < 4; k++) {
    const int sx = k < 2 ? 1 : -1, sy = (k == 0 || k == 3) ? 1 : -1;
    const bool back = k % 2 == 1;

    for (int i = 0; i < QUAD_PTS; i++) {
      const GPoint p = q[back ? QUAD_PTS - 1 - i : i];
      n = push(GPoint(sx * p.x, sy * p.y), n);
    }
  }
  s_mid_path->num_points = n;
  s_laid_out = l;
}

void glass_init(void) {
  s_mid_path = gpath_create(&s_mid_info);
  s_laid_out = NULL;
}

void glass_deinit(void) { gpath_destroy(s_mid_path); }

void glass_draw(GContext *ctx, const Lattice *l, GPoint centre, int32_t angle) {
  glass_layout(l);
  gpath_rotate_to(s_mid_path, angle);
  gpath_move_to(s_mid_path, centre);
  graphics_context_set_stroke_color(ctx, GColorVividCerulean);
  graphics_context_set_stroke_width(ctx, l->stroke);
  // Antialiasing widens the stroke by a pixel wherever a segment leaves the
  // axes, and at rest that pixel comes out of the gap the sand is meant to
  // keep. Off the right angles the blend falls along the diagonals instead.
  graphics_context_set_antialiased(ctx, angle % (TRIG_MAX_ANGLE / 4) != 0);
  gpath_draw_outline(ctx, s_mid_path);
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_width(ctx, 1);
}
