/*
    Waveform seekbar plugin for the DeaDBeeF audio player

    Copyright (C) 2013 Christian Boxdörfer <christian.boxdoerfer@posteo.de>

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
#include <string.h>
#include <assert.h>
#include <math.h>
#include <fcntl.h>
#include <gtk/gtk.h>

#include <deadbeef/deadbeef.h>
#include <deadbeef/gtkui_api.h>

#define C_COLOUR(X) (X)->r, (X)->g, (X)->b, (X)->a

#define BORDER_LINE_WIDTH   (2.0)

// min, max, rms
#define VALUES_PER_FRAME (3)
#define MAX_VALUES_PER_CHANNEL (4096)

#define     CONFSTR_WF_LOG_ENABLED       "waveform.log_enabled"
#define     CONFSTR_WF_MIX_TO_MONO       "waveform.mix_to_mono"
#define     CONFSTR_WF_BG_COLOR_R        "waveform.bg_color_r"
#define     CONFSTR_WF_BG_COLOR_G        "waveform.bg_color_g"
#define     CONFSTR_WF_BG_COLOR_B        "waveform.bg_color_b"
#define     CONFSTR_WF_FG_COLOR_R        "waveform.fg_color_r"
#define     CONFSTR_WF_FG_COLOR_G        "waveform.fg_color_g"
#define     CONFSTR_WF_FG_COLOR_B        "waveform.fg_color_b"
#define     CONFSTR_WF_PB_COLOR_R        "waveform.pb_color_r"
#define     CONFSTR_WF_PB_COLOR_G        "waveform.pb_color_g"
#define     CONFSTR_WF_PB_COLOR_B        "waveform.pb_color_b"

/* Global variables */
static DB_misc_t            plugin;
static DB_functions_t *     deadbeef = NULL;
static ddb_gtkui_t *        gtkui_plugin = NULL;

typedef struct
{
    ddb_gtkui_widget_t base;
    GtkWidget *popup;
    GtkWidget *popup_item;
    GtkWidget *drawarea;
    guint drawtimer;
    guint resizetimer;
    float *buffer;
    size_t max_buffer_len;
    size_t buffer_len;
    int nsamples;
    int channel;
    int seekbar_moving;
    float seekbar_moved;
    float seekbar_move_x;
    float height;
    float width;
    intptr_t mutex;
    cairo_surface_t *surf;
} w_waveform_t;

typedef struct DRECT
{   double x1, y1;
    double x2, y2;
} DRECT;

typedef struct COLOUR
{   double r;
    double g;
    double b;
    double a;
} COLOUR;

typedef struct
{   const char *sndfilepath, *pngfilepath, *filename;
    int width, height, channel_separation;
    int channel;
    int what;
    gboolean autogain;
    gboolean border, geometry_no_border, logscale, rectified;
    COLOUR c_fg, c_rms, c_bg, c_ann, c_bbg, c_cl;
    int tc_num, tc_den;
    double tc_off;
    gboolean parse_bwf;
    double border_width;
} RENDER;

enum WHAT { PEAK = 1, RMS = 2 };

static gboolean CONFIG_LOG_ENABLED = FALSE;
static gboolean CONFIG_MIX_TO_MONO = FALSE;
static GdkColor CONFIG_BG_COLOR;
static GdkColor CONFIG_FG_COLOR;
static GdkColor CONFIG_PB_COLOR;

static void
save_config (void)
{
    deadbeef->conf_set_int (CONFSTR_WF_LOG_ENABLED,   CONFIG_LOG_ENABLED);
    deadbeef->conf_set_int (CONFSTR_WF_MIX_TO_MONO,   CONFIG_MIX_TO_MONO);
    deadbeef->conf_set_int (CONFSTR_WF_BG_COLOR_R,    CONFIG_BG_COLOR.red);
    deadbeef->conf_set_int (CONFSTR_WF_BG_COLOR_G,    CONFIG_BG_COLOR.green);
    deadbeef->conf_set_int (CONFSTR_WF_BG_COLOR_B,    CONFIG_BG_COLOR.blue);
    deadbeef->conf_set_int (CONFSTR_WF_FG_COLOR_R,    CONFIG_FG_COLOR.red);
    deadbeef->conf_set_int (CONFSTR_WF_FG_COLOR_G,    CONFIG_FG_COLOR.green);
    deadbeef->conf_set_int (CONFSTR_WF_FG_COLOR_B,    CONFIG_FG_COLOR.blue);
    deadbeef->conf_set_int (CONFSTR_WF_PB_COLOR_R,    CONFIG_PB_COLOR.red);
    deadbeef->conf_set_int (CONFSTR_WF_PB_COLOR_G,    CONFIG_PB_COLOR.green);
    deadbeef->conf_set_int (CONFSTR_WF_PB_COLOR_B,    CONFIG_PB_COLOR.blue);
}

