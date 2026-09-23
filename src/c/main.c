// main.c — the layout, the clock and the frame. The body of the hourglass and
// its sand live in glass.c and sand.c.

#include <pebble.h>
#include <stdlib.h>

#include "glass.h"
#include "sand.h"
#include "timefmt.h"

// Vertical budget. The date and the time never change size and keep a margin
// of their own; all the slack goes to the body. On a square screen it is spent
// to the pixel: a band a pixel shorter drops the body a rung.
#define M_TOP  6
#define DATE_H 18
#define TIME_H 42
#define M_BOT  PBL_IF_ROUND_ELSE(16, 6)
#define G_MIN  6

// The ink sits lower in the box than the nominal size, so the box is drawn
// taller than the budget the layout spends on the date.
#define DATE_BOX_H 22

// LECO stands its digits on the floor of its box and leaves the rows above
// them blank, so the band below is measured to the ink and not to the box.
#define TIME_BOX_BLANK 13

static Window *s_window;
static Layer *s_layer;

typedef struct {
  const Lattice *lat;
  GPoint centre;
  int date_y;
  int time_y;
} Layout;

static Layout s_layout;

// localtime() inside an update proc freezes on some devices, with `now`
// advancing while the hour and the minute stand still. Everything drawn comes
// from this cache instead, filled by the firmware tick.
static struct tm s_now;

// --- The level of the sand --------------------------------------------------
//
// One cycle drains the upper chamber row by row. Nothing derives a step length
// from the period: one the row count does not divide has no whole-second step
// at all, so the level and the next boundary are read off the clock instead.
static int s_cycle_sec = 60 * 60;

// Truncation always leaves the last row behind, so the level runs 0..rows-1 and
// the upper chamber empties in the same frame that refills it. Taking that
// frame apart is the flip's job, not the clock's.
static int s_rows_gone;
static AppTimer *s_step_timer;
static int s_cycle_index;

static void flip_start(void);

// Seconds since local midnight, and which cycle of the run they fall in: where
// a cycle starts follows from the period, and an hour-long one starts on the
// hour. The day belongs in the index — with a period as long as the day the
// seconds below it start from zero at every midnight, and a body watching only
// those would never turn over.
static int day_seconds(int *cycle) {
  const time_t now = time(NULL);
  const struct tm *t = localtime(&now);
  const int day_sec = (t->tm_hour * 60 + t->tm_min) * 60 + t->tm_sec;

  *cycle = t->tm_yday * (24 * 60 * 60 / s_cycle_sec) + day_sec / s_cycle_sec;
  return day_sec;
}

static int level_at(int sec, int rows) { return sec * rows / s_cycle_sec; }

// First second of the cycle at which `level + 1` rows are gone. Rounds UP on
// purpose: rounded down it lands on the last second the old level still holds,
// and the timer below would re-arm at zero delay until that second is out.
static int next_boundary(int level, int rows) {
  return ((level + 1) * s_cycle_sec + rows - 1) / rows;
}

static bool level_refresh(void);

static void step_timeout(void *context) {
  s_step_timer = NULL;
  // The timer only fires on a boundary: a safety net, not a filter.
  if (level_refresh()) layer_mark_dirty(s_layer);
}

// The minute tick cannot carry the step on its own — a boundary need not fall
// on a minute — and seconds are never subscribed to, so the level is read off
// the clock and a timer armed for the next row. The end of a cycle is spotted
// here as well: both the tick and the timer come through this one place.
static bool level_refresh(void) {
  const int rows = glass_rows(s_layout.lat);
  int cycle;
  const int day_sec = day_seconds(&cycle);
  const int sec = day_sec % s_cycle_sec;
  const int level = level_at(sec, rows);
  const bool moved = level != s_rows_gone;

  if (cycle != s_cycle_index) {
    s_cycle_index = cycle;
    flip_start();
  }
  s_rows_gone = level;
  if (s_step_timer) app_timer_cancel(s_step_timer);
  s_step_timer = app_timer_register(
      (uint32_t)(next_boundary(level, rows) - sec) * 1000, step_timeout, NULL);
  return moved;
}

// --- The period as a setting ------------------------------------------------

// A persist key is the whole name of a stored value: never reuse this number.
#define PERSIST_CYCLE_SEC 1

// A period the day does not divide leaves a short cycle at midnight — the body
// would turn over from a chamber that never emptied. The lower bound is the
// shortest period the settings page offers; zero would divide by zero
// everywhere the period is used.
static bool period_is_sound(int sec) {
  const int day = 24 * 60 * 60;

  return sec >= 60 && day % sec == 0;
}

