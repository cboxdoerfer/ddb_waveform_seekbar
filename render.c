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

#include <stdlib.h>
#include <stdbool.h>
#include <sys/param.h>
#include <math.h>
#include <assert.h>
#include <cairo.h>

#include "render.h"
#include "waveform.h"
#include "config.h"

#define LINE_WIDTH_DEFAULT (1.0)
#define LINE_WIDTH_BARS (1.0)
#define VALUES_PER_SAMPLE (3)
#define W_COLOR(X) (X)->r, (X)->g, (X)->b, (X)->a

typedef struct
{
    double x;
    double y;
} waveform_point_t;

typedef struct
{
    double x1, y1;
    double x2, y2;
} waveform_line_t;

void
waveform_data_render_free (waveform_data_render_t *w_render_ctx)
{
    if (!w_render_ctx) {
        return;
    }

    if (w_render_ctx->samples) {
        for (int ch = 0; ch < w_render_ctx->num_channels; ch++) {
            waveform_sample_t *samples = w_render_ctx->samples[ch];
            if (samples) {
                free (samples);
                w_render_ctx->samples[ch] = NULL;
            }
        }
        free (w_render_ctx->samples);
        w_render_ctx->samples = NULL;
    }
    free (w_render_ctx);
    w_render_ctx = NULL;

    return;
}

waveform_data_render_t *
waveform_data_render_new (int channels, int width)
{
    if (channels <= 0) {
        return NULL;
    }

    waveform_data_render_t *w_render_ctx = calloc (1, sizeof (waveform_data_render_t));
    assert (w_render_ctx != NULL);

    w_render_ctx->samples = calloc (channels, sizeof (waveform_sample_t *));
    assert (w_render_ctx->samples != NULL);

    for (int ch = 0; ch < channels; ch++) {
        w_render_ctx->samples[ch] = calloc (width, sizeof (waveform_sample_t));
        assert (w_render_ctx->samples[ch] != NULL);
    }

    w_render_ctx->num_channels = channels;
    w_render_ctx->num_samples = width;

    return w_render_ctx;
}

static int
waveform_data_render_build_sample (wavedata_t *wave_data,
                                   waveform_sample_t *sample,
                                   int sample_size,
                                   int channel,
                                   double start,
                                   double end)
{
    const int ch_offset = channel * VALUES_PER_SAMPLE;

    float min = 1.0;
    float max = -1.0;
    float rms = 0.0;

    const int s_end = floorf (sample_size * end);

    int counter = 0;
    for (int i = start; i < end; i++) {
        for (int pos = i * sample_size; pos < s_end; pos += sample_size, counter++) {
            int index = pos + ch_offset;
            float s_max = (float)wave_data->data[index]/1000;
            float s_min = (float)wave_data->data[index+1]/1000;
            float s_rms = (float)wave_data->data[index+2]/1000;
            max = MAX (max, s_max);
            min = MIN (min, s_min);
            rms += s_rms * s_rms;
        }
    }

    sample->max = max;
    sample->min = min;
    sample->rms = rms;

    return counter;
}

waveform_data_render_t *
waveform_render_data_build (wavedata_t *wave_data, int width, bool downmix_mono)
{
    const int channels_data = wave_data->channels;
    if (channels_data <= 0) {
        return NULL;
    }

    const int channels_render = CONFIG_MIX_TO_MONO ? 1 : channels_data;
    const int sample_size = VALUES_PER_SAMPLE * channels_data;
    const float num_samples_per_x = wave_data->data_len / (float)(width * sample_size);

    waveform_data_render_t *w_render_ctx = waveform_data_render_new (channels_render, width);

    for (int ch = 0; ch < w_render_ctx->num_channels; ch++) {
        waveform_sample_t *samples = w_render_ctx->samples[ch];

        int d_start = 0;

        for (int x = 0; x < width; x++) {
            const int d_end = MAX (floorf ((x+1) * num_samples_per_x),1);
            waveform_sample_t *sample = &samples[x];

            int counter = 0;
            if (CONFIG_MIX_TO_MONO) {
                for (int ch_data = 0; ch_data < channels_data; ch_data++) {
                    counter += waveform_data_render_build_sample (wave_data,
                                                                 sample,
                                                                 sample_size,
                                                                 ch_data,
                                                                 d_start,
                                                                 d_end);
                }
            }
            else {
                counter += waveform_data_render_build_sample (wave_data,
                                                             sample,
                                                             sample_size,
                                                             ch,
                                                             d_start,
                                                             d_end);
            }

            sample->rms /= counter;
            sample->rms = sqrt (sample->rms);

            d_start = d_end;
        }
    }

    return w_render_ctx;
}