static void
load_config (void)
{
    deadbeef->conf_lock ();
    CONFIG_LOG_ENABLED = deadbeef->conf_get_int (CONFSTR_WF_LOG_ENABLED,    FALSE);
    CONFIG_MIX_TO_MONO = deadbeef->conf_get_int (CONFSTR_WF_MIX_TO_MONO,    FALSE);
    CONFIG_BG_COLOR.red = deadbeef->conf_get_int (CONFSTR_WF_BG_COLOR_R,      55000);
    CONFIG_BG_COLOR.green = deadbeef->conf_get_int (CONFSTR_WF_BG_COLOR_G,      55000);
    CONFIG_BG_COLOR.blue = deadbeef->conf_get_int (CONFSTR_WF_BG_COLOR_B,      55000);
    CONFIG_FG_COLOR.red = deadbeef->conf_get_int (CONFSTR_WF_FG_COLOR_R,          0);
    CONFIG_FG_COLOR.green = deadbeef->conf_get_int (CONFSTR_WF_FG_COLOR_G,          0);
    CONFIG_FG_COLOR.blue = deadbeef->conf_get_int (CONFSTR_WF_FG_COLOR_B,          0);
    CONFIG_PB_COLOR.red = deadbeef->conf_get_int (CONFSTR_WF_PB_COLOR_R,          0);
    CONFIG_PB_COLOR.green = deadbeef->conf_get_int (CONFSTR_WF_PB_COLOR_G,      65535);
    CONFIG_PB_COLOR.blue = deadbeef->conf_get_int (CONFSTR_WF_PB_COLOR_B,          0);
    deadbeef->conf_unlock ();
}

static int
on_config_changed (uintptr_t ctx)
{
    load_config ();
    return 0;
}

void
w_waveform_destroy (ddb_gtkui_widget_t *widget)
{
    w_waveform_t *w = (w_waveform_t *)widget;
    if (w->drawtimer) {
        g_source_remove (w->drawtimer);
        w->drawtimer = 0;
    }
    if (w->surf) {
        cairo_surface_destroy (w->surf);
        w->surf = NULL;
    }
    if (w->buffer) {
        free (w->buffer);
        w->buffer = NULL;
    }
    if (w->mutex) {
        deadbeef->mutex_free (w->mutex);
        w->mutex = 0;
    }
}

void
waveform_render (void *user_data);

gboolean
w_waveform_draw_cb (void *data)
{
    w_waveform_t *w = data;
    gtk_widget_queue_draw (w->drawarea);
    return TRUE;
}

static gboolean
waveform_redraw_cb (void *user_data)
{
    w_waveform_t *w = user_data;
    waveform_render (w);
    gtk_widget_queue_draw (w->drawarea);
    return FALSE;
}

static gboolean
waveform_redraw_thread (void *user_data)
{
    w_waveform_t *w = user_data;
    w->resizetimer = 0;
    intptr_t tid = deadbeef->thread_start (waveform_render, w);
    deadbeef->thread_detach (tid);
    gtk_widget_queue_draw (w->drawarea);
    return FALSE;
}

static inline void
_draw_vline (uint8_t *data, int stride, int x0, int y0, int y1)
{
    if (y0 > y1) {
        int tmp = y0;
        y0 = y1;
        y1 = tmp;
        y1--;
    }
    else if (y0 < y1) {
        y0++;
    }
    while (y0 <= y1) {
        uint32_t *ptr = (uint32_t*)&data[y0*stride+x0*4];
        *ptr = 0xffffffff;
        y0++;
    }
}

static inline void
set_colour (COLOUR * c, int h)
{   c->a = ((h >> 24) & 0xff) / 255.0;
    c->r = ((h >> 16) & 0xff) / 255.0;
    c->g = ((h >> 8) & 0xff) / 255.0;
    c->b = ((h) & 0xff) / 255.0;
} /* set_colour */

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

static inline float coefficient_to_dB (float coeff)
{
    return 20.0f * log10 (coeff);
}
/* end of ardour copy */

#ifdef EQUAL_VSPACE_LOG_TICKS
static inline float dB_to_coefficient (float dB)
{
    return dB > -318.8f ? pow (10.0f, dB * 0.05f) : 0.0f;
}

static inline float
inv_log_meter (float power)
{
    return (power < 0.00000000026 ? -192.0 : (pow (power, 0.125) * 192.0) -192.0);
}
#endif

static inline void
draw_cairo_line (cairo_t* cr, DRECT *pts, const COLOUR *c)
{
    cairo_set_source_rgba (cr, C_COLOUR (c));
    cairo_move_to (cr, pts->x1, pts->y1);
    cairo_line_to (cr, pts->x2, pts->y2);
    cairo_stroke (cr);
}

gboolean
waveform_seekbar_render (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    w_waveform_t *w = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation (widget, &a);

    double width = a.width;
    float pos = 0.0;

    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (!trk || deadbeef->pl_get_item_duration (trk) < 0) {
        if (trk) {
            deadbeef->pl_item_unref (trk);
            return FALSE;
        }
    }

    if (trk) {
        if (deadbeef->pl_get_item_duration (trk) > 0) {
            pos = deadbeef->streamer_get_playpos () / deadbeef->pl_get_item_duration (trk);
            pos *= width;
        }

        deadbeef->pl_item_unref (trk);
    }
    else {
        return FALSE;
    }

    if (w->seekbar_moving) {
        if (w->seekbar_move_x < 0) {
            pos = 0;
        }
        else if (w->seekbar_move_x > a.width) {
            pos = a.width;
        }
        else {
            pos = w->seekbar_move_x;
        }
    }
    cairo_save (cr);
    cairo_translate(cr, 0, 0);
    cairo_scale (cr, a.width/w->width, a.height/w->height);
    cairo_set_source_surface (cr, w->surf, 0, 0);
    cairo_paint (cr);
    cairo_restore (cr);

    cairo_set_source_rgba (cr,(float)CONFIG_PB_COLOR.red/65535,(float)CONFIG_PB_COLOR.green/65535,(float)CONFIG_PB_COLOR.blue/65535,0.3);
    cairo_rectangle (cr, 0, 0, pos, a.height);
    cairo_fill (cr);

    cairo_set_source_rgb (cr,(float)CONFIG_PB_COLOR.red/65535,(float)CONFIG_PB_COLOR.green/65535,(float)CONFIG_PB_COLOR.blue/65535);
    cairo_rectangle (cr, pos-3, 0, 3, a.height);
    cairo_fill (cr);
    return TRUE;
}

