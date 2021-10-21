/*
    Waveform seekbar plugin for the DeaDBeeF audio player

    Copyright (C) 2014 Christian Boxdörfer <christian.boxdoerfer@posteo.de>

    Based on sndfile-tools waveform by Erik de Castro Lopo.
        waveform.c - v1.04
        Copyright (C) 2007-2012 Erik de Castro Lopo <erikd@mega-nerd.com>
        Copyright (C) 2012 Robin Gareus <robin@gareus.org>
        Copyright (C) 2013 driedfruit <driedfruit@mindloop.net>

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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <deadbeef/deadbeef.h>
#include <deadbeef/gtkui_api.h>

#include "support.h"
#include "cache.h"
#include "config.h"
#include "config_dialog.h"
#include "utils.h"
#include "waveform.h"
#include "render.h"
#include "ruler.h"

#define W_COLOR(X) (X)->r, (X)->g, (X)->b, (X)->a

//#define M_PI (3.1415926535897932384626433832795029)
// min, max, rms
#define VALUES_PER_SAMPLE (3)
#define MAX_CHANNELS (6)
#define MAX_SAMPLES (4096)
#define DISTANCE_THRESHOLD (100)


/* Global variables */
DB_functions_t *deadbeef = NULL;
static DB_misc_t plugin;
static ddb_gtkui_t *gtkui_plugin = NULL;

static char cache_path[PATH_MAX];

enum PLAYBACK_STATUS { STOPPED = 0, PLAYING = 1, PAUSED = 2 };
static int playback_status = STOPPED;
static int waveform_instancecount;

typedef struct
{
    ddb_gtkui_widget_t base;
    GtkWidget *popup;
    GtkWidget *popup_item;
    GtkWidget *drawarea;
    GtkWidget *ruler;
    GtkWidget *frame;
    guint drawtimer;
    guint resizetimer;
    wavedata_t *wave;

    waveform_colors_t colors;
    waveform_colors_t colors_shaded;

    size_t max_buffer_len;
    int seekbar_moving;
    float seekbar_move_x;
    float seekbar_move_x_clicked;
    float height;
    float width;
    float pos_last;
    intptr_t mutex;
    cairo_surface_t *surf;
    cairo_surface_t *surf_shaded;
} waveform_t;

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

static gboolean
waveform_draw_cb (void *user_data);

static gboolean
waveform_redraw_cb (void *user_data);

static gboolean
ruler_redraw_cb (void *user_data);

static void
waveform_draw (void *user_data, int shaded);

static gboolean
waveform_set_refresh_interval (void *user_data, int interval);

static color_t
waveform_color_contrast (color_t *color)
{
    // Counting the perceptive luminance - human eye favors green color...
    double a = 1.0 - ( 2.0 * color->r + 3.0 * color->g + color->b) / 6.0;
    if (a < 0.5)
        a = 0.0; // bright colors - black font
    else
        a = 1.0; // dark colors - white font

    color_t color_cont = {
        .r = a,
        .g = a,
        .b = a,
        .a = 1.0,
    };
    return color_cont;
}

static void
waveform_colors_update (waveform_t *w)
{
    w->colors.fg = (color_t) {
        CONFIG_FG_COLOR.red/65535.f,
        CONFIG_FG_COLOR.green/65535.f,
        CONFIG_FG_COLOR.blue/65535.f,
        1.0
    };
    w->colors.bg = (color_t) {
        CONFIG_BG_COLOR.red/65535.f,
        CONFIG_BG_COLOR.green/65535.f,
        CONFIG_BG_COLOR.blue/65535.f,
        1.0
    };
    w->colors.rms = (color_t) {
        CONFIG_FG_RMS_COLOR.red/65535.f,
        CONFIG_FG_RMS_COLOR.green/65535.f,
        CONFIG_FG_RMS_COLOR.blue/65535.f,
        1.0
    };

    w->colors.pb = (color_t) {
        CONFIG_PB_COLOR.red/65535.f,
        CONFIG_PB_COLOR.green/65535.f,
        CONFIG_PB_COLOR.blue/65535.f,
        1.0
    };

    w->colors.rlr = (color_t) {
        CONFIG_RLR_COLOR.red/65535.f,
        CONFIG_RLR_COLOR.green/65535.f,
        CONFIG_RLR_COLOR.blue/65535.f,
        CONFIG_RLR_ALPHA/65535.f
    };

    w->colors.font = waveform_color_contrast (&w->colors.bg);
    w->colors.font_pb = waveform_color_contrast (&w->colors.pb);

    w->colors_shaded.fg = w->colors.pb;
    w->colors_shaded.bg = w->colors.bg;
    w->colors_shaded.pb = w->colors.pb;
    w->colors_shaded.pb.a = CONFIG_PB_ALPHA/65535.f;

    w->colors_shaded.rms = (color_t) {
        0.8 * w->colors.pb.r,
        0.8 * w->colors.pb.g,
        0.8 * w->colors.pb.b,
        1.0
    };
}

static int
on_config_changed (void *widget)
{
    waveform_t *w = (waveform_t *) widget;
    load_config ();
    waveform_colors_update (w);
    // enable/disable border
    switch (CONFIG_BORDER_WIDTH) {
        case 0:
            gtk_frame_set_shadow_type ((GtkFrame *)w->frame, GTK_SHADOW_NONE);
            break;
        case 1:
            gtk_frame_set_shadow_type ((GtkFrame *)w->frame, GTK_SHADOW_IN);
            break;
    }
    switch (CONFIG_DISPLAY_RULER) {
        case 0:
            gtk_widget_hide (w->ruler);
            break;
        case 1:
            gtk_widget_show (w->ruler);
            break;
    }

    waveform_set_refresh_interval (w, CONFIG_REFRESH_INTERVAL);
    g_idle_add (waveform_redraw_cb, w);
    g_idle_add (ruler_redraw_cb, w);
    return 0;
}

static void
make_cache_dir (char *path, int size)
{
    const char *cache_dir = g_get_user_cache_dir ();
    if (cache_dir) {
        snprintf (path, size, "%s/deadbeef/waveform_seekbar", cache_dir);
        g_mkdir_with_parents (path, 0755);
    }
}