enum SAMPLE_TYPE {
    SAMPLE_MAX,
    SAMPLE_MIN,
    SAMPLE_RMS_MAX,
    SAMPLE_RMS_MIN,
    N_SAMPLE_TYPES
};

typedef void (*waveform_render_sample_func)(cairo_t *cr_ctx,
                                            waveform_sample_t *sample,
                                            waveform_point_t *point,
                                            double y_scale_1,
                                            double y_scale_2);

/* copied from ardour3 */
static inline float
_log_meter (float power, double lower_db, double upper_db, double non_linearity)
{
    return (power < lower_db ? 0.0 : pow ((power - lower_db) / (upper_db - lower_db), non_linearity));
}

static inline float
alt_log_meter (float power)
{
    return _log_meter (power, -192.0, 0.0, 8.0);
}

static inline float
coefficient_to_dB (float coeff)
{
    return 20.0f * log10 (coeff);
}
/* end of ardour copy */

static inline float
sample_log_scale (float sample)
{
    float sample_log = 0.0;
    if (sample > 0.0) {
        sample_log = alt_log_meter (coefficient_to_dB (sample));
    }
    else {
        sample_log = -alt_log_meter (coefficient_to_dB (-sample));
    }

    return sample_log;
}

static float
sample_value_scale (float value, float scale, bool log_scale)
{
    if (log_scale) {
        value = sample_log_scale (value);
    }

    return value * scale;
}

static void
waveform_render_samples_loop_reverse (cairo_t *cr_ctx,
                                      waveform_sample_t *samples,
                                      waveform_render_sample_func *render_sample,
                                      double y_scale_1,
                                      double y_scale_2,
                                      double x_start,
                                      double y_start,
                                      double width)
{
    if (!render_sample) {
        return;
    }

    const int width_i = floor (width) - 1;

    for (int x = width_i; x >= x_start; x--) {
        waveform_sample_t *sample = &samples[x];
        waveform_point_t point = {x, y_start};

        (*render_sample)(cr_ctx, sample, &point, y_scale_1, y_scale_2);
    }
}

static void
waveform_render_samples_loop (cairo_t *cr_ctx,
                              waveform_sample_t *samples,
                              waveform_render_sample_func *render_sample,
                              double y_scale_1,
                              double y_scale_2,
                              double x_start,
                              double y_start,
                              double width)
{
    if (!render_sample) {
        return;
    }

    const int width_i = floor (width);

    for (int x = 0; x < width_i; x++) {
        waveform_sample_t *sample = &samples[x];
        waveform_point_t point = {x_start + x, y_start};

        (*render_sample)(cr_ctx, sample, &point, y_scale_1, y_scale_2);
    }
}

static void inline
waveform_render_bars_sample_generic (cairo_t *cr_ctx,
                                     double s1,
                                     double s2,
                                     waveform_point_t *point,
                                     double y_scale_1,
                                     double y_scale_2)
{
    const double x = point->x;
    const double y = point->y;
    const double y_1 = y - sample_value_scale (s1, y_scale_1, CONFIG_LOG_ENABLED);
    const double y_2 = y - sample_value_scale (s2, y_scale_2, CONFIG_LOG_ENABLED);

    cairo_move_to (cr_ctx, x, y_1);
    cairo_line_to (cr_ctx, x, y_2);
}

static void
waveform_render_bars_sample_minmax (cairo_t *cr_ctx,
                                    waveform_sample_t *sample,
                                    waveform_point_t *point,
                                    double y_scale_1,
                                    double y_scale_2)
{
    waveform_render_bars_sample_generic (cr_ctx,
                                         sample->max,
                                         sample->min,
                                         point,
                                         y_scale_1,
                                         y_scale_2);
}

static void
waveform_render_bars_sample_rms (cairo_t *cr_ctx,
                                 waveform_sample_t *sample,
                                 waveform_point_t *point,
                                 double y_scale_1,
                                 double y_scale_2)
{
    waveform_render_bars_sample_generic (cr_ctx,
                                         sample->rms,
                                         -sample->rms,
                                         point,
                                         y_scale_1,
                                         y_scale_2);

}