void
waveform_render (void *user_data)
{
    w_waveform_t *w = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);

    double width = a.width;
    double height = a.height * 0.9;
    double left = 0;
    double top = (a.height - height)/2;
    w->width = a.width;
    w->height = a.height;

    float pmin = 0;
    float pmax = 0;
    float prms = 0;

    int channel = 0;
    float gain = 1.0;
    int x = 0;

    RENDER render =
    {   NULL, NULL, NULL,
        /*width*/ a.width, /*height*/ a.height - 6,
        /*channel_separation*/ 12.0,
        /*channel*/ 0,
        /*what*/ PEAK | RMS,
        /*autogain*/ FALSE,
        /*border*/ FALSE,
        /*geometry_no_border*/ FALSE,
        /*logscale*/ CONFIG_LOG_ENABLED, /*rectified*/ FALSE,
        /*foreground*/  { (float)CONFIG_FG_COLOR.red/65535,(float)CONFIG_FG_COLOR.green/65535,(float)CONFIG_FG_COLOR.blue/65535, 0.6 },
        /*wave-rms*/    { (float)CONFIG_FG_COLOR.red/65535,(float)CONFIG_FG_COLOR.green/65535,(float)CONFIG_FG_COLOR.blue/65535, 1.0 },
        /*background*/  { (float)CONFIG_BG_COLOR.red/65535,(float)CONFIG_BG_COLOR.green/65535,(float)CONFIG_BG_COLOR.blue/65535, 0.5 },
        /*annotation*/  { 1.0, 1.0, 1.0, 1.0 },
        /*border-bg*/   { 0.0, 0.0, 0.0, 0.7 },
        /*center-line*/ { (float)CONFIG_FG_COLOR.red/65535,(float)CONFIG_FG_COLOR.green/65535,(float)CONFIG_FG_COLOR.blue/65535, 0.3 },
        /*timecode num*/ 0, /*den*/ 0, /*offset*/ 0.0,
        /*parse BWF*/ TRUE,
        /*border-width*/ 2.0f,
    };

    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    DB_decoder_t *dec = NULL;
    DB_fileinfo_t *fileinfo = NULL;

    if (it) {
        deadbeef->pl_lock ();
        const char *dec_meta = deadbeef->pl_find_meta_raw (it, ":DECODER");
        char decoder_id[100];
        if (dec_meta) {
            strncpy (decoder_id, dec_meta, sizeof (decoder_id));
        }
        DB_decoder_t **decoders = deadbeef->plug_get_decoder_list ();
        for (int i = 0; decoders[i]; i++) {
            if (!strcmp (decoders[i]->plugin.id, decoder_id)) {
                dec = decoders[i];
                break;
            }
        }
        deadbeef->pl_unlock ();
        if (dec) {
            fileinfo = dec->open (0);
            if (fileinfo && dec->init (fileinfo, DB_PLAYITEM (it)) != 0) {
                deadbeef->pl_lock ();
                fprintf (stderr, "waveform: failed to decode file %s\n", deadbeef->pl_find_meta (it, ":URI"));
                deadbeef->pl_unlock ();
            }
        }
        deadbeef->pl_item_unref (it);
    }
    if (fileinfo) {
        if (!w->surf || cairo_image_surface_get_width (w->surf) != a.width || cairo_image_surface_get_height (w->surf) != a.height) {
            if (w->surf) {
                cairo_surface_destroy (w->surf);
                w->surf = NULL;
            }
            w->surf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, a.width, a.height);
        }
        cairo_surface_flush (w->surf);
        cairo_t *temp_cr = cairo_create (w->surf);

        cairo_set_line_width (temp_cr, render.border_width);
        cairo_rectangle (temp_cr, left, 0, a.width, a.height);
        cairo_stroke_preserve (temp_cr);
        cairo_set_source_rgba (temp_cr,(float)CONFIG_BG_COLOR.red/65535,(float)CONFIG_BG_COLOR.green/65535,(float)CONFIG_BG_COLOR.blue/65535,1);
        cairo_fill (temp_cr);
        cairo_set_line_width (temp_cr, 1.0);

        if (channel < 0 || channel >= fileinfo->fmt.channels) {
            printf ("invalid channel\n");
            return;
        }

        float frames_per_x;
        int frames_size = VALUES_PER_FRAME * fileinfo->fmt.channels;

        if (fileinfo->fmt.channels != 0) {
            frames_per_x = w->buffer_len / (float)(width * frames_size);
        }
        else {
            frames_per_x = w->buffer_len / (float)(width * VALUES_PER_FRAME);
        }
        int max_frames_per_x = 1 + ceilf (frames_per_x);
        int frames_per_buf = floorf (frames_per_x * (float)(frames_size));

        int ch;
        int offset;
        int f_offset;
        int temp;
        int frame_offset;
        float min, max, rms;

        int channels = fileinfo->fmt.channels;

        if (channels != 0) {
            height /= channels;
            top /= channels;
        }

        if (CONFIG_MIX_TO_MONO) {
            frames_size = VALUES_PER_FRAME;
            channels = 1;
            frames_per_x = w->buffer_len / ((float)width * frames_size);
            max_frames_per_x = 1 + ceilf (frames_per_x);
            frames_per_buf = floorf (frames_per_x * (float)(frames_size));
            height = a.height * 0.9;
            top = (a.height - height)/2;
        }

        deadbeef->mutex_lock (w->mutex);
        for (ch = 0; ch < channels; ch++, top += (a.height / channels)) {
            x = 0;
            f_offset = 0;
            offset = ch * VALUES_PER_FRAME;
            frame_offset = frames_size;
            for (int i = 0; i < width; i++) {
                double yoff;
                min = 1.0; max = -1.0; rms = 0.0;

                temp = offset;
                if (offset + frames_per_buf > w->buffer_len) {
                    break;
                }
                for (int j = offset; j < offset + frames_per_buf; j = j + frame_offset) {
                    max = MAX (max, w->buffer[j]);
                    min = MIN (min, w->buffer[j+1]);
                    rms = (rms + w->buffer[j+2])/2;
                    temp += frame_offset;
                }
                offset = temp;

                if (gain != 1.0) {
                    min *= gain;
                    max *= gain;
                    rms *= gain;
                }

                if (render.logscale) {
                   if (max > 0)
                        max = alt_log_meter (coefficient_to_dB (max));
                    else
                        max = -alt_log_meter (coefficient_to_dB (-max));

                    if (min > 0)
                        min = alt_log_meter (coefficient_to_dB (min));
                    else
                        min = -alt_log_meter (coefficient_to_dB (-min));

                    rms = alt_log_meter (coefficient_to_dB (rms));
                }

                if (render.rectified) {
                    yoff = height;
                    min = height * MAX (fabsf (min), fabsf (max));
                    max = 0;
                    rms = height * rms;
                }
                else {
                    yoff = 0.5 * height;
                    min = min * yoff;
                    max = max * yoff;
                    rms = rms * yoff;
                }

                /* Draw background - box */
                if (TRUE) {
                    if (render.rectified) {
                        DRECT pts2 = { left + x, top + yoff - MIN (min, pmin), left + x, top + yoff };
                        draw_cairo_line (temp_cr, &pts2, &render.c_fg);
                    }
                    else {
                        DRECT pts2 = { left + x, top + yoff - MAX (pmin, min), left + x, top + yoff - MIN (pmax, max) };
                        draw_cairo_line (temp_cr, &pts2, &render.c_fg);
                    }
                }

                if (TRUE) {
                    if (render.rectified) {
                        DRECT pts2 = { left + x, top + yoff - MIN (prms, rms), left + x, top + yoff };
                        draw_cairo_line (temp_cr, &pts2, &render.c_rms);
                    }
                    else {
                        DRECT pts2 = { left + x, top + yoff - MIN (prms, rms), left + x, top + yoff + MIN (prms, rms) };
                        draw_cairo_line (temp_cr, &pts2, &render.c_rms);
                    }
                }

                /* Draw Foreground - line */
                if (TRUE) {
                    DRECT pts0 = { left + x - 0.5, top + yoff - prms, left + x + 0.5, top + yoff - rms };
                    draw_cairo_line (temp_cr, &pts0, &render.c_rms);

                    if (!render.rectified) {
                        DRECT pts1 = { left + x - 0.5, top + yoff + prms, left + x + 0.5, top + yoff + rms };
                        draw_cairo_line (temp_cr, &pts1, &render.c_rms);
                    }
                }

                if (TRUE) {
                    DRECT pts0 = { left + x - 0.5, top + yoff - pmin, left + x + 0.5, top + yoff - min };
                    draw_cairo_line (temp_cr, &pts0, &render.c_fg);
                    if (!render.rectified) {
                        DRECT pts1 = { left + x - 0.5, top + yoff - pmax, left + x + 0.5, top + yoff - max };
                        draw_cairo_line (temp_cr, &pts1, &render.c_fg);
                    }
                }
                pmin = min;
                pmax = max;
                prms = rms;

                x++;
                if (x > width) {
                    break;
                }
                f_offset += frames_per_buf;
                frames_per_buf = floorf ((x + 1) * frames_per_x * frames_size) - f_offset;
                frames_per_buf = frames_per_buf > (max_frames_per_x * frames_size) ? (max_frames_per_x * frames_size) : frames_per_buf;
                frames_per_buf = frames_per_buf + ((frames_size) -(frames_per_buf % (frames_size)));
            }
        }
        deadbeef->mutex_unlock (w->mutex);

        // center line
        if (!render.rectified) {
            DRECT pts = { left, top + (0.5 * height) - 0.5, left + width, top + (0.5 * height) + 0.5 };
            cairo_set_line_width (temp_cr, BORDER_LINE_WIDTH);
            draw_cairo_line (temp_cr, &pts, &render.c_cl);
        }
        cairo_destroy (temp_cr);
    }
    if (dec && fileinfo) {
        dec->free (fileinfo);
        fileinfo = NULL;
    }
    return;
}