static char *
waveform_format_uri (DB_playItem_t *it, const char *uri)
{
    if (!it || !uri) {
        return NULL;
    }
    const int key_len = strlen (uri) + 10;
    char *key = malloc (key_len);
    if (deadbeef->pl_get_item_flags (it) & DDB_IS_SUBTRACK) {
        int subtrack = deadbeef->pl_find_meta_int (it, ":TRACKNUM", 0);
        snprintf (key, key_len, "%d%s", subtrack, uri);
    }
    else {
        snprintf (key, key_len, "%s", uri);
    }
    return key;
}

enum BORDERS
{
    CORNER_NONE        = 0,
    CORNER_TOPLEFT     = 1,
    CORNER_TOPRIGHT    = 2,
    CORNER_BOTTOMLEFT  = 4,
    CORNER_BOTTOMRIGHT = 8,
    CORNER_ALL         = 15
};

static void
clearlooks_rounded_rectangle (cairo_t * cr,
                              double x,
                              double y,
                              double w,
                              double h,
                              double radius,
                              uint8_t corners)
{
    if (radius < 0.01 || (corners == CORNER_NONE)) {
        cairo_rectangle (cr, x, y, w, h);
        return;
    }

    if (corners & CORNER_TOPLEFT)
        cairo_move_to (cr, x + radius, y);
    else
        cairo_move_to (cr, x, y);

    if (corners & CORNER_TOPRIGHT)
        cairo_arc (cr, x + w - radius, y + radius, radius, M_PI * 1.5, M_PI * 2);
    else
        cairo_line_to (cr, x + w, y);

    if (corners & CORNER_BOTTOMRIGHT)
        cairo_arc (cr, x + w - radius, y + h - radius, radius, 0, M_PI * 0.5);
    else
        cairo_line_to (cr, x + w, y + h);

    if (corners & CORNER_BOTTOMLEFT)
        cairo_arc (cr, x + radius, y + h - radius, radius, M_PI * 0.5, M_PI);
    else
        cairo_line_to (cr, x, y + h);

    if (corners & CORNER_TOPLEFT)
        cairo_arc (cr, x + radius, y + radius, radius, M_PI, M_PI * 1.5);
    else
        cairo_line_to (cr, x, y);

}

static inline void
waveform_draw_cairo_rectangle (cairo_t *cr, color_t *clr, waveform_rect_t *rect)
{
    cairo_set_source_rgba (cr, W_COLOR (clr));
    cairo_rectangle (cr, rect->x, rect->y, rect->width, rect->height);
    cairo_fill (cr);
}

static gboolean
ruler_redraw_cb (void *user_data)
{
    waveform_t *w = user_data;
    gtk_widget_queue_draw (w->ruler);
    return FALSE;
}

static gboolean
waveform_draw_cb (void *user_data)
{
    waveform_t *w = user_data;

    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (!trk) {
        return FALSE;
    }
    
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);
    const int width = a.width;
    const int height = a.height;
    const float dur = deadbeef->pl_get_item_duration (trk);
    deadbeef->pl_item_unref (trk);

    const float pos = (deadbeef->streamer_get_playpos () * width)/ dur;
    if (pos < w->pos_last) {
        w->pos_last = 0;
    }

    const float x = floorf (w->pos_last - CONFIG_CURSOR_WIDTH);
    const float dx = ceilf (pos - w->pos_last + CONFIG_CURSOR_WIDTH);
    gtk_widget_queue_draw_area (w->drawarea, x, 0, dx, height);

    w->pos_last = pos;

    return TRUE;
}

static gboolean
waveform_redraw_cb (void *user_data)
{
    waveform_t *w = user_data;
    if (w->resizetimer) {
        g_source_remove (w->resizetimer);
        w->resizetimer = 0;
    }
    waveform_draw (w, 0);
    waveform_draw (w, 1);
    gtk_widget_queue_draw (w->drawarea);
    return FALSE;
}

static void
waveform_draw_text (cairo_t *cr, waveform_colors_t *color, const char *text, double x, double y)
{
    cairo_set_source_rgba (cr, W_COLOR (&color->font));
    cairo_set_font_size (cr, CONFIG_FONT_SIZE);

    cairo_text_extents_t ex;
    cairo_text_extents (cr, text, &ex);
    const double text_x = x - ex.width/2;
    const double text_y = y + ex.height/2;
    cairo_move_to (cr, text_x, text_y);
    cairo_show_text (cr, text);
}

static void
waveform_draw_seeking_cursor (waveform_t *w, cairo_t *cr, float duration, waveform_rect_t *rect)
{
    float seek_pos = floor(CLAMP (w->seekbar_move_x, rect->x, rect->x + rect->width));

    waveform_rect_t cursor_rect = {
        .x = seek_pos - CONFIG_CURSOR_WIDTH,
        .y = rect->y,
        .width = CONFIG_CURSOR_WIDTH,
        .height = rect->height,
    };
    waveform_draw_cairo_rectangle (cr, &w->colors.pb, &cursor_rect);

    if (w->seekbar_move_x != w->seekbar_move_x_clicked || w->seekbar_move_x_clicked == -1) {
        w->seekbar_move_x_clicked = -1;

        const float cur_time = CLAMP (w->seekbar_move_x * duration / rect->width, 0, duration);
        const int hr = cur_time / 3600;
        const int mn = (cur_time - hr * 3600)/60;
        const int sc = cur_time - hr * 3600 - mn * 60;

        char s[100] = "";
        snprintf (s, sizeof (s), "%02d:%02d:%02d", hr, mn, sc);

        cairo_set_source_rgba (cr, W_COLOR (&w->colors.pb));
        cairo_set_font_size (cr, CONFIG_FONT_SIZE);
        cairo_select_font_face (cr,
                                "monospace",
                                CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);


        cairo_text_extents_t text_size;
        cairo_text_extents (cr, s, &text_size);

        const double rec_padding = 5;
        const double rec_padding_total = 2 * rec_padding;
        const double rec_width = ceil (text_size.x_advance) + rec_padding_total;
        const double rec_height = ceil (text_size.height) + rec_padding_total;
        double rec_pos = ceil (seek_pos - rec_width);
        double text_pos = rec_pos + rec_padding;

        //uint8_t corners = CORNER_TOPLEFT | CORNER_BOTTOMLEFT;
        uint8_t corners = CORNER_NONE;
        if (seek_pos < rec_width) {
            rec_pos = 0;
            text_pos = rec_pos + rec_padding;
            //corners = CORNER_TOPRIGHT | CORNER_BOTTOMRIGHT;
        }


        clearlooks_rounded_rectangle (cr, rec_pos, (rect->height - text_size.height - rec_padding_total)/2, rec_width, rec_height, 3, corners);
        cairo_fill (cr);
        cairo_move_to (cr, text_pos, (rect->height + text_size.height)/2);
        cairo_set_source_rgba (cr, W_COLOR (&w->colors.font_pb));
        cairo_show_text (cr, s);
    }
}