static cairo_pattern_t *
waveform_render_soundcloud_pattern_get (cairo_t *cr_ctx,
                                        waveform_colors_t *color,
                                        waveform_line_t *vec_pat)
{
    cairo_pattern_t *lin_pat = cairo_pattern_create_linear (vec_pat->x1,
                                                            vec_pat->y1,
                                                            vec_pat->x2,
                                                            vec_pat->y2);
    cairo_pattern_add_color_stop_rgba (lin_pat, 0.0, color->fg.r, color->fg.g, color->fg.b, 0.7);
    cairo_pattern_add_color_stop_rgba (lin_pat, 0.7, color->fg.r, color->fg.g, color->fg.b, 1.0);
    cairo_pattern_add_color_stop_rgba (lin_pat, 0.7, color->fg.r, color->fg.g, color->fg.b, 0.5);
    cairo_pattern_add_color_stop_rgba (lin_pat, 1.0, color->fg.r, color->fg.g, color->fg.b, 0.5);
    cairo_set_source (cr_ctx, lin_pat);

    return lin_pat;
}

static void
waveform_render_wave_bar_values (cairo_t *cr_ctx,
                                 waveform_sample_t *samples,
                                 waveform_colors_t *color,
                                 int type,
                                 waveform_rect_t *rect)
{
    double x = rect->x;
    double y = rect->y;
    double width = rect->width;
    double height = rect->height;

    double y_scale_1 = 0.5 * height;
    if (CONFIG_SOUNDCLOUD_STYLE) {
        y_scale_1 = 0.7 * height;
    }
    double y_scale_2 = height - y_scale_1;

    double y_center = y_scale_1 + y;

    cairo_move_to (cr_ctx, x, y_center);

    waveform_render_sample_func render_func = NULL;
    switch (type) {
        case SAMPLE_RMS_MAX:
        case SAMPLE_RMS_MIN:
            render_func = &waveform_render_bars_sample_rms;
            break;
        case SAMPLE_MAX:
        case SAMPLE_MIN:
            render_func = &waveform_render_bars_sample_minmax;
            break;
    }

    cairo_pattern_t *lin_pat = NULL;
    if (CONFIG_SOUNDCLOUD_STYLE) {
        waveform_line_t vec_pat = {
            .x1 = x,
            .y1 = y,
            .x2 = x,
            .y2 = y + height,
        };
        lin_pat = waveform_render_soundcloud_pattern_get (cr_ctx,
                                                          color,
                                                          &vec_pat);
    }

    waveform_render_samples_loop (cr_ctx,
                                  samples,
                                  &render_func,
                                  y_scale_1,
                                  y_scale_2,
                                  x,
                                  y_center,
                                  width);
    cairo_stroke (cr_ctx);

    if (lin_pat) {
        cairo_pattern_destroy (lin_pat);
        lin_pat = NULL;
    }
}