gboolean
waveform_generate_wavedata (gpointer user_data)
{
    w_waveform_t *w = user_data;
    int channel = w->channel;
    double width = 4096;
    int channels;

    long frames_per_buf, buffer_len;

    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    DB_fileinfo_t *fileinfo = NULL;
    if (it) {
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
        if (dec) {
            fileinfo = dec->open (0);
            if (fileinfo && dec->init (fileinfo, DB_PLAYITEM (it)) != 0) {
                deadbeef->pl_lock ();
                fprintf (stderr, "waveform: failed to decode file %s\n", deadbeef->pl_find_meta (it, ":URI"));
                deadbeef->pl_unlock ();
            }
            float* data;
            float* buffer;
            if (fileinfo) {
                int nframes_per_channel = (int)deadbeef->pl_get_item_duration(it) * fileinfo->fmt.samplerate;
                //int nframes = nframes_per_channel * fileinfo->fmt.channels;
                const float frames_per_x = (float) nframes_per_channel / (float) width;
                const long max_frames_per_x = 1 + ceilf (frames_per_x);

                if (channel < 0 || channel >= fileinfo->fmt.channels) {
                    printf ("invalid channel\n");
                    deadbeef->pl_item_unref (it);
                    return FALSE;
                }

                deadbeef->mutex_lock (w->mutex);
                data = malloc (sizeof (float) * max_frames_per_x * fileinfo->fmt.channels);
                if (!data) {
                    printf ("out of memory.\n");
                    deadbeef->pl_item_unref (it);
                    deadbeef->mutex_unlock (w->mutex);
                    return FALSE;
                }
                memset (data, 0, sizeof (float) * max_frames_per_x * fileinfo->fmt.channels);
                deadbeef->mutex_unlock (w->mutex);

                deadbeef->mutex_lock (w->mutex);
                buffer = malloc (sizeof (float) * max_frames_per_x * fileinfo->fmt.channels);
                if (!buffer) {
                    printf ("out of memory.\n");
                    deadbeef->pl_item_unref (it);
                    deadbeef->mutex_unlock (w->mutex);
                    return FALSE;
                }
                memset (buffer, 0, sizeof (float) * max_frames_per_x * fileinfo->fmt.channels);
                deadbeef->mutex_unlock (w->mutex);

                channels = (channel > 0) ? 1 : fileinfo->fmt.channels;
                frames_per_buf = floorf (frames_per_x);
                buffer_len = frames_per_buf * fileinfo->fmt.channels;

                ddb_waveformat_t out_fmt = {
                    .bps = 32,
                    .channels = fileinfo->fmt.channels,
                    .samplerate = fileinfo->fmt.samplerate,
                    .channelmask = fileinfo->fmt.channelmask,
                    .is_float = 1,
                    .is_bigendian = 0
                };

                int eof = 0;
                int counter = 0;
                while (TRUE) {
                    if (eof) {
                        //printf ("end of file.\n");
                        break;
                    }

                    if (buffer_len % (2*fileinfo->fmt.channels) != 0) {
                        buffer_len -= fileinfo->fmt.channels;
                    }

                    //ugly hack
                    //buffer_len = buffer_len + ((fileinfo->fmt.channels * 2) -(buffer_len % (fileinfo->fmt.channels * 2)));

                    int sz = dec->read (fileinfo, (char *)buffer, buffer_len);
                    if (sz != buffer_len) {
                        eof = 1;
                    }
                    deadbeef->pcm_convert (&fileinfo->fmt, (char *)buffer, &out_fmt, (char *)data, buffer_len);

                    int frame;
                    float min, max, rms;
                    int ch;
                    for (ch = 0; ch < fileinfo->fmt.channels; ch++) {
                        if (counter >= w->max_buffer_len) {
                            break;
                        }
                        min = 1.0; max = -1.0; rms = 0.0;
                        /*if (channel > 0 && ch + 1 != channel)
                            continue;*/
                        for (frame = 0; frame < frames_per_buf - ch; frame++) {
                            if (frame * fileinfo->fmt.channels - ch > buffer_len) {
                                fprintf (stderr, "index error!\n");
                                break;
                            }
                            const float sample_val = data [frame * fileinfo->fmt.channels + ch];
                            max = MAX (max, sample_val);
                            min = MIN (min, sample_val);
                            rms += (sample_val * sample_val);
                        }
                        rms /= frames_per_buf * channels;
                        rms = sqrt (rms);
                        // write(fd,(const char *)&max,sizeof(float));
                        // write(fd,(const char *)&min,sizeof(float));
                        // write(fd,(const char *)&rms,sizeof(float));
                        deadbeef->mutex_lock (w->mutex);
                        w->buffer[counter] = max;
                        w->buffer[counter+1] = min;
                        w->buffer[counter+2] = rms;
                        deadbeef->mutex_unlock (w->mutex);
                        counter += 3;
                    }
                }
                w->buffer_len = counter;
                free (data);
                free (buffer);
            }
        }

        deadbeef->pl_item_unref (it);
        if (dec && fileinfo) {
            dec->free (fileinfo);
            fileinfo = NULL;
        }
    }
    return TRUE;
}