static void
waveform_seekbar_draw (gpointer user_data, cairo_t *cr, waveform_rect_t *rect)
{
    waveform_t *w = user_data;
    if (playback_status == STOPPED) {
        return;
    }
    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (!trk) {
        return;
    }

    const double left = rect->x;
    const double top = rect->y;
    const double width = rect->width;
    const double height = rect->height;

    const float dur = deadbeef->pl_get_item_duration (trk);
    const float pos = (deadbeef->streamer_get_playpos () * width)/ dur + left;
    int cursor_width = CONFIG_CURSOR_WIDTH;

    if (!deadbeef->is_local_file (deadbeef->pl_find_meta_raw (trk, ":URI"))) {
        if (w->drawtimer) {
            g_source_remove (w->drawtimer);
            w->drawtimer = 0;
        }
        waveform_draw_cairo_rectangle (cr, &w->colors.bg, rect);
        waveform_draw_text (cr, &w->colors, "Streaming...", width/2,height/2);
    }
    else {
        if (height != w->height || width != w->width) {
            cairo_save (cr);
            cairo_translate (cr, 0, 0);
            cairo_scale (cr, width/w->width, height/w->height);
            cairo_set_source_surface (cr, w->surf_shaded, 0, 0);
            cairo_rectangle (cr, left, top, (pos - cursor_width) / (width/w->width), height / (height/w->height));
            cairo_fill (cr);
            cairo_restore (cr);
        }
        else {
            cairo_set_source_surface (cr, w->surf_shaded, 0, 0);
            cairo_rectangle (cr, left, top, pos - cursor_width, height);
            cairo_fill (cr);
        }

        waveform_rect_t cursor_rect = {
            .x = pos - cursor_width,
            .y = top,
            .width = cursor_width,
            .height = height,
        };
        waveform_draw_cairo_rectangle (cr, &w->colors.pb, &cursor_rect);

        if (w->seekbar_moving && dur >= 0) {
            waveform_draw_seeking_cursor (w, cr, dur, rect);
        }
    }

    deadbeef->pl_item_unref (trk);
}

static cairo_surface_t *
waveform_draw_surface_update (cairo_surface_t *surface, double width, double height)
{
    if (!surface || cairo_image_surface_get_width (surface) != width || cairo_image_surface_get_height (surface) != height) {
        if (surface) {
            cairo_surface_destroy (surface);
            surface = NULL;
        }
        surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
    }
    return surface;
}

static void
waveform_draw (void *user_data, int shaded)
{
    waveform_t *w = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);

    const int width = a.width;
    const int height = a.height;

    w->width = width;
    w->height = height;

    cairo_surface_t *surface;
    if (!shaded) {
        w->surf = waveform_draw_surface_update (w->surf, width, height);
        surface = w->surf;
    }
    else {
        w->surf_shaded = waveform_draw_surface_update (w->surf_shaded, width, height);
        surface = w->surf_shaded;
    }

    cairo_surface_flush (surface);
    cairo_t *cr = cairo_create (surface);
    assert (cr != NULL);

    waveform_data_render_t *w_render_ctx = waveform_render_data_build (w->wave, width, CONFIG_MIX_TO_MONO);

    // Draw background
    waveform_rect_t bg_rect = {
        .x = 0,
        .y = 0,
        .width = width,
        .height = height,
    };
    waveform_draw_cairo_rectangle (cr, &w->colors.bg, &bg_rect);

    if (w_render_ctx) {

        const int channels = w_render_ctx->num_channels;
        const double channel_height = height/channels;
        const double waveform_height = 0.9 * channel_height;
        const double x = 0.0;
        double y = (channel_height - waveform_height)/2;

        waveform_colors_t *colors = &w->colors;
        if (CONFIG_SHADE_WAVEFORM && shaded) {
            colors = &w->colors_shaded;
        }

        for (int ch = 0; ch < channels; ch++, y += channel_height) {
            waveform_sample_t *samples = w_render_ctx->samples[ch];
            waveform_rect_t rect = {
                .x = x,
                .y = y,
                .width = width,
                .height = waveform_height,
            };
            switch (CONFIG_RENDER_METHOD) {
                case SPIKES:
                    waveform_draw_wave_default (samples, colors, cr, &rect);
                    break;
                case BARS:
                    waveform_draw_wave_bars (samples, colors, cr, &rect);
                    break;
                default:
                    waveform_draw_wave_default (samples, colors, cr, &rect);
                    break;
            }
        }
        if (!CONFIG_SHADE_WAVEFORM && shaded == 1) {
            waveform_draw_cairo_rectangle (cr, &w->colors_shaded.pb, &bg_rect);
        }

        waveform_data_render_free (w_render_ctx);
    }

    cairo_destroy (cr);
    return;
}