// The cycle index is re-seeded from the new period BEFORE the level is read:
// the index still standing counts in the old one, and a refresh that found the
// two disagreeing would turn the body over on a settings change. Nothing waits
// for a turn in progress — the period is not on the drawing path.
static void period_set(int sec) {
  if (sec == s_cycle_sec) return;
  s_cycle_sec = sec;
  day_seconds(&s_cycle_index);
  if (level_refresh()) layer_mark_dirty(s_layer);
}

// A select field carries a string, so the value arrives as a CSTRING and
// reading it as an int32 would take the bytes of the digits for a number.
static void inbox_received(DictionaryIterator *iter, void *context) {
  const Tuple *t = dict_find(iter, MESSAGE_KEY_CyclePeriod);

  if (!t) return;
  const int sec = (t->type == TUPLE_CSTRING) ? atoi(t->value->cstring)
                                             : (int)t->value->int32;
  if (!period_is_sound(sec)) return;
  persist_write_int(PERSIST_CYCLE_SEC, sec);
  period_set(sec);
}

// --- The layout -------------------------------------------------------------

#if defined(PBL_ROUND)
// Bitwise on purpose: the Newton form never settles on k*k - 1.
static uint32_t isqrt32(uint32_t n) {
  uint32_t root = 0, rem = n, bit = 1UL << 30;

  while (bit > rem) bit >>= 2;
  while (bit) {
    if (rem >= root + bit) {
      rem -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return root;
}
#endif

// Lowest row the time is allowed to reach. On a round screen the mask decides
// it: a band w wide has to keep d >= R - sqrt(R*R - (w/2)*(w/2)) clear of the
// edge, measured against the screen and never against the unobstructed area —
// the peek pushes the time towards the centre, where there is more room.
//
// Sized against the widest string EITHER format can draw, never the current
// minute and never the format in force: a clearance that moved with the digits
// would carry the time up and down a screen on which nothing else had changed.
static int time_floor(GRect screen) {
  const int bottom = screen.origin.y + screen.size.h;
#if defined(PBL_ROUND)
  const int span =
      graphics_text_layout_get_content_size(
          "12:88A", fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS), screen,
          GTextOverflowModeWordWrap, GTextAlignmentCenter)
          .w;
  const int r = screen.size.w / 2, half = (span + 1) / 2;

  if (half < r) {
    return bottom - (r - (int)isqrt32((uint32_t)(r * r - half * half)));
  }
#endif
  return bottom;
}

static void layout_recalc(GRect u) {
  const GRect screen = layer_get_bounds(s_layer);
  const int date_y = u.origin.y + M_TOP;
  const int by_margin = u.origin.y + u.size.h - M_BOT;
  const int by_mask = time_floor(screen);
  const int time_y = (by_margin < by_mask ? by_margin : by_mask) - TIME_H;
  const int band_top = date_y + DATE_H;
  const int band_h = time_y + TIME_BOX_BLANK - band_top;
  // Picked after the clamp above, so the body gives up the room the mask takes
  // rather than the time overrunning it.
  const Lattice *lat = glass_pick(band_h - 2 * G_MIN);
  const int gap = (band_h - glass_h(lat)) / 2;

  s_layout.lat = lat;
  s_layout.date_y = date_y;
  s_layout.time_y = time_y;
  s_layout.centre = GPoint(screen.origin.x + screen.size.w / 2,
                           band_top + gap + glass_h(lat) / 2);
}

// --- The flip ---------------------------------------------------------------
//
// A cycle ends with the body turning half a circle. Silhouette and lattice are
// symmetric about either axis, so the last frame of the turn covers a still
// body with a full upper chamber and the handover cannot be seen.
//
// The full lower chamber the body carries round belongs to the turn rather
// than to `s_rows_gone`: written into the variable it would be overwritten
// halfway, because a cycle ends on a minute as readily as not.

#define FLIP_MS 700

static Animation *s_flip; // non-NULL only while the body is turning
static int32_t s_flip_angle;
static GRect s_area_deferred;
static bool s_area_waiting;

static void flip_update(Animation *a, const AnimationProgress p) {
  // int64: the product outgrows int32 halfway through the turn, and an angle
  // that wrapped there would swing the body back rather than stop it.
  s_flip_angle =
      (int32_t)(((int64_t)TRIG_MAX_ANGLE * p) / (2 * ANIMATION_NORMALIZED_MAX));
  layer_mark_dirty(s_layer);
}

// The stopped handler is the only place an animation may be destroyed.
static void flip_stopped(Animation *a, bool finished, void *context) {
  animation_destroy(a);
  s_flip = NULL;
  s_flip_angle = 0;
  if (s_area_waiting) {
    s_area_waiting = false;
    layout_recalc(s_area_deferred);
  }
  // Back to a level read off the clock, which is zero at the head of a cycle.
  level_refresh();
  layer_mark_dirty(s_layer);
}

// A fresh animation every turn: the setters do nothing once one is scheduled.
static void flip_start(void) {
  static const AnimationImplementation impl = {.update = flip_update};

  if (s_flip) return;
  // The cycle may have ended on the timer rather than on the tick, and HH:MM
  // may not lag a whole turn behind it.
  const time_t now = time(NULL);
  s_now = *localtime(&now);

  s_flip_angle = 0;
  s_flip = animation_create();
  animation_set_implementation(s_flip, &impl);
  animation_set_duration(s_flip, FLIP_MS);
  animation_set_curve(s_flip, AnimationCurveEaseInOut);
  animation_set_handlers(s_flip, (AnimationHandlers){.stopped = flip_stopped},
                         NULL);
  animation_schedule(s_flip);
}

// Subscribed to will_change alone: the body takes its new size as the peek
// starts moving, rather than being laid out again on every frame the peek
// animates through.
static void area_will_change(GRect final_area, void *context) {
  // A peek arriving mid-turn would change the rung and the centre between two
  // frames of the same rotation. It waits for the turn to end instead.
  if (s_flip) {
    s_area_deferred = final_area;
    s_area_waiting = true;
    return;
  }
  layout_recalc(final_area);
  // The level rides through untouched, and the step timer with it: every rung
  // of the ladder holds nine rows, so a rung is the same fraction of the
  // chamber.
  layer_mark_dirty(s_layer);
}

static void update_proc(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  char buf[12];

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  glass_draw(ctx, s_layout.lat, s_layout.centre, s_flip_angle);
  graphics_context_set_fill_color(ctx, GColorFolly);
  sand_draw(ctx, s_layout.lat, s_layout.centre,
            s_flip ? glass_rows(s_layout.lat) : s_rows_gone, s_flip_angle);
  // Nothing runs through the neck of a body being turned over. Asked of the
  // animation rather than of the angle, which flip_start zeroes before it
  // schedules: the stream would flash on the first frame of a turn.
  if (!s_flip) sand_draw_stream(ctx, s_layout.lat, s_layout.centre, s_rows_gone);

  // Both strings go on last: a body in mid-turn reaches nine pixels past its
  // resting outline, far enough on a square screen to cross the rows the text
  // stands on, and whichever is drawn first loses that ink. The date is bold
  // because grey on black goes thin at arm's length.
  timefmt_date(buf, sizeof buf, &s_now);
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(b.origin.x, s_layout.date_y, b.size.w, DATE_BOX_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);

  timefmt_hm(buf, sizeof buf, &s_now, clock_is_24h_style());
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
                     GRect(b.origin.x, s_layout.time_y, b.size.w, TIME_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  s_now = *tick_time;
  // Re-arming from the tick is also what pulls the level back in line after the
  // clock is set by hand or by the network.
  level_refresh();
  // HH:MM has moved, so the frame is due whatever the sand did.
  layer_mark_dirty(s_layer);
}

static void init(void) {
  glass_init();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  s_layer = layer_create(layer_get_bounds(window_get_root_layer(s_window)));
  layer_set_update_proc(s_layer, update_proc);
  layer_add_child(window_get_root_layer(s_window), s_layer);
  window_stack_push(s_window, false);

  // A peek already up when the watchface starts raises no event of its own, so
  // the first layout reads the area rather than waiting to be told about it.
  layout_recalc(layer_get_unobstructed_bounds(s_layer));

  // Read before the cycle index is seeded from it. An index seeded from the
  // default would disagree with the stored period, and the first tick after
  // that would turn the body over — on every launch, not only the first.
  if (persist_exists(PERSIST_CYCLE_SEC)) {
    const int sec = persist_read_int(PERSIST_CYCLE_SEC);

    if (period_is_sound(sec)) s_cycle_sec = sec;
  }

  // Seeded outside the update proc, so the first frame does not sit at midnight
  // waiting for the first tick.
  const time_t now = time(NULL);
  s_now = *localtime(&now);
  day_seconds(&s_cycle_index);
  level_refresh();

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  unobstructed_area_service_subscribe(
      (UnobstructedAreaHandlers){.will_change = area_will_change}, NULL);

  // One short key fits the smallest inbox the SDK offers, and nothing is ever
  // sent the other way.
  app_message_register_inbox_received(inbox_received);
  app_message_open(APP_MESSAGE_INBOX_SIZE_MINIMUM,
                   APP_MESSAGE_OUTBOX_SIZE_MINIMUM);
}

static void deinit(void) {
  // Unscheduling runs the stopped handler, which arms the step timer again:
  // cancelling it first would leave that one behind.
  if (s_flip) animation_unschedule(s_flip);
  if (s_step_timer) app_timer_cancel(s_step_timer);
  app_message_deregister_callbacks();
  unobstructed_area_service_unsubscribe();
  tick_timer_service_unsubscribe();
  layer_destroy(s_layer);
  window_destroy(s_window);
  glass_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