void
waveform_get_wavedata (gpointer user_data)
{
    deadbeef->background_job_increment ();
    w_waveform_t *w = user_data;
    waveform_generate_wavedata (user_data);
    waveform_render (w);
    deadbeef->background_job_decrement ();
}

gboolean
waveform_expose_event (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
    cairo_t *cr = gdk_cairo_create (gtk_widget_get_window (widget));
    w_waveform_t *w = user_data;
    gboolean res = waveform_seekbar_render (widget, cr, w);
    cairo_destroy (cr);
    return res;
}

gboolean
waveform_configure_event (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    if (w->resizetimer) {
        g_source_remove (w->resizetimer);
    }
    w->resizetimer = g_timeout_add (500, waveform_redraw_thread, user_data);
    return FALSE;
}

gboolean
waveform_motion_notify_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    if (w->seekbar_moving) {
        GtkAllocation a;
        gtk_widget_get_allocation (widget, &a);
        w->seekbar_move_x = event->x;
        gtk_widget_queue_draw (widget);
    }
    return TRUE;
}

gboolean
waveform_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    if (event->button == 3)
    {
      return TRUE;
    }
    w->seekbar_moving = 1;
    w->seekbar_moved = 0.0;
    GtkAllocation a;
    gtk_widget_get_allocation (widget, &a);
    w->seekbar_move_x = event->x;
    return TRUE;
}