static void
waveform_scale (void *user_data, cairo_t *cr, waveform_rect_t *rect)
{
    waveform_t *w = user_data;

    if (rect->height != w->height || rect->width != w->width) {
        cairo_save (cr);
        cairo_translate (cr, rect->x, rect->y);
        cairo_scale (cr, rect->width/w->width, rect->height/w->height);
        cairo_set_source_surface (cr, w->surf, rect->x, rect->y);
        cairo_paint (cr);
        cairo_restore (cr);
    }
    else {
        cairo_set_source_surface (cr, w->surf, rect->x, rect->y);
        cairo_paint (cr);
    }
}

static gboolean
waveform_generate_wavedata (gpointer user_data, DB_playItem_t *it, const char *uri, wavedata_t *wavedata)
{
    waveform_t *w = user_data;
    const double width = CONFIG_NUM_SAMPLES;

    DB_fileinfo_t *fileinfo = NULL;

    deadbeef->pl_lock ();
    const char *dec_meta = deadbeef->pl_find_meta_raw (it, ":DECODER");
    char decoder_id[100];
    if (dec_meta) {
        strncpy (decoder_id, dec_meta, sizeof (decoder_id));
    }
    DB_decoder_t *dec = NULL;
    DB_decoder_t **decoders = deadbeef->plug_get_decoder_list ();
    for (int i = 0; decoders[i]; i++) {
        if (!strcmp (decoders[i]->plugin.id, decoder_id)) {
            dec = decoders[i];
            break;
        }
    }
    deadbeef->pl_unlock ();

    wavedata->data_len = 0;
    wavedata->channels = 0;

    if (dec && dec->open) {
        fileinfo = dec->open (0);
        if (fileinfo && dec->init (fileinfo, DB_PLAYITEM (it)) != 0) {
            deadbeef->pl_lock ();
            fprintf (stderr, "waveform: failed to decode file %s\n", deadbeef->pl_find_meta (it, ":URI"));
            deadbeef->pl_unlock ();
            goto out;
        }
        float *data;
        float *buffer;

        if (fileinfo) {
            const float duration = deadbeef->pl_get_item_duration (it);
            const int num_updates = MAX (1, floorf (duration)/30);
            const int update_after_nsamples = width/num_updates;
            if (duration <= 0) {
                goto out;
            }
            const int bytes_per_sample = fileinfo->fmt.bps / 8;
            const int samplesize = fileinfo->fmt.channels * bytes_per_sample;
            const int nsamples_per_channel = floorf (duration * (float)fileinfo->fmt.samplerate);
            const int samples_per_buf = ceilf ((float) nsamples_per_channel / (float) width);
            const int max_samples_per_buf = 1 + samples_per_buf;

            w->wave->channels = fileinfo->fmt.channels;
            w->wave->data_len = w->wave->channels * 3 * CONFIG_NUM_SAMPLES;
            deadbeef->mutex_lock (w->mutex);
            memset (w->wave->data, 0, sizeof (short) * w->max_buffer_len);
            deadbeef->mutex_unlock (w->mutex);

            data = malloc (sizeof (float) * max_samples_per_buf * samplesize);
            if (!data) {
                trace ("waveform: out of memory.\n");
                goto out;
            }
            memset (data, 0, sizeof (float) * max_samples_per_buf * samplesize);

            buffer = malloc (sizeof (float) * max_samples_per_buf * samplesize);
            if (!buffer) {
                trace ("waveform: out of memory.\n");
                goto out;
            }
            memset (buffer, 0, sizeof (float) * max_samples_per_buf * samplesize);


            ddb_waveformat_t out_fmt = {
                .bps = 32,
                .channels = fileinfo->fmt.channels,
                .samplerate = fileinfo->fmt.samplerate,
                .channelmask = fileinfo->fmt.channelmask,
                .is_float = 1,
                .is_bigendian = 0
            };

            int update_counter = 0;
            int eof = 0;
            int counter = 0;
            const long buffer_len = samples_per_buf * samplesize;
            while (!eof) {
                int sz = dec->read (fileinfo, (char *)buffer, buffer_len);
                if (sz != buffer_len) {
                    eof = 1;
                }
                else if (sz == 0) {
                    break;
                }

                deadbeef->pcm_convert (&fileinfo->fmt, (char *)buffer, &out_fmt, (char *)data, sz);

                int sample;
                float min, max, rms;

                for (int ch = 0; ch < fileinfo->fmt.channels; ch++) {
                    min = 1.0; max = -1.0; rms = 0.0;
                    for (sample = 0; sample < sz/samplesize; sample++) {
                        if (sample * fileinfo->fmt.channels > buffer_len) {
                            fprintf (stderr, "index error!\n");
                            break;
                        }
                        const float sample_val = data [sample * fileinfo->fmt.channels + ch];
                        max = MAX (max, sample_val);
                        min = MIN (min, sample_val);
                        rms += (sample_val * sample_val);
                    }
                    rms /= sample;
                    rms = sqrt (rms);
                    wavedata->data[counter] = (short)(max*1000);
                    wavedata->data[counter+1] = (short)(min*1000);
                    wavedata->data[counter+2] = (short)(rms*1000);
                    counter += 3;
                }
                if (update_counter == update_after_nsamples) {
                    DB_playItem_t *playing = deadbeef->streamer_get_playing_track ();
                    if (playing) {
                        if (playing == it) {
                            deadbeef->mutex_lock (w->mutex);
                            w->wave->channels = fileinfo->fmt.channels;
                            w->wave->data_len = w->wave->channels * VALUES_PER_SAMPLE * CONFIG_NUM_SAMPLES;
                            memset (w->wave->data, 0, sizeof (short) * w->max_buffer_len);
                            memcpy (w->wave->data, wavedata->data, counter * sizeof (short));
                            deadbeef->mutex_unlock (w->mutex);
                            g_idle_add (waveform_redraw_cb, w);
                        }
                        deadbeef->pl_item_unref (playing);
                    }
                    update_counter = 0;
                }
                update_counter++;
            }
            wavedata->fname = strdup (deadbeef->pl_find_meta_raw (it, ":URI"));
            wavedata->data_len = counter;
            wavedata->channels = fileinfo->fmt.channels;


            if (data) {
                free (data);
                data = NULL;
            }
            if (buffer) {
                free (buffer);
                buffer = NULL;
            }
        }
    }
out:
    if (dec && fileinfo) {
        dec->free (fileinfo);
        fileinfo = NULL;
    }

    return TRUE;
}

