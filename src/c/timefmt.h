// timefmt.h — pure formatters: no hardware, no globals, no drawing.

#pragma once

#include <pebble.h>

// "05:20" in 24-hour form, "5:20A" / "9:30P" in 12-hour form; the marker is not
// optional. Upper case on both sides: LECO_42_NUMBERS draws a lower-case `a`
// but takes `p` from the capital, and the two halves of the day looked unlike
// each other. Out-of-range tm gives "--:--". buf needs 8 bytes.
void timefmt_hm(char *buf, size_t size, const struct tm *t, bool use_24h);

// "SUN 13 SEP". Out-of-range tm gives an empty string. buf needs 12 bytes.
void timefmt_date(char *buf, size_t size, const struct tm *t);