void
waveform_draw_wave_bars (waveform_sample_t *samples,
                         waveform_colors_t *colors,
                         cairo_t *cr_ctx,
                         waveform_rect_t *rect)
{
    cairo_set_line_width (cr_ctx, LINE_WIDTH_BARS);
    cairo_set_antialias (cr_ctx, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgba (cr_ctx, W_COLOR (&colors->fg));

    // draw min/max values
    waveform_render_wave_bar_values (cr_ctx,
                                     samples,
                                     colors,
                                     SAMPLE_MAX,
                                     rect);

    if (CONFIG_DISPLAY_RMS) {
        // draw rms values
        cairo_set_source_rgba (cr_ctx, W_COLOR (&colors->rms));
        waveform_render_wave_bar_values (cr_ctx,
                                         samples,
                                         colors,
                                         SAMPLE_RMS_MAX,
                                         rect);
    }

    return;
}

static void
waveform_render_default_sample_generic (cairo_t *cr_ctx,
                                        double sample,
                                        waveform_point_t *point,
                                        double y_scale)
{
    const double x = point->x;
    const double y = point->y - sample_value_scale (sample, y_scale, CONFIG_LOG_ENABLED);
    cairo_line_to (cr_ctx, x, y);
}

static void
waveform_render_default_sample_max (cairo_t *cr_ctx,
                                    waveform_sample_t *sample,
                                    waveform_point_t *point,
                                    double y_scale_1,
                                    double y_scale_2)
{
    waveform_render_default_sample_generic (cr_ctx,
                                            sample->max,
                                            point,
                                            y_scale_1);
}

static void
waveform_render_default_sample_min (cairo_t *cr_ctx,
                                    waveform_sample_t *sample,
                                    waveform_point_t *point,
                                    double y_scale_1,
                                    double y_scale_2)
{
    waveform_render_default_sample_generic (cr_ctx,
                                            sample->min,
                                            point,
                                            y_scale_1);
}

static void
waveform_render_default_sample_rms1 (cairo_t *cr_ctx,
                                     waveform_sample_t *sample,
                                     waveform_point_t *point,
                                     double y_scale_1,
                                     double y_scale_2)
{
    waveform_render_default_sample_generic (cr_ctx,
                                            sample->rms,
                                            point,
                                            y_scale_1);
}

static void
waveform_render_default_sample_rms2 (cairo_t *cr_ctx,
                                     waveform_sample_t *sample,
                                     waveform_point_t *point,
                                     double y_scale_1,
                                     double y_scale_2)
{
    waveform_render_default_sample_generic (cr_ctx,
                                            -sample->rms,
                                            point,
                                            y_scale_1);
}

enum SAMPLE_GROUPS {
    SAMPLE_MIN_MAX,
    SAMPLE_RMS_MIN_MAX,
    N_SAMPLE_GROUPS,
};

static void
waveform_render_wave_default_values (cairo_t *cr_ctx,
                                     waveform_sample_t *samples,
                                     waveform_colors_t *color,
                                     int type,
                                     waveform_rect_t *rect)
{
    double x = rect->x;
    double y = rect->y;
    double width = rect->width;
    double height = rect->height;

    waveform_render_sample_func render_func_1;
    waveform_render_sample_func render_func_2;

    switch (type) {
        case SAMPLE_MIN_MAX:
            render_func_1 = waveform_render_default_sample_max;
            render_func_2= waveform_render_default_sample_min;
            break;
        case SAMPLE_RMS_MIN_MAX:
            render_func_1= waveform_render_default_sample_rms1;
            render_func_2= waveform_render_default_sample_rms2;
            break;
        default:
            return;
    }

    double y_scale = 0.5 * height;
    double y_center = y_scale + y;
    if (CONFIG_SOUNDCLOUD_STYLE) {
        y_scale = 0.7 * height;
        y_center = y_scale + y;
    }

    cairo_pattern_t *lin_pat = NULL;
    if (CONFIG_SOUNDCLOUD_STYLE) {
        waveform_line_t vec_pat = {
            .x1 = x,
            .y1 = y,
            .x2 = x,
            .y2 = y + height,
        };
        lin_pat = waveform_render_soundcloud_pattern_get (cr_ctx, color, &vec_pat);
    }

    cairo_move_to (cr_ctx, x, y_center);
    waveform_render_samples_loop (cr_ctx,
                                  samples,
                                  &render_func_1,
                                  y_scale,
                                  y_scale,
                                  x,
                                  y_center,
                                  width);

    y_scale = height - y_scale;

    waveform_render_samples_loop_reverse (cr_ctx,
                                          samples,
                                          &render_func_2,
                                          y_scale,
                                          y_scale,
                                          x,
                                          y_center,
                                          width);
    if (!CONFIG_FILL_WAVEFORM) {
        cairo_stroke (cr_ctx);
    }
    else {
        cairo_line_to (cr_ctx, x, y);
        cairo_close_path (cr_ctx);
        cairo_fill (cr_ctx);
    }

    if (lin_pat) {
        cairo_pattern_destroy (lin_pat);
        lin_pat = NULL;
    }
}

void
waveform_draw_wave_default (waveform_sample_t *samples,
                            waveform_colors_t *colors,
                            cairo_t *cr_ctx,
                            waveform_rect_t *rect)
{
    cairo_set_line_width (cr_ctx, LINE_WIDTH_DEFAULT);
    cairo_set_antialias (cr_ctx, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_source_rgba (cr_ctx, W_COLOR (&colors->fg));

    waveform_render_wave_default_values (cr_ctx,
                                         samples,
                                         colors,
                                         SAMPLE_MIN_MAX,
                                         rect);

    if (CONFIG_DISPLAY_RMS) {
        cairo_set_source_rgba (cr_ctx, W_COLOR (&colors->rms));

        waveform_render_wave_default_values (cr_ctx,
                                             samples,
                                             colors,
                                             SAMPLE_RMS_MIN_MAX,
                                             rect);
    }

    return;
}