static void
waveform_db_cache (gpointer user_data, DB_playItem_t *it, wavedata_t *wavedata)
{
    waveform_t *w = user_data;
    char *key = waveform_format_uri (it, wavedata->fname);
    if (!key) {
        return;
    }
    deadbeef->mutex_lock (w->mutex);
    waveform_db_write (key, wavedata->data, wavedata->data_len * sizeof (short), wavedata->channels, 0);
    deadbeef->mutex_unlock (w->mutex);
    if (key) {
        free (key);
        key = NULL;
    }
}

static int
waveform_valid_track (DB_playItem_t *it, const char *uri)
{
    if (!deadbeef->is_local_file (uri)) {
        return 0;
    }
    if (deadbeef->pl_get_item_duration (it)/60 >= CONFIG_MAX_FILE_LENGTH && CONFIG_MAX_FILE_LENGTH != -1) {
        return 0;
    }

    deadbeef->pl_lock ();
    const char *file_meta = deadbeef->pl_find_meta_raw (it, ":FILETYPE");
    if (file_meta && strcmp (file_meta,"cdda") == 0) {
        deadbeef->pl_unlock ();
        return 0;
    }
    deadbeef->pl_unlock ();
    return 1;
}

static int
waveform_delete (DB_playItem_t *it, const char *uri)
{
    char *key = waveform_format_uri (it, uri);
    if (!key) {
        return 0;
    }
    int result = waveform_db_delete (key);
    if (key) {
        free (key);
        key = NULL;
    }
    return result;
}

static int
waveform_is_cached (DB_playItem_t *it, const char *uri)
{
    char *key = waveform_format_uri (it, uri);
    if (!key) {
        return 0;
    }
    int result = waveform_db_cached (key);
    if (key) {
        free (key);
        key = NULL;
    }
    return result;
}

static void
waveform_get_from_cache (gpointer user_data, DB_playItem_t *it, const char *uri)
{
    waveform_t *w = user_data;
    char *key = waveform_format_uri (it, uri);
    if (!key) {
        return;
    }
    deadbeef->mutex_lock (w->mutex);
    w->wave->data_len = waveform_db_read (key, w->wave->data, w->max_buffer_len, &w->wave->channels);
    deadbeef->mutex_unlock (w->mutex);
    if (key) {
        free (key);
        key = NULL;
    }
}

static void
waveform_get_wavedata (gpointer user_data)
{
    waveform_t *w = user_data;
    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    if (!it) {
        return;
    }

    char *uri = strdup (deadbeef->pl_find_meta_raw (it, ":URI"));
    if (!uri) {
        return;
    }
    if (!waveform_valid_track (it, uri)) {
        return;
    }

    deadbeef->background_job_increment ();
    if (CONFIG_CACHE_ENABLED && waveform_is_cached (it, uri)) {
        waveform_get_from_cache (w, it, uri);
        g_idle_add (waveform_redraw_cb, w);
    }
    else if (queue_add (uri)) {
        wavedata_t *wavedata = malloc (sizeof (wavedata_t));
        wavedata->data = malloc (sizeof (short) * w->max_buffer_len);
        memset (wavedata->data, 0, sizeof (short) * w->max_buffer_len);
        wavedata->fname = NULL;

        waveform_generate_wavedata (w, it, uri, wavedata);
        if (CONFIG_CACHE_ENABLED) {
            waveform_db_cache (w, it, wavedata);
        }
        queue_pop (uri);

        DB_playItem_t *playing = deadbeef->streamer_get_playing_track ();
        if (playing && it && it == playing) {
            deadbeef->mutex_lock (w->mutex);
            memcpy (w->wave->data, wavedata->data, wavedata->data_len * sizeof (short));
            w->wave->data_len = wavedata->data_len;
            w->wave->channels = wavedata->channels;
            deadbeef->mutex_unlock (w->mutex);
            g_idle_add (waveform_redraw_cb, w);

        }
        if (playing) {
            deadbeef->pl_item_unref (playing);
        }

        if (wavedata->data) {
            free (wavedata->data);
            wavedata->data = NULL;
        }
        if (wavedata->fname) {
            free (wavedata->fname);
            wavedata->fname = NULL;
        }
        if (wavedata) {
            free (wavedata);
            wavedata = NULL;
        }
    }

    free (uri);
    uri = NULL;

    deadbeef->pl_item_unref (it);
    deadbeef->background_job_decrement ();
}

static gboolean
waveform_set_refresh_interval (gpointer user_data, int interval)
{
    waveform_t *w = user_data;
    if (!w || interval <= 0) {
        return FALSE;
    }
    if (w->drawtimer) {
        g_source_remove (w->drawtimer);
        w->drawtimer = 0;
    }
    if (playback_status == PLAYING) {
        w->drawtimer = g_timeout_add (interval, waveform_draw_cb, w);
    }
    return TRUE;
}

static void
ruler_expose_event (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
    waveform_t *w = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation (w->ruler, &a);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    cairo_t *cr = gdk_cairo_create (gtk_widget_get_window (w->ruler));
#pragma GCC diagnostic pop
    if (!cr) {
        return;
    }

    waveform_rect_t rect = {
        .x = 0.0,
        .y = 0.0,
        .width = a.width,
        .height = a.height,
    };

    float duration = 0.f;

    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (trk) {
        duration = deadbeef->pl_get_item_duration (trk);
        deadbeef->pl_item_unref (trk);
    }

    waveform_render_ruler (cr, &w->colors, duration, &rect);

    cairo_destroy (cr);
}

static void
waveform_draw_generic_event (waveform_t *w, cairo_t *cr)
{
    if (playback_status != PLAYING) {
        if (w->drawtimer) {
            g_source_remove (w->drawtimer);
            w->drawtimer = 0;
        }
    }
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);

    waveform_rect_t rect = {
        .x = 0,
        .y = 0,
        .width = a.width,
        .height = a.height,
    };

    waveform_scale (w, cr, &rect);
    waveform_seekbar_draw (w, cr, &rect);
}