gboolean
waveform_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    if (event->button == 3)
    {
      gtk_menu_popup (GTK_MENU (w->popup), NULL, NULL, NULL, w->drawarea, 0, gtk_get_current_event_time());
      return TRUE;
    }
    w->seekbar_moving = 0;
    w->seekbar_moved = 1.0;
    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (trk) {
        GtkAllocation a;
        gtk_widget_get_allocation (widget, &a);
        float time = (event->x - a.x) * deadbeef->pl_get_item_duration (trk) / (a.width);
        if (time < 0) {
            time = 0;
        }
        deadbeef->sendmessage (DB_EV_SEEK, 0, time * 1000, 0);
        deadbeef->pl_item_unref (trk);
    }
    gtk_widget_queue_draw (widget);
    return TRUE;
}

static int
waveform_message (ddb_gtkui_widget_t *widget, uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
    w_waveform_t *w = (w_waveform_t *)widget;
    intptr_t tid;
    switch (id) {
    case DB_EV_SONGSTARTED:
        tid = deadbeef->thread_start (waveform_get_wavedata, w);
        deadbeef->thread_detach (tid);
        break;
    case DB_EV_TRACKINFOCHANGED:
        {
            ddb_event_track_t *ev = (ddb_event_track_t *)ctx;
            DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
            if (it == ev->track) {
                g_idle_add (waveform_redraw_cb, w);
            }
            if (it) {
                deadbeef->pl_item_unref (it);
            }
        }
        break;
    case DB_EV_CONFIGCHANGED:
        {
            on_config_changed (ctx);
            g_idle_add (waveform_redraw_cb, w);
        }
        break;
    }
    return 0;
}

