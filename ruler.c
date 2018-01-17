/*
    Waveform seekbar plugin for the DeaDBeeF audio player

    Copyright (C) 2017 Christian Boxdörfer <christian.boxdoerfer@posteo.de>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdbool.h>
#include "ruler.h"
#include "config.h"
#include "waveform.h"

#define TEXT_SPACING 20
#define TEXT_MARKER_SPACING 3
#define RULER_MAX_LABELS 30
#define RULER_LINE_WIDTH 1.0
#define RULER_FONT_SIZE 8.0

typedef enum
{
    TIME_ID_6H,
    TIME_ID_1H,
    TIME_ID_30M,
    TIME_ID_15M,
    TIME_ID_10M,
    TIME_ID_5M,
    TIME_ID_1M,
    TIME_ID_30S,
    TIME_ID_10S,
    TIME_ID_5S,
    TIME_ID_1S,
    TIME_ID_500MS,
    TIME_ID_100MS,
    N_TIME_IDS,
} TimeValueID;

typedef struct
{
    TimeValueID id;
    float value;
} ruler_time_value_t;

static ruler_time_value_t time_scale[] = {
    {   TIME_ID_6H,
        6 * 3600.f, // 6 Hours
    },
    {   TIME_ID_1H,
        3600.f, // 1 hour
    },
    {   TIME_ID_30M,
        1800.f, // 30 min
    },
    {   TIME_ID_15M,
        900.f,  // 15 min
    },
    {   TIME_ID_10M,
        600.f,  // 10 min
    },
    {   TIME_ID_5M,
        300.f,  // 5 min
    },
    {   TIME_ID_1M,
        60.f,   // 1 min
    },
    {   TIME_ID_30S,
        30.f,   // 30 sec
    },
    {   TIME_ID_10S,
        10.f,   // 10 sec
    },
    {   TIME_ID_5S,
        5.f,    // 5 sec
    },
    {   TIME_ID_1S,
        1.f,    // 1 sec
    },
    {   TIME_ID_500MS,
        0.5f,   // 0.5 sec
    },
    {   TIME_ID_100MS,
        0.1f,   // 0.1 sec
    },
};

typedef struct
{
    ruler_time_value_t value;
    // number of times this resolution fits into a given duration
    int n;
} ruler_time_resolution_t;

static double
ruler_text_height_get (cairo_t *cr)
{
    cairo_text_extents_t text_dim;
    cairo_text_extents (cr, "Test", &text_dim);
    return text_dim.height;
}

static void
ruler_format_time (char *dest, size_t dest_size, ruler_time_value_t *time_val, int n)
{
    const double time_in_seconds = n * time_val->value;

    const int hours = (int)time_in_seconds/3600;
    const int minutes = (int)time_in_seconds/60;
    const int seconds = (int)time_in_seconds;
    const int milliseconds = (int)(time_in_seconds * 10);

    int time_remaining = 0;
    switch (time_val->id) {
        case TIME_ID_6H:
        case TIME_ID_1H:
            snprintf (dest, dest_size, "%d:00:00", hours);
            break;
        case TIME_ID_30M:
        case TIME_ID_15M:
        case TIME_ID_10M:
        case TIME_ID_5M:
        case TIME_ID_1M:
            if (time_in_seconds >= 3600) {
                time_remaining = seconds % 3600 / 60;
                snprintf (dest, dest_size, "%d:%02d:00", hours, time_remaining);
            }
            else {
                snprintf (dest, dest_size, "%d:00", minutes);
            }
            break;
        case TIME_ID_30S:
        case TIME_ID_10S:
        case TIME_ID_5S:
        case TIME_ID_1S:
            if (time_in_seconds >= 60) {
                time_remaining = seconds % 60;
                snprintf (dest, dest_size, "%d:%02d", minutes, time_remaining);
            }
            else {
                snprintf (dest, dest_size, "0:%02d", seconds);
            }
            break;
        case TIME_ID_500MS:
        case TIME_ID_100MS:
            snprintf (dest, dest_size, "0.%d", milliseconds);
            break;
        default:
            return;
    }
}

static void
ruler_time_resolution_build (ruler_time_resolution_t *res, float duration)
{
    for (int i = 0; i < N_TIME_IDS; i++) {
        ruler_time_resolution_t *r = &res[i];
        r->value = time_scale[i];
        // Determine the number of times each time step fits into the given track duration
        // e.g. given a 2min10s track a 30s time step will fit 4 times
        r->n = floorf (duration / time_scale[i].value);
    }
}

static bool
ruler_time_fits_width (cairo_t *cr,
                       char *dest,
                       size_t dest_size,
                       ruler_time_resolution_t *res,
                       float duration,
                       double width)
{
    if (res->n > RULER_MAX_LABELS) {
        return false;
    }

    ruler_time_value_t *time_val = &res->value;
    double text_width = 0;
    const double x_start = time_val->value/duration * width;

    for (int i = 1; i <= res->n; i++) {
        ruler_format_time (dest, dest_size, time_val, i);
        cairo_text_extents_t text_dim;
        cairo_text_extents (cr, dest, &text_dim);
        text_width += text_dim.width + TEXT_SPACING;
    }
    return floor ((width-x_start)/text_width) >= 1 ? true : false;
}

// Determine the best time resolution for a given duration and width.
// Where "best" means:
// 1) high resolution (e.g. given a 2 min duration: four 30s steps are better than two 1min steps)
//
//   |         0:30         1:00       1:30           |  <-- better
//
//   |                      1:00                      |  <-- worse
//
// 2) given a resolution the resp. time labels must fit to the available width
//
//   E.g. given a 2 min duration and the following width
//   |                                                | <- max width
//
//   the following 10s resolution obviously doesn't work
//
//                                                    | max width
//                                                    V
//   |0:10 0:20 0:30 0:40 0:50 1:00 1:10 1:20 1:30 1:40 1:50 2:00|
//
//   since displaying all values up to 2min uses more space than available
//
static ruler_time_resolution_t *
ruler_time_find_resolution (cairo_t *cr,
                            ruler_time_resolution_t *resolutions,
                            float duration,
                            double width)
{
    ruler_time_resolution_t *res = NULL;

    int n_max = 0;

    for (int i = 0; i < N_TIME_IDS; i++) {
        ruler_time_resolution_t *res_tmp = &resolutions[i];
        if (res_tmp->n <= 0) {
            // resolution too low, e.g. duration: 3min, resolution: 1h
            continue;
        }
        char time_text[100] = "";
        // determine how many labels fit in to the width for the given resolution
        bool fits = ruler_time_fits_width (cr,
                                           time_text,
                                           sizeof (time_text)/sizeof (char),
                                           res_tmp,
                                           duration,
                                           width);
        // only consider resolutions which are able to display one or more labels
        // and prefer higher to lower resolutions
        if (fits && res_tmp->n > n_max) {
            res = res_tmp;
            n_max = res_tmp->n;
        }
    }
    return res;
}

void
waveform_render_ruler (cairo_t *cr_ctx,
                       waveform_colors_t *color,
                       float duration,
                       waveform_rect_t *rect)
{
    // Draw background
    cairo_set_source_rgba (cr_ctx, color->bg.r, color->bg.g, color->bg.b, 1.0);
    cairo_rectangle (cr_ctx, rect->x, rect->y, rect->width, rect->height);
    cairo_fill (cr_ctx);

    cairo_set_antialias (cr_ctx, CAIRO_ANTIALIAS_NONE);
    cairo_set_line_width (cr_ctx, RULER_LINE_WIDTH);
    cairo_set_font_size (cr_ctx, RULER_FONT_SIZE);
    cairo_set_source_rgba (cr_ctx, 0.0, 0.0, 0.0, 1.0);

    // Draw separator
    //         0:30     1:00     1:30     2:00
    // -> ------------------------------------
    //
    //                Waveform
    //
    cairo_move_to (cr_ctx, rect->x, rect->height);
    cairo_line_to (cr_ctx, rect->width, rect->height);
    cairo_stroke (cr_ctx);

    if (duration <= 0.f) {
        // no duration, no ruler.
        return;
    }

    ruler_time_resolution_t resolutions[N_TIME_IDS];
    ruler_time_resolution_build (resolutions, duration);


    ruler_time_resolution_t *res = ruler_time_find_resolution (cr_ctx,
                                                               resolutions,
                                                               duration,
                                                               rect->width);
    if (!res) {
        return;
    }

    const double x_start = res->value.value/duration * rect->width;
    const double center = (rect->height - RULER_LINE_WIDTH)/2;
    const double y = center + ruler_text_height_get (cr_ctx)/2;

    double x = rect->x + x_start;
    for (int i = 0; i < res->n; i++) {
        // Draw time markers
        //
        //       |
        //       v
        //       | 0:30     | 1:00    | 1:30    | 2:00
        // ----------------------------------------------
        //
        //                   Waveform
        //
        cairo_move_to (cr_ctx, x, rect->height/2);
        cairo_line_to (cr_ctx, x, rect->height);
        cairo_stroke (cr_ctx);

        // Draw time labels
        //
        //          |
        //          v
        //       | 0:30     | 1:00    | 1:30    | 2:00
        // ----------------------------------------------
        //
        //                   Waveform
        //
        cairo_move_to (cr_ctx, x + TEXT_MARKER_SPACING, y);
        char time_text[100] = "";
        ruler_format_time (time_text, sizeof (time_text)/sizeof (char), &res->value, i+1);
        cairo_show_text (cr_ctx, time_text);
        x += x_start;
    }
}