#if !GTK_CHECK_VERSION(3,0,0)
static gboolean
waveform_expose_event (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
    waveform_t *w = user_data;
    cairo_t *cr = gdk_cairo_create (gtk_widget_get_window (w->drawarea));
    waveform_draw_generic_event (w, cr);
    cairo_destroy (cr);

    return TRUE;
}
#else
static gboolean
waveform_draw_event (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    waveform_t *w = user_data;
    waveform_draw_generic_event (w, cr);

    return TRUE;
}
#endif

static gboolean
waveform_configure_event (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    waveform_t *w = user_data;
    if (!w) {
        return FALSE;
    }
    if (w->resizetimer) {
        g_source_remove (w->resizetimer);
        w->resizetimer = 0;
    }
    w->resizetimer = g_timeout_add (100, waveform_redraw_cb, w);
    return FALSE;
}

static gboolean
waveform_motion_notify_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    waveform_t *w = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);

    if (w->seekbar_moving || w->seekbar_move_x_clicked) {
        if (event->x < -DISTANCE_THRESHOLD
            || event->x > a.width + DISTANCE_THRESHOLD
            || event->y < -DISTANCE_THRESHOLD
            || event->y > a.height + DISTANCE_THRESHOLD) {
            w->seekbar_moving = 0;
        }
        else {
            w->seekbar_moving = 1;
            w->seekbar_move_x = event->x - a.x;
        }
        gtk_widget_queue_draw (w->drawarea);
    }
    return TRUE;
}

static gboolean
waveform_scroll_event (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    GdkEventScroll *ev = (GdkEventScroll *)event;
    if (!CONFIG_SCROLL_ENABLED) {
        return TRUE;
    }

    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (trk) {
        const int duration = (int)(deadbeef->pl_get_item_duration (trk) * 1000);
        const int time = (int)(deadbeef->streamer_get_playpos () * 1000);
        const int step = CLAMP (duration / 30, 1000, 3600000);

        switch (ev->direction) {
            case GDK_SCROLL_UP:
                deadbeef->sendmessage (DB_EV_SEEK, 0, MIN (duration, time + step), 0);
                break;
            case GDK_SCROLL_DOWN:
                deadbeef->sendmessage (DB_EV_SEEK, 0, MAX (0, time - step), 0);
                break;
            default:
                break;
        }
        deadbeef->pl_item_unref (trk);
    }
    return TRUE;
}

static gboolean
waveform_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    waveform_t *w = user_data;
    if (event->button == 3 || event->button == 2) {
        return TRUE;
    }
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);

    w->seekbar_moving = 1;
    w->seekbar_move_x = event->x - a.x;
    w->seekbar_move_x_clicked = event->x - a.x;
    return TRUE;
}

static gboolean
waveform_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    waveform_t *w = user_data;
    if (event->button == 3) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        gtk_menu_popup (GTK_MENU (w->popup), NULL, NULL, NULL, w->drawarea, 0, gtk_get_current_event_time ());
#pragma GCC diagnostic pop
        return TRUE;
    }
    if (event->button == 2) {
        deadbeef->sendmessage (DB_EV_TOGGLE_PAUSE, 0, 0, 0);
        return TRUE;
    }
    w->seekbar_move_x_clicked = 0;
    if (w->seekbar_moving) {
        DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
        if (trk) {
            GtkAllocation a;
            gtk_widget_get_allocation (w->drawarea, &a);
            const float time = MAX (0, (event->x - a.x) * deadbeef->pl_get_item_duration (trk) / (a.width) * 1000.f);
            deadbeef->sendmessage (DB_EV_SEEK, 0, time, 0);
            deadbeef->pl_item_unref (trk);
        }
        gtk_widget_queue_draw (widget);
    }
    w->seekbar_moving = 0;
    return TRUE;
}

static int
waveform_message (ddb_gtkui_widget_t *widget, uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
    waveform_t *w = (waveform_t *)widget;
    intptr_t tid;

    switch (id) {
    case DB_EV_SONGSTARTED:
        playback_status = PLAYING;
        waveform_set_refresh_interval (w, CONFIG_REFRESH_INTERVAL);
        g_idle_add (waveform_redraw_cb, w);
        g_idle_add (ruler_redraw_cb, w);
        tid = deadbeef->thread_start_low_priority (waveform_get_wavedata, w);
        if (tid) {
            deadbeef->thread_detach (tid);
        }
        break;
    case DB_EV_STOP:
        playback_status = STOPPED;
        deadbeef->mutex_lock (w->mutex);
        memset (w->wave->data, 0, sizeof (short) * w->max_buffer_len);
        w->wave->data_len = 0;
        w->wave->channels = 0;
        deadbeef->mutex_unlock (w->mutex);
        g_idle_add (waveform_redraw_cb, w);
        g_idle_add (ruler_redraw_cb, w);
        break;
    case DB_EV_CONFIGCHANGED:
        on_config_changed (w);
        break;
    case DB_EV_PAUSED:
        if (p1) {
            playback_status = PAUSED;
        }
        else {
            playback_status = PLAYING;
            waveform_set_refresh_interval (w, CONFIG_REFRESH_INTERVAL);
        }
        break;
    case DB_EV_SEEKED:
        g_idle_add (waveform_redraw_cb, w);
        g_idle_add (ruler_redraw_cb, w);
    }
    return 0;
}

static void
waveform_destroy (ddb_gtkui_widget_t *widget)
{
    waveform_t *w = (waveform_t *)widget;
    deadbeef->mutex_lock (w->mutex);
    waveform_db_close ();
    if (w->drawtimer) {
        g_source_remove (w->drawtimer);
        w->drawtimer = 0;
    }
    if (w->resizetimer) {
        g_source_remove (w->resizetimer);
        w->resizetimer = 0;
    }
    if (w->surf) {
        cairo_surface_destroy (w->surf);
        w->surf = NULL;
    }
    if (w->surf_shaded) {
        cairo_surface_destroy (w->surf_shaded);
        w->surf_shaded = NULL;
    }
    if (w->wave->data) {
        free (w->wave->data);
        w->wave->data = NULL;
    }
    if (w->wave->fname) {
        free (w->wave->fname);
        w->wave->fname = NULL;
    }
    if (w->wave) {
        free (w->wave);
        w->wave = NULL;
    }
    deadbeef->mutex_unlock (w->mutex);
    if (w->mutex) {
        deadbeef->mutex_free (w->mutex);
        w->mutex = 0;
    }

    if (waveform_instancecount > 0) {
        waveform_instancecount--;
    }
}