static void
on_button_config (GtkMenuItem *menuitem, gpointer user_data) {
    GtkWidget *waveform_properties;
    GtkWidget *config_dialog;
    GtkWidget *vbox01;
    GtkWidget *label00;
    GtkWidget *frame01;
    GtkWidget *hbox01;
    GtkWidget *vbox11;
    GtkWidget *label01;
    GtkWidget *background_color;
    GtkWidget *vbox12;
    GtkWidget *label02;
    GtkWidget *foreground_color;
    GtkWidget *vbox13;
    GtkWidget *label03;
    GtkWidget *progressbar_color;
    GtkWidget *downmix_to_mono;
    GtkWidget *log_scale;
    GtkWidget *dialog_action_area13;
    GtkWidget *applybutton1;
    GtkWidget *cancelbutton1;
    GtkWidget *okbutton1;

    waveform_properties = gtk_dialog_new ();
    gtk_widget_set_size_request (waveform_properties, -1, 300);
    gtk_window_set_title (GTK_WINDOW (waveform_properties), "Waveform Properties");
    gtk_window_set_type_hint (GTK_WINDOW (waveform_properties), GDK_WINDOW_TYPE_HINT_DIALOG);

    config_dialog = gtk_dialog_get_content_area (GTK_DIALOG (waveform_properties));
    gtk_widget_show (config_dialog);

    vbox01 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox01);
    gtk_box_pack_start (GTK_BOX (config_dialog), vbox01, FALSE, FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (vbox01), 12);

    label00 = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL(label00),"<b>Colors</b>");
    gtk_widget_show (label00);

    frame01 = gtk_frame_new ("Colors");
    gtk_frame_set_label_widget ((GtkFrame *)frame01, label00);
    gtk_frame_set_shadow_type ((GtkFrame *)frame01, GTK_SHADOW_IN);
    gtk_widget_show (frame01);
    gtk_box_pack_start (GTK_BOX (vbox01), frame01, FALSE, FALSE, 0);

    hbox01 = gtk_hbox_new (FALSE, 8);
    gtk_widget_show (hbox01);
    gtk_container_add (GTK_CONTAINER (frame01), hbox01);
    gtk_container_set_border_width (GTK_CONTAINER (hbox01), 6);

    vbox11 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox11);
    gtk_box_pack_start (GTK_BOX (hbox01), vbox11, TRUE, FALSE, 0);
    // gtk_container_set_border_width (GTK_CONTAINER (frame01), 6);

    label01 = gtk_label_new ("Background");
    gtk_widget_show (label01);
    gtk_box_pack_start (GTK_BOX (vbox11), label01, FALSE, FALSE, 0);

    background_color = gtk_color_button_new ();
    gtk_widget_show (background_color);
    gtk_box_pack_start (GTK_BOX (vbox11), background_color, FALSE, FALSE, 0);

    vbox12 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox12);
    gtk_box_pack_start (GTK_BOX (hbox01), vbox12, TRUE, FALSE, 0);
    // gtk_container_set_border_width (GTK_CONTAINER (vbox12), 6);

    label02 = gtk_label_new ("Foreground");
    gtk_widget_show (label02);
    gtk_box_pack_start (GTK_BOX (vbox12), label02, FALSE, FALSE, 0);

    foreground_color = gtk_color_button_new ();
    gtk_widget_show (foreground_color);
    gtk_box_pack_start (GTK_BOX (vbox12), foreground_color, FALSE, FALSE, 0);

    vbox13 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox13);
    gtk_box_pack_start (GTK_BOX (hbox01), vbox13, TRUE, FALSE, 0);
    // gtk_container_set_border_width (GTK_CONTAINER (frame01), 6);

    label03 = gtk_label_new ("Progressbar");
    gtk_widget_show (label03);
    gtk_box_pack_start (GTK_BOX (vbox13), label03, FALSE, FALSE, 0);

    progressbar_color = gtk_color_button_new ();
    gtk_widget_show (progressbar_color);
    gtk_box_pack_start (GTK_BOX (vbox13), progressbar_color, FALSE, FALSE, 0);

    downmix_to_mono = gtk_check_button_new_with_mnemonic ("Downmix to mono");
    gtk_widget_show (downmix_to_mono);
    gtk_box_pack_start (GTK_BOX (vbox01), downmix_to_mono, FALSE, FALSE, 0);

    log_scale = gtk_check_button_new_with_mnemonic ("Logarithmic scale");
    gtk_widget_show (log_scale);
    gtk_box_pack_start (GTK_BOX (vbox01), log_scale, FALSE, FALSE, 0);

    dialog_action_area13 = gtk_dialog_get_action_area (GTK_DIALOG (waveform_properties));
    gtk_widget_show (dialog_action_area13);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (dialog_action_area13), GTK_BUTTONBOX_END);

    applybutton1 = gtk_button_new_from_stock ("gtk-apply");
    gtk_widget_show (applybutton1);
    gtk_dialog_add_action_widget (GTK_DIALOG (waveform_properties), applybutton1, GTK_RESPONSE_APPLY);
    gtk_widget_set_can_default(applybutton1, TRUE);

    cancelbutton1 = gtk_button_new_from_stock ("gtk-cancel");
    gtk_widget_show (cancelbutton1);
    gtk_dialog_add_action_widget (GTK_DIALOG (waveform_properties), cancelbutton1, GTK_RESPONSE_CANCEL);
    gtk_widget_set_can_default(cancelbutton1, TRUE);

    okbutton1 = gtk_button_new_from_stock ("gtk-ok");
    gtk_widget_show (okbutton1);
    gtk_dialog_add_action_widget (GTK_DIALOG (waveform_properties), okbutton1, GTK_RESPONSE_OK);
    gtk_widget_set_can_default(okbutton1, TRUE);

    // GtkWidget *downmix_to_mono = lookup_widget (dlg, "downmix_to_mono");
    gtk_color_button_set_color (GTK_COLOR_BUTTON (background_color), &CONFIG_BG_COLOR);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (foreground_color), &CONFIG_FG_COLOR);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (progressbar_color), &CONFIG_PB_COLOR);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (downmix_to_mono), CONFIG_MIX_TO_MONO);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (log_scale), CONFIG_LOG_ENABLED);

    for (;;) {
        int response = gtk_dialog_run (GTK_DIALOG (waveform_properties));
        if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY) {
            gtk_color_button_get_color (GTK_COLOR_BUTTON (background_color), &CONFIG_BG_COLOR);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (foreground_color), &CONFIG_FG_COLOR);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (progressbar_color), &CONFIG_PB_COLOR);
            CONFIG_MIX_TO_MONO = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (downmix_to_mono));
            CONFIG_LOG_ENABLED = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (log_scale));
            save_config ();
            deadbeef->sendmessage (DB_EV_CONFIGCHANGED, 0, 0, 0);
        }
        if (response == GTK_RESPONSE_APPLY) {
            continue;
        }
        break;
    }
    gtk_widget_destroy (waveform_properties);
    return;
}

void
w_waveform_init (ddb_gtkui_widget_t *w)
{
    w_waveform_t *wf = (w_waveform_t *)w;
    wf->max_buffer_len = VALUES_PER_FRAME * sizeof(float) * MAX_VALUES_PER_CHANNEL * 4;
    deadbeef->mutex_lock (wf->mutex);
    wf->buffer = malloc (sizeof (float) * wf->max_buffer_len);
    memset (wf->buffer, 0, sizeof (float) * wf->max_buffer_len);
    deadbeef->mutex_unlock (wf->mutex);
    if (wf->drawtimer) {
        g_source_remove (wf->drawtimer);
        wf->drawtimer = 0;
    }
    if (wf->resizetimer) {
        g_source_remove (wf->resizetimer);
        wf->resizetimer = 0;
    }
    wf->drawtimer = g_timeout_add (33, w_waveform_draw_cb, w);
}