static void
waveform_init (ddb_gtkui_widget_t *w)
{
    waveform_t *wf = (waveform_t *)w;
    GtkAllocation a;
    gtk_widget_get_allocation (wf->drawarea, &a);
    load_config ();
    waveform_colors_update (wf);

    wf->max_buffer_len = MAX_SAMPLES * VALUES_PER_SAMPLE * MAX_CHANNELS * sizeof (short);
    deadbeef->mutex_lock (wf->mutex);
    wf->wave = malloc (sizeof (wavedata_t));
    wf->wave->data = malloc (sizeof (short) * wf->max_buffer_len);
    memset (wf->wave->data, 0, sizeof (short) * wf->max_buffer_len);
    wf->wave->fname = NULL;
    wf->wave->data_len = 0;
    wf->wave->channels = 0;
    wf->surf = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
                                           a.width,
                                           a.height);
    wf->surf_shaded = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
                                                  a.width,
                                                  a.height);
    deadbeef->mutex_unlock (wf->mutex);
    wf->seekbar_moving = 0;
    wf->height = a.height;
    wf->width = a.width;
    wf->pos_last = 0;

    make_cache_dir (cache_path, sizeof (cache_path)/sizeof (char));

    deadbeef->mutex_lock (wf->mutex);
    waveform_db_open (cache_path);
    waveform_db_init (NULL);
    deadbeef->mutex_unlock (wf->mutex);

    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    if (it) {
        playback_status = PLAYING;
        intptr_t tid = deadbeef->thread_start_low_priority (waveform_get_wavedata, w);
        if (tid) {
            deadbeef->thread_detach (tid);
        }
        deadbeef->pl_item_unref (it);
    }
    wf->resizetimer = 0;

    on_config_changed (w);
}