static ddb_gtkui_widget_t *
w_waveform_create (void)
{
    w_waveform_t *w = malloc (sizeof (w_waveform_t));
    memset (w, 0, sizeof (w_waveform_t));

    w->base.widget = gtk_event_box_new ();
    w->base.init = w_waveform_init;
    w->base.destroy = w_waveform_destroy;
    w->base.message = waveform_message;
    w->drawarea = gtk_drawing_area_new ();
    w->popup = gtk_menu_new ();
    w->popup_item = gtk_menu_item_new_with_mnemonic ("Configure");
    w->mutex = deadbeef->mutex_create ();
    gtk_widget_show (w->drawarea);
    gtk_container_add (GTK_CONTAINER (w->base.widget), w->drawarea);
    gtk_widget_show (w->popup);
    gtk_container_add (GTK_CONTAINER (w->base.widget), w->popup);
    gtk_widget_show (w->popup_item);
    gtk_container_add (GTK_CONTAINER (w->popup), w->popup_item);

#if !GTK_CHECK_VERSION(3,0,0)
    g_signal_connect_after ((gpointer) w->drawarea, "expose_event", G_CALLBACK (waveform_expose_event), w);
#else
    //g_signal_connect_after ((gpointer) w->drawarea, "draw", G_CALLBACK (waveform_draw), w);
#endif
    g_signal_connect_after ((gpointer) w->drawarea, "configure_event", G_CALLBACK (waveform_configure_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "button_press_event", G_CALLBACK (waveform_button_press_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "button_release_event", G_CALLBACK (waveform_button_release_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "motion_notify_event", G_CALLBACK (waveform_motion_notify_event), w);
    g_signal_connect_after ((gpointer) w->popup_item, "activate", G_CALLBACK (on_button_config), w);
    gtkui_plugin->w_override_signals (w->base.widget, w);
    return (ddb_gtkui_widget_t *)w;
}

int
waveform_connect (void)
{
    gtkui_plugin = (ddb_gtkui_t *) deadbeef->plug_get_for_id (DDB_GTKUI_PLUGIN_ID);
    if (gtkui_plugin) {
        //trace("using '%s' plugin %d.%d\n", DDB_GTKUI_PLUGIN_ID, gtkui_plugin->gui.plugin.version_major, gtkui_plugin->gui.plugin.version_minor );
        if (gtkui_plugin->gui.plugin.version_major == 2) {
            printf ("fb api2\n");
            // 0.6+, use the new widget API
            gtkui_plugin->w_reg_widget ("Waveform Seekbar", DDB_WF_SINGLE_INSTANCE, w_waveform_create, "waveform_seekbar", NULL);
            return 0;
        }
    }
    return -1;
}

int
waveform_start (void)
{
    load_config ();
    return 0;
}

int
waveform_stop (void)
{
    save_config ();
    return 0;
}

int
waveform_startup (GtkWidget *cont)
{
    return 0;
}

int
waveform_shutdown (GtkWidget *cont)
{
    return 0;
}
int
waveform_disconnect (void)
{
    gtkui_plugin = NULL;
    return 0;
}

/*
static const char settings_dlg[] =
    "property \"Logarithmic scale \"                     checkbox "      CONFSTR_WF_LOG_ENABLED        " 0 ;\n"
    "property \"Downmix to mono \"                     checkbox "      CONFSTR_WF_MIX_TO_MONO        " 0 ;\n"
    "property \"Background color (r): \"          spinbtn[0,255,1] "      CONFSTR_WF_BG_COLOR_R        " 60 ;\n"
    "property \"Background color (g): \"          spinbtn[0,255,1] "      CONFSTR_WF_BG_COLOR_G        " 60 ;\n"
    "property \"Background color (b): \"          spinbtn[0,255,1] "      CONFSTR_WF_BG_COLOR_B        " 60 ;\n"
    "property \"Foreground color (r): \"          spinbtn[0,255,1] "      CONFSTR_WF_FG_COLOR_R       " 120 ;\n"
    "property \"Foreground color (g): \"          spinbtn[0,255,1] "      CONFSTR_WF_FG_COLOR_G       " 120 ;\n"
    "property \"Foreground color (b): \"          spinbtn[0,255,1] "      CONFSTR_WF_FG_COLOR_B       " 120 ;\n"
    "property \"Progressbar color (r): \"         spinbtn[0,255,1] "      CONFSTR_WF_PB_COLOR_R      " 255 ;\n"
    "property \"Progressbar color (g): \"         spinbtn[0,255,1] "      CONFSTR_WF_PB_COLOR_G        " 0 ;\n"
    "property \"Progressbar color (b): \"         spinbtn[0,255,1] "      CONFSTR_WF_PB_COLOR_B        " 0 ;\n"
;
*/
static DB_misc_t plugin = {
    //DB_PLUGIN_SET_API_VERSION
    .plugin.type            = DB_PLUGIN_MISC,
    .plugin.api_vmajor      = 1,
    .plugin.api_vminor      = 0,
    .plugin.version_major   = 0,
    .plugin.version_minor   = 1,
    .plugin.id              = "waveform_seekbar",
    .plugin.name            = "Waveform Seekbar",
    .plugin.descr           = "Waveform Seekbar",
    .plugin.copyright       =
        "Copyright (C) 2013 Christian Boxdörfer <christian.boxdoerfer@posteo.de>\n"
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
    .plugin.website         = "TODO",
    .plugin.start           = waveform_start,
    .plugin.stop            = waveform_stop,
    .plugin.connect         = waveform_connect,
    .plugin.disconnect      = waveform_disconnect,
    // .plugin.configdialog    = settings_dlg,
};

DB_plugin_t *
ddb_misc_waveform_load (DB_functions_t *ddb) {
    deadbeef = ddb;
    return &plugin.plugin;
}