static ddb_gtkui_widget_t *
waveform_create (void)
{
    waveform_t *w = malloc (sizeof (waveform_t));
    memset (w, 0, sizeof (waveform_t));

    w->base.widget = gtk_event_box_new ();
    w->base.init = waveform_init;
    w->base.destroy = waveform_destroy;
    w->base.message = waveform_message;
    w->drawarea = gtk_drawing_area_new ();
    w->ruler = gtk_drawing_area_new ();
#if !GTK_CHECK_VERSION(3,0,0)
    GtkWidget *vbox = gtk_vbox_new (FALSE, 0);
#else
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
#endif
    w->frame = gtk_frame_new (NULL);
    w->popup = gtk_menu_new ();
    gtk_menu_attach_to_widget (GTK_MENU (w->popup), w->base.widget, NULL);
    w->popup_item = gtk_menu_item_new_with_mnemonic ("Configure");
    w->mutex = deadbeef->mutex_create ();
    gtk_widget_set_size_request (w->base.widget, 300, 96);
    gtk_widget_set_size_request (w->ruler, -1, 20);
    gtk_widget_set_size_request (w->drawarea, -1, -1);
    gtk_widget_add_events (w->base.widget, GDK_SCROLL_MASK);
    gtk_container_add (GTK_CONTAINER (w->base.widget), w->frame);
    gtk_container_add (GTK_CONTAINER (w->frame), vbox);
    gtk_container_add (GTK_CONTAINER (vbox), w->ruler);
    gtk_container_add (GTK_CONTAINER (vbox), w->drawarea);
    gtk_container_add (GTK_CONTAINER (w->popup), w->popup_item);
    gtk_box_set_child_packing (GTK_BOX (vbox), w->drawarea, TRUE, TRUE, 0, 0);
    gtk_box_set_child_packing (GTK_BOX (vbox), w->ruler, FALSE, TRUE, 0, 0);
    gtk_widget_show (w->drawarea);
    gtk_widget_show (vbox);
    gtk_widget_show (w->frame);
    gtk_widget_show (w->popup);
    gtk_widget_show (w->ruler);
    gtk_widget_show (w->popup_item);

#if !GTK_CHECK_VERSION(3,0,0)
    g_signal_connect_after ((gpointer) w->drawarea, "expose_event", G_CALLBACK (waveform_expose_event), w);
#else
    g_signal_connect_after ((gpointer) w->drawarea, "draw", G_CALLBACK (waveform_draw_event), w);
#endif
#if !GTK_CHECK_VERSION(3,0,0)
    g_signal_connect_after ((gpointer) w->ruler, "expose_event", G_CALLBACK (ruler_expose_event), w);
#else
    g_signal_connect_after ((gpointer) w->ruler, "draw", G_CALLBACK (ruler_expose_event), w);
#endif
    g_signal_connect_after ((gpointer) w->drawarea, "configure_event", G_CALLBACK (waveform_configure_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "button_press_event", G_CALLBACK (waveform_button_press_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "button_release_event", G_CALLBACK (waveform_button_release_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "scroll-event", G_CALLBACK (waveform_scroll_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "motion_notify_event", G_CALLBACK (waveform_motion_notify_event), w);
    g_signal_connect_after ((gpointer) w->popup_item, "activate", G_CALLBACK (on_button_config), w);
    gtkui_plugin->w_override_signals (w->base.widget, w);

    waveform_instancecount++;

    return (ddb_gtkui_widget_t *)w;
}

static int
waveform_connect (void)
{
    gtkui_plugin = (ddb_gtkui_t *) deadbeef->plug_get_for_id (DDB_GTKUI_PLUGIN_ID);
    if (gtkui_plugin) {
        trace ("using '%s' plugin %d.%d\n", DDB_GTKUI_PLUGIN_ID, gtkui_plugin->gui.plugin.version_major, gtkui_plugin->gui.plugin.version_minor );
        if (gtkui_plugin->gui.plugin.version_major == 2) {
            gtkui_plugin->w_reg_widget ("Waveform Seekbar", DDB_WF_SINGLE_INSTANCE, waveform_create, "waveform_seekbar", NULL);
            return 0;
        }
    }
    return -1;
}

static int
waveform_start (void)
{
    load_config ();
    return 0;
}

static int
waveform_stop (void)
{
    save_config ();
    return 0;
}

static int
waveform_disconnect (void)
{
    if (gtkui_plugin) {
        gtkui_plugin->w_unreg_widget ("waveform_seekbar");
    }
    gtkui_plugin = NULL;
    return 0;
}

static int
waveform_action_lookup (DB_plugin_action_t *action, int ctx)
{
    DB_playItem_t *it = NULL;
    deadbeef->pl_lock ();
    if (ctx == DDB_ACTION_CTX_SELECTION) {
        ddb_playlist_t *plt = deadbeef->plt_get_curr ();
        if (plt) {
            it = deadbeef->plt_get_first (plt, PL_MAIN);
            while (it) {
                if (deadbeef->pl_is_selected (it)) {
                    const char *uri = deadbeef->pl_find_meta_raw (it, ":URI");
                    if (waveform_is_cached (it, uri)) {
                        waveform_delete (it, uri);
                    }
                }
                DB_playItem_t *next = deadbeef->pl_get_next (it, PL_MAIN);
                deadbeef->pl_item_unref (it);
                it = next;
            }
            deadbeef->plt_unref (plt);
        }
    }
    if (it) {
        deadbeef->pl_item_unref (it);
    }
    deadbeef->pl_unlock ();
    return 0;
}

static DB_plugin_action_t lookup_action = {
    .title = "Remove Waveform From Cache",
    .name = "waveform_lookup",
    .flags = DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU,
    .callback2 = waveform_action_lookup,
    .next = NULL
};

static DB_plugin_action_t *
waveform_get_actions (DB_playItem_t *it)
{
    if (!waveform_instancecount) {
        return NULL;
    }
    deadbeef->pl_lock ();
    lookup_action.flags |= DB_ACTION_DISABLED;
    DB_playItem_t *current = deadbeef->pl_get_first (PL_MAIN);
    while (current) {
        if (deadbeef->pl_is_selected (current) && waveform_is_cached (current, deadbeef->pl_find_meta_raw (current, ":URI"))) {
            lookup_action.flags &= ~DB_ACTION_DISABLED;
            deadbeef->pl_item_unref (current);
            break;
        }
        DB_playItem_t *next = deadbeef->pl_get_next (current, PL_MAIN);
        deadbeef->pl_item_unref (current);
        current = next;
    }
    deadbeef->pl_unlock ();
    return &lookup_action;
}


static const char settings_dlg[] =
    "property \"Refresh interval (ms): \"           spinbtn[10,1000,1] "        CONFSTR_WF_REFRESH_INTERVAL    " 33 ;\n"
    "property \"Border width: \"                    spinbtn[0,1,1] "            CONFSTR_WF_BORDER_WIDTH         " 1 ;\n"
    "property \"Cursor width: \"                    spinbtn[0,3,1] "            CONFSTR_WF_CURSOR_WIDTH         " 3 ;\n"
    "property \"Font size: \"                       spinbtn[8,20,1] "           CONFSTR_WF_FONT_SIZE           " 18 ;\n"
    "property \"Ignore files longer than x minutes "
                "(-1 scans every file): \"          spinbtn[-1,9999,1] "        CONFSTR_WF_MAX_FILE_LENGTH    " 180 ;\n"
    "property \"Use cache \"                        checkbox "                  CONFSTR_WF_CACHE_ENABLED        " 1 ;\n"
    "property \"Scroll wheel to seek \"             checkbox "                  CONFSTR_WF_SCROLL_ENABLED       " 1 ;\n"
    "property \"Number of samples (per channel): \" spinbtn[2048,4092,2048] "   CONFSTR_WF_NUM_SAMPLES       " 2048 ;\n"
;

static DB_misc_t plugin = {
    //DB_PLUGIN_SET_API_VERSION
    .plugin.type            = DB_PLUGIN_MISC,
    .plugin.api_vmajor      = 1,
    .plugin.api_vminor      = 5,
    .plugin.version_major   = 0,
    .plugin.version_minor   = 5,
#if GTK_CHECK_VERSION(3,0,0)
    .plugin.id              = "waveform_seekbar-gtk3",
#else
    .plugin.id              = "waveform_seekbar",
#endif
    .plugin.name            = "Waveform Seekbar",
    .plugin.descr           = "Waveform Seekbar",
    .plugin.copyright       =
        "Copyright (C) 2014 Christian Boxdörfer <christian.boxdoerfer@posteo.de>\n"
        "\n"
        "Based on sndfile-tools waveform by Erik de Castro Lopo.\n"
        "\n"
        "This program is free software; you can redistribute it and/or\n"
        "modify it under the terms of the GNU General Public License\n"
        "as published by the Free Software Foundation; either version 2\n"
        "of the License, or (at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License\n"
        "along with this program; if not, write to the Free Software\n"
        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n"
    ,
    .plugin.website         = "https://github.com/cboxdoerfer/ddb_waveform_seekbar",
    .plugin.start           = waveform_start,
    .plugin.stop            = waveform_stop,
    .plugin.connect         = waveform_connect,
    .plugin.disconnect      = waveform_disconnect,
    .plugin.configdialog    = settings_dlg,
    .plugin.get_actions     = waveform_get_actions,
};

#if !GTK_CHECK_VERSION(3,0,0)
DB_plugin_t *
ddb_misc_waveform_GTK2_load (DB_functions_t *ddb)
{
    deadbeef = ddb;
    return &plugin.plugin;
}
#else
DB_plugin_t *
ddb_misc_waveform_GTK3_load (DB_functions_t *ddb)
{
    deadbeef = ddb;
    return &plugin.plugin;
}
#endif
