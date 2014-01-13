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
#include "cache.h"

#define C_COLOUR(X) (X)->r, (X)->g, (X)->b, (X)->a

#define M_PI (3.1415926535897932384626433832795029)
#define BORDER_LINE_WIDTH   (1.0)
#define BARS (1)
#define SPIKES (2)
// min, max, rms
#define VALUES_PER_FRAME (3)
#define MAX_VALUES_PER_CHANNEL (2048)

#define     CONFSTR_WF_LOG_ENABLED       "waveform.log_enabled"
#define     CONFSTR_WF_MIX_TO_MONO       "waveform.mix_to_mono"
#define     CONFSTR_WF_DISPLAY_RMS       "waveform.display_rms"
#define     CONFSTR_WF_RENDER_METHOD     "waveform.render_method"
#define     CONFSTR_WF_BG_COLOR_R        "waveform.bg_color_r"
#define     CONFSTR_WF_BG_COLOR_G        "waveform.bg_color_g"
#define     CONFSTR_WF_BG_COLOR_B        "waveform.bg_color_b"
#define     CONFSTR_WF_BG_ALPHA          "waveform.bg_alpha"
#define     CONFSTR_WF_FG_COLOR_R        "waveform.fg_color_r"
#define     CONFSTR_WF_FG_COLOR_G        "waveform.fg_color_g"
#define     CONFSTR_WF_FG_COLOR_B        "waveform.fg_color_b"
#define     CONFSTR_WF_FG_ALPHA          "waveform.fg_alpha"
#define     CONFSTR_WF_PB_COLOR_R        "waveform.pb_color_r"
#define     CONFSTR_WF_PB_COLOR_G        "waveform.pb_color_g"
#define     CONFSTR_WF_PB_COLOR_B        "waveform.pb_color_b"
#define     CONFSTR_WF_PB_ALPHA          "waveform.pb_alpha"
#define     CONFSTR_WF_FG_RMS_COLOR_R    "waveform.fg_rms_color_r"
#define     CONFSTR_WF_FG_RMS_COLOR_G    "waveform.fg_rms_color_g"
#define     CONFSTR_WF_FG_RMS_COLOR_B    "waveform.fg_rms_color_b"
#define     CONFSTR_WF_FG_RMS_ALPHA      "waveform.fg_rms_alpha"

#define     CONFSTR_WF_MAX_FILE_LENGTH   "waveform.max_file_length"
#define     CONFSTR_WF_CACHE_ENABLED     "waveform.cache_enabled"


/* Global variables */
static DB_misc_t            plugin;
static DB_functions_t *     deadbeef = NULL;
static ddb_gtkui_t *        gtkui_plugin = NULL;

static char cache_path[PATH_MAX];
static int cache_path_size;

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
    int channels;
    int read;
    int rendering;
    int nsamples;
    int seekbar_moving;
    float seekbar_moved;
    float seekbar_move_x;
    float seekbar_move_x_clicked;
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
{
    gboolean border, logscale, rectified;
    COLOUR c_fg, c_rms, c_bg, c_ann, c_bbg, c_cl;
    double border_width;
} RENDER;

enum WHAT { PEAK = 1, RMS = 2 };

static gboolean CONFIG_LOG_ENABLED = FALSE;
static gboolean CONFIG_MIX_TO_MONO = FALSE;
static gboolean CONFIG_DISPLAY_RMS = TRUE;
static GdkColor CONFIG_BG_COLOR;
static GdkColor CONFIG_FG_COLOR;
static GdkColor CONFIG_PB_COLOR;
static GdkColor CONFIG_FG_RMS_COLOR;
static guint16  CONFIG_BG_ALPHA;
static guint16  CONFIG_FG_ALPHA;
static guint16  CONFIG_PB_ALPHA;
static guint16  CONFIG_FG_RMS_ALPHA;
static gint     CONFIG_RENDER_METHOD = SPIKES;
static gint     CONFIG_MAX_FILE_LENGTH = 180;
static gboolean CONFIG_CACHE_ENABLED = FALSE;

static void
save_config (void)
{
    deadbeef->conf_set_int (CONFSTR_WF_LOG_ENABLED,         CONFIG_LOG_ENABLED);
    deadbeef->conf_set_int (CONFSTR_WF_MIX_TO_MONO,         CONFIG_MIX_TO_MONO);
    deadbeef->conf_set_int (CONFSTR_WF_DISPLAY_RMS,         CONFIG_DISPLAY_RMS);
    deadbeef->conf_set_int (CONFSTR_WF_RENDER_METHOD,       CONFIG_RENDER_METHOD);
    deadbeef->conf_set_int (CONFSTR_WF_MAX_FILE_LENGTH,     CONFIG_MAX_FILE_LENGTH);
    deadbeef->conf_set_int (CONFSTR_WF_CACHE_ENABLED,       CONFIG_CACHE_ENABLED);
    deadbeef->conf_set_int (CONFSTR_WF_BG_COLOR_R,          CONFIG_BG_COLOR.red);
    deadbeef->conf_set_int (CONFSTR_WF_BG_COLOR_G,          CONFIG_BG_COLOR.green);
    deadbeef->conf_set_int (CONFSTR_WF_BG_COLOR_B,          CONFIG_BG_COLOR.blue);
    deadbeef->conf_set_int (CONFSTR_WF_BG_ALPHA,            CONFIG_BG_ALPHA);
    deadbeef->conf_set_int (CONFSTR_WF_FG_COLOR_R,          CONFIG_FG_COLOR.red);
    deadbeef->conf_set_int (CONFSTR_WF_FG_COLOR_G,          CONFIG_FG_COLOR.green);
    deadbeef->conf_set_int (CONFSTR_WF_FG_COLOR_B,          CONFIG_FG_COLOR.blue);
    deadbeef->conf_set_int (CONFSTR_WF_FG_ALPHA,            CONFIG_FG_ALPHA);
    deadbeef->conf_set_int (CONFSTR_WF_PB_COLOR_R,          CONFIG_PB_COLOR.red);
    deadbeef->conf_set_int (CONFSTR_WF_PB_COLOR_G,          CONFIG_PB_COLOR.green);
    deadbeef->conf_set_int (CONFSTR_WF_PB_COLOR_B,          CONFIG_PB_COLOR.blue);
    deadbeef->conf_set_int (CONFSTR_WF_PB_ALPHA,            CONFIG_PB_ALPHA);
    deadbeef->conf_set_int (CONFSTR_WF_FG_RMS_COLOR_R,      CONFIG_FG_RMS_COLOR.red);
    deadbeef->conf_set_int (CONFSTR_WF_FG_RMS_COLOR_G,      CONFIG_FG_RMS_COLOR.green);
    deadbeef->conf_set_int (CONFSTR_WF_FG_RMS_COLOR_B,      CONFIG_FG_RMS_COLOR.blue);
    deadbeef->conf_set_int (CONFSTR_WF_FG_RMS_ALPHA,        CONFIG_FG_RMS_ALPHA);
}

static void
load_config (void)
{
    deadbeef->conf_lock ();
    CONFIG_LOG_ENABLED = deadbeef->conf_get_int (CONFSTR_WF_LOG_ENABLED,             FALSE);
    CONFIG_MIX_TO_MONO = deadbeef->conf_get_int (CONFSTR_WF_MIX_TO_MONO,             FALSE);
    CONFIG_DISPLAY_RMS = deadbeef->conf_get_int (CONFSTR_WF_DISPLAY_RMS,              TRUE);
    CONFIG_RENDER_METHOD = deadbeef->conf_get_int (CONFSTR_WF_RENDER_METHOD,        SPIKES);
    CONFIG_MAX_FILE_LENGTH = deadbeef->conf_get_int (CONFSTR_WF_MAX_FILE_LENGTH,       180);
    CONFIG_CACHE_ENABLED = deadbeef->conf_get_int (CONFSTR_WF_CACHE_ENABLED,         FALSE);

    CONFIG_BG_COLOR.red = deadbeef->conf_get_int (CONFSTR_WF_BG_COLOR_R,             50000);
    CONFIG_BG_COLOR.green = deadbeef->conf_get_int (CONFSTR_WF_BG_COLOR_G,           50000);
    CONFIG_BG_COLOR.blue = deadbeef->conf_get_int (CONFSTR_WF_BG_COLOR_B,            50000);
    CONFIG_BG_ALPHA = deadbeef->conf_get_int (CONFSTR_WF_BG_ALPHA,                   65535);

    CONFIG_FG_COLOR.red = deadbeef->conf_get_int (CONFSTR_WF_FG_COLOR_R,             20000);
    CONFIG_FG_COLOR.green = deadbeef->conf_get_int (CONFSTR_WF_FG_COLOR_G,           20000);
    CONFIG_FG_COLOR.blue = deadbeef->conf_get_int (CONFSTR_WF_FG_COLOR_B,            20000);
    CONFIG_FG_ALPHA = deadbeef->conf_get_int (CONFSTR_WF_FG_ALPHA,                   65535);

    CONFIG_PB_COLOR.red = deadbeef->conf_get_int (CONFSTR_WF_PB_COLOR_R,                 0);
    CONFIG_PB_COLOR.green = deadbeef->conf_get_int (CONFSTR_WF_PB_COLOR_G,           65535);
    CONFIG_PB_COLOR.blue = deadbeef->conf_get_int (CONFSTR_WF_PB_COLOR_B,                0);
    CONFIG_PB_ALPHA = deadbeef->conf_get_int (CONFSTR_WF_PB_ALPHA,                   20000);

    CONFIG_FG_RMS_COLOR.red = deadbeef->conf_get_int (CONFSTR_WF_FG_RMS_COLOR_R,      5000);
    CONFIG_FG_RMS_COLOR.green = deadbeef->conf_get_int (CONFSTR_WF_FG_RMS_COLOR_G,    5000);
    CONFIG_FG_RMS_COLOR.blue = deadbeef->conf_get_int (CONFSTR_WF_FG_RMS_COLOR_B,     5000);
    CONFIG_FG_RMS_ALPHA = deadbeef->conf_get_int (CONFSTR_WF_FG_RMS_ALPHA,           65535);
    deadbeef->conf_unlock ();
}

static int
on_config_changed (uintptr_t ctx)
{
    load_config ();
    return 0;
}

static void
on_button_config (GtkMenuItem *menuitem, gpointer user_data) {
    GtkWidget *waveform_properties;
    GtkWidget *config_dialog;
    GtkWidget *vbox01;
    GtkWidget *label00;
    GtkWidget *frame01;
    GtkWidget *table01;
    GtkWidget *label01;
    GtkWidget *background_color;
    GtkWidget *label02;
    GtkWidget *foreground_color;
    GtkWidget *label05;
    GtkWidget *foreground_rms_color;
    GtkWidget *label03;
    GtkWidget *progressbar_color;
    GtkWidget *downmix_to_mono;
    GtkWidget *log_scale;
    GtkWidget *display_rms;
    GtkWidget *label04;
    GtkWidget *frame02;
    GtkWidget *vbox14;
    GtkWidget *render_method_spikes;
    GtkWidget *render_method_bars;
    GtkWidget *dialog_action_area13;
    GtkWidget *applybutton1;
    GtkWidget *cancelbutton1;
    GtkWidget *okbutton1;

    waveform_properties = gtk_dialog_new ();
    gtk_widget_set_size_request (waveform_properties, -1, 350);
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
    gtk_box_pack_start (GTK_BOX (vbox01), frame01, TRUE, FALSE, 0);

    table01 = gtk_table_new (2, 4, TRUE);
    gtk_widget_show (table01);
    gtk_container_add (GTK_CONTAINER (frame01), table01);
    gtk_table_set_col_spacings ((GtkTable *) table01, 8);
    gtk_container_set_border_width (GTK_CONTAINER (table01), 6);

    label01 = gtk_label_new ("Background");
    gtk_widget_show (label01);
    gtk_table_attach_defaults ((GtkTable *) table01, label01, 0,1,0,1);

    label02 = gtk_label_new ("Waveform");
    gtk_widget_show (label02);
    gtk_table_attach_defaults ((GtkTable *) table01, label02, 1,2,0,1);

    label05 = gtk_label_new ("RMS");
    gtk_widget_show (label05);
    gtk_table_attach_defaults ((GtkTable *) table01, label05, 2,3,0,1);

    label03 = gtk_label_new ("Progressbar");
    gtk_widget_show (label03);
    gtk_table_attach_defaults ((GtkTable *) table01, label03, 3,4,0,1);

    background_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)background_color, TRUE);
    gtk_widget_show (background_color);
    gtk_table_attach_defaults ((GtkTable *) table01, background_color, 0,1,1,2);

    foreground_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)foreground_color, TRUE);
    gtk_widget_show (foreground_color);
    gtk_table_attach_defaults ((GtkTable *) table01, foreground_color, 1,2,1,2);

    foreground_rms_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)foreground_rms_color, TRUE);
    gtk_widget_show (foreground_rms_color);
    gtk_table_attach_defaults ((GtkTable *) table01, foreground_rms_color, 2,3,1,2);

    progressbar_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)progressbar_color, TRUE);
    gtk_widget_show (progressbar_color);
    gtk_table_attach_defaults ((GtkTable *) table01, progressbar_color, 3,4,1,2);

    label04 = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL(label04),"<b>Style</b>");
    gtk_widget_show (label04);

    frame02 = gtk_frame_new ("Style");
    gtk_frame_set_label_widget ((GtkFrame *)frame02, label04);
    gtk_frame_set_shadow_type ((GtkFrame *)frame02, GTK_SHADOW_IN);
    gtk_widget_show (frame02);
    gtk_box_pack_start (GTK_BOX (vbox01), frame02, FALSE, FALSE, 0);

    vbox14 = gtk_vbox_new (FALSE, 6);
    gtk_widget_show (vbox14);
    gtk_container_add (GTK_CONTAINER (frame02), vbox14);

    render_method_spikes = gtk_radio_button_new_with_label (NULL, "Spikes");
    gtk_widget_show (render_method_spikes);
    gtk_box_pack_start (GTK_BOX (vbox14), render_method_spikes, TRUE, TRUE, 0);

    render_method_bars = gtk_radio_button_new_with_label_from_widget ((GtkRadioButton *)render_method_spikes, "Bars");
    gtk_widget_show (render_method_bars);
    gtk_box_pack_start (GTK_BOX (vbox14), render_method_bars, TRUE, TRUE, 0);

    downmix_to_mono = gtk_check_button_new_with_label ("Downmix to mono");
    gtk_widget_show (downmix_to_mono);
    gtk_box_pack_start (GTK_BOX (vbox01), downmix_to_mono, FALSE, FALSE, 0);

    log_scale = gtk_check_button_new_with_label ("Logarithmic scale");
    gtk_widget_show (log_scale);
    gtk_box_pack_start (GTK_BOX (vbox01), log_scale, FALSE, FALSE, 0);

    display_rms = gtk_check_button_new_with_label ("Display RMS");
    gtk_widget_show (display_rms);
    gtk_box_pack_start (GTK_BOX (vbox01), display_rms, FALSE, FALSE, 0);

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

    gtk_color_button_set_color (GTK_COLOR_BUTTON (background_color), &CONFIG_BG_COLOR);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (foreground_color), &CONFIG_FG_COLOR);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (progressbar_color), &CONFIG_PB_COLOR);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (foreground_rms_color), &CONFIG_FG_RMS_COLOR);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON (background_color), CONFIG_BG_ALPHA);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON (foreground_color), CONFIG_FG_ALPHA);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON (progressbar_color), CONFIG_PB_ALPHA);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON (foreground_rms_color), CONFIG_FG_RMS_ALPHA);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (downmix_to_mono), CONFIG_MIX_TO_MONO);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (log_scale), CONFIG_LOG_ENABLED);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (display_rms), CONFIG_DISPLAY_RMS);

    switch (CONFIG_RENDER_METHOD) {
    case SPIKES:
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (render_method_spikes), TRUE);
        break;
    case BARS:
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (render_method_bars), TRUE);
        break;
    }

    for (;;) {
        int response = gtk_dialog_run (GTK_DIALOG (waveform_properties));
        if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY) {
            gtk_color_button_get_color (GTK_COLOR_BUTTON (background_color), &CONFIG_BG_COLOR);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (foreground_color), &CONFIG_FG_COLOR);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (progressbar_color), &CONFIG_PB_COLOR);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (foreground_rms_color), &CONFIG_FG_RMS_COLOR);
            CONFIG_BG_ALPHA = gtk_color_button_get_alpha (GTK_COLOR_BUTTON (background_color));
            CONFIG_FG_ALPHA = gtk_color_button_get_alpha (GTK_COLOR_BUTTON (foreground_color));
            CONFIG_PB_ALPHA = gtk_color_button_get_alpha (GTK_COLOR_BUTTON (progressbar_color));
            CONFIG_FG_RMS_ALPHA = gtk_color_button_get_alpha (GTK_COLOR_BUTTON (foreground_rms_color));
            CONFIG_MIX_TO_MONO = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (downmix_to_mono));
            CONFIG_LOG_ENABLED = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (log_scale));
            CONFIG_DISPLAY_RMS = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (display_rms));
            if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (render_method_spikes)) == TRUE) {
                CONFIG_RENDER_METHOD = SPIKES;
            }
            else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (render_method_bars)) == TRUE) {
                CONFIG_RENDER_METHOD = BARS;
            }
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

int
make_cache_dir (char *path, int size) {
    const char *cache = getenv ("XDG_CACHE_HOME");
    int sz;
    sz = snprintf (path, size, cache ? "%s/deadbeef/waveform/" : "%s/.cache/deadbeef/waveform/", cache ? cache : getenv ("HOME"));
    mkdir (path, 0700);
    return sz;
}

void
w_waveform_destroy (ddb_gtkui_widget_t *widget)
{
    w_waveform_t *w = (w_waveform_t *)widget;
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

static gboolean
w_waveform_draw_cb (void *user_data)
{
    w_waveform_t *w = user_data;
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
    intptr_t tid = deadbeef->thread_start_low_priority (waveform_render, w);
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

enum
{
    CORNER_NONE        = 0,
    CORNER_TOPLEFT     = 1,
    CORNER_TOPRIGHT    = 2,
    CORNER_BOTTOMLEFT  = 4,
    CORNER_BOTTOMRIGHT = 8,
    CORNER_ALL         = 15
};

void color_contrast (GdkColor * color)
{
    int d = 0;
    // Counting the perceptive luminance - human eye favors green color...
    double a = 1 - ( 0.299 * color->red + 0.587 * color->green + 0.114 * color->blue)/65535;
    if (a < 0.5)
        d = 0; // bright colors - black font
    else
        d = 65535; // dark colors - white font
    color->red = d;
    color->blue = d;
    color->green = d;
}

static void
clearlooks_rounded_rectangle (cairo_t * cr,
                  double x, double y, double w, double h,
                  double radius, uint8_t corners)
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
draw_cairo_line_path (cairo_t* cr, DRECT *pts, const COLOUR *c)
{
    cairo_set_source_rgba (cr, C_COLOUR (c));
    //cairo_move_to (cr, pts->x1, pts->y1);
    cairo_line_to (cr, pts->x2, pts->y2);
    //cairo_stroke (cr);
}

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

    int cursor_width = 3;
    double width = a.width;
    float pos = 0;
    float seek_pos = 0;

    if (w->rendering == 1) {
        return FALSE;
    }

    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (trk) {
        if (deadbeef->pl_get_item_duration (trk) > 0) {
            pos = (deadbeef->streamer_get_playpos () * width)/ deadbeef->pl_get_item_duration (trk);
        }
        else {
            pos = 0;
        }
        //deadbeef->pl_item_unref (trk);
    }

    if (a.height != w->height || a.width != w->width) {
        cairo_save (cr);
        cairo_translate(cr, 0, 0);
        cairo_scale (cr, width/w->width, a.height/w->height);
        cairo_set_source_surface (cr, w->surf, 0, 0);
        cairo_paint (cr);
        cairo_restore (cr);
    }
    else {
        cairo_set_source_surface (cr, w->surf, 0, 0);
        cairo_paint (cr);
    }

    cairo_set_source_rgba (cr,CONFIG_PB_COLOR.red/65535.f,CONFIG_PB_COLOR.green/65535.f,CONFIG_PB_COLOR.blue/65535.f,CONFIG_PB_ALPHA/65535.f);
    cairo_rectangle (cr, 0, 0, pos, a.height);
    cairo_fill (cr);

    cairo_set_source_rgb (cr,CONFIG_PB_COLOR.red/65535.f,CONFIG_PB_COLOR.green/65535.f,CONFIG_PB_COLOR.blue/65535.f);
    cairo_rectangle (cr, pos - cursor_width, 0, cursor_width, a.height);
    cairo_fill (cr);

    if (w->seekbar_moving && trk) {
        if (w->seekbar_move_x < 0) {
            seek_pos = 0;
        }
        else if (w->seekbar_move_x > width) {
            seek_pos = width;
        }
        else {
            seek_pos = w->seekbar_move_x;
        }

        cairo_set_source_rgb (cr,CONFIG_PB_COLOR.red/65535.f,CONFIG_PB_COLOR.green/65535.f,CONFIG_PB_COLOR.blue/65535.f);
        cairo_rectangle (cr, seek_pos - cursor_width, 0, cursor_width, a.height);
        cairo_fill (cr);
        if (w->seekbar_move_x != w->seekbar_move_x_clicked || w->seekbar_move_x_clicked == -1) {
            w->seekbar_move_x_clicked = -1;
            float time = 0;
            float dur = deadbeef->pl_get_item_duration (trk);

            if (w->seekbar_moved > 0) {
                time = deadbeef->streamer_get_playpos ();
            }
            else {
                time = w->seekbar_move_x * dur / (a.width);
            }

            if (time < 0) {
                time = 0;
            }
            if (time > dur) {
                time = dur;
            }
            char s[1000];
            int hr = time/3600;
            int mn = (time-hr*3600)/60;
            int sc = time-hr*3600-mn*60;
            snprintf (s, sizeof (s), "%02d:%02d:%02d", hr, mn, sc);

            cairo_set_source_rgba (cr, CONFIG_PB_COLOR.red/65535.f, CONFIG_PB_COLOR.green/65535.f, CONFIG_PB_COLOR.blue/65535.f, 1);
            cairo_save (cr);
            cairo_set_font_size (cr, 20);

            cairo_text_extents_t ex;
            cairo_text_extents (cr, s, &ex);

            int rec_width = ex.width + 10;
            int rec_height = ex.height + 10;
            int rec_pos = seek_pos - rec_width;
            int text_pos = rec_pos + 5;

            if (seek_pos < rec_width) {
                rec_pos = 0;
                text_pos = rec_pos + 5;
            }

            uint8_t corners = 0xff;

            clearlooks_rounded_rectangle (cr, rec_pos, (a.height - ex.height - 10)/2, rec_width, rec_height, 3, corners);
            cairo_fill (cr);
            cairo_move_to (cr, text_pos, (a.height + ex.height)/2);
            GdkColor color_text = CONFIG_PB_COLOR;
            color_contrast (&color_text);
            cairo_set_source_rgba (cr, color_text.red/65535.f, color_text.green/65535.f, color_text.blue/65535.f, 1);
            cairo_show_text (cr, s);
            cairo_restore (cr);
        }
    }

    if (trk) {
        deadbeef->pl_item_unref (trk);
    }
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
    float gain = 1.0;

    RENDER render =
    {
        /*border*/ FALSE,
        /*logscale*/ CONFIG_LOG_ENABLED,
        /*rectified*/ FALSE,
        /*foreground*/  { CONFIG_FG_COLOR.red/65535.f,CONFIG_FG_COLOR.green/65535.f,CONFIG_FG_COLOR.blue/65535.f, CONFIG_FG_ALPHA/65535.f },
        /*wave-rms*/    { CONFIG_FG_RMS_COLOR.red/65535.f,CONFIG_FG_RMS_COLOR.green/65535.f,CONFIG_FG_RMS_COLOR.blue/65535.f, CONFIG_FG_RMS_ALPHA/65535.f },
        /*background*/  { CONFIG_BG_COLOR.red/65535.f,CONFIG_BG_COLOR.green/65535.f,CONFIG_BG_COLOR.blue/65535.f, CONFIG_BG_ALPHA/65535.f },
        /*annotation*/  { 1.0, 1.0, 1.0, 1.0 },
        /*border-bg*/   { 0.0, 0.0, 0.0, 0.7 },
        /*center-line*/ { CONFIG_FG_COLOR.red/65535.f,CONFIG_FG_COLOR.green/65535.f,CONFIG_FG_COLOR.blue/65535.f, 0.6 },
        /*border-width*/ 2.0f,
    };

    w->rendering = 1;
    if (cairo_image_surface_get_width (w->surf) != a.width || cairo_image_surface_get_height (w->surf) != a.height) {
        if (w->surf) {
            cairo_surface_destroy (w->surf);
            w->surf = NULL;
        }
        w->surf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, a.width, a.height);
    }
    w->rendering = 0;

    cairo_surface_flush (w->surf);
    cairo_t *temp_cr = cairo_create (w->surf);
    cairo_t *max_cr = cairo_create (w->surf);
    cairo_t *min_cr = cairo_create (w->surf);
    cairo_t *rms_max_cr = cairo_create (w->surf);
    cairo_t *rms_min_cr = cairo_create (w->surf);

    cairo_set_line_width (temp_cr, render.border_width);
    cairo_rectangle (temp_cr, left, 0, a.width, a.height);
    cairo_stroke_preserve (temp_cr);
    cairo_set_source_rgba (temp_cr,CONFIG_BG_COLOR.red/65535.f,CONFIG_BG_COLOR.green/65535.f,CONFIG_BG_COLOR.blue/65535.f,1);
    cairo_fill (temp_cr);

    cairo_set_line_width (temp_cr, BORDER_LINE_WIDTH);
    cairo_set_line_width (max_cr, BORDER_LINE_WIDTH);
    cairo_set_line_width (min_cr, BORDER_LINE_WIDTH);
    cairo_set_line_width (rms_max_cr, BORDER_LINE_WIDTH);
    cairo_set_line_width (rms_min_cr, BORDER_LINE_WIDTH);

    int channels = w->channels;
    int frames_size = VALUES_PER_FRAME * channels;
    float frames_per_x;

    if (channels != 0) {
        frames_per_x = w->buffer_len / (float)(width * frames_size);
        height /= channels;
        top /= channels;
    }
    else {
        frames_per_x = w->buffer_len / (float)(width * VALUES_PER_FRAME);
    }
    int max_frames_per_x = 1 + ceilf (frames_per_x);
    int frames_per_buf = floorf (frames_per_x * (float)(frames_size));

    float x_off;
    if (CONFIG_RENDER_METHOD == BARS) {
        x_off = 0.0;
        cairo_set_line_width (temp_cr, 1);
        cairo_set_antialias (temp_cr, CAIRO_ANTIALIAS_NONE);
    }
    else {
        x_off = 0.5;
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

    int frames_per_buf_temp = frames_per_buf;
    int offset;
    int f_offset;
    float min, max, rms;

    deadbeef->mutex_lock (w->mutex);
    for (int ch = 0; ch < channels; ch++, top += (a.height / channels)) {
        f_offset = 0;
        offset = ch * VALUES_PER_FRAME;
        frames_per_buf = frames_per_buf_temp;

        if (CONFIG_RENDER_METHOD == SPIKES) {
            cairo_move_to (max_cr, 0, top + height/2);
            cairo_move_to (min_cr, 0, top + height/2);
            cairo_move_to (rms_max_cr, 0, top + height/2);
            cairo_move_to (rms_min_cr, 0, top + height/2);
        }

        for (int x = 0; x < width; x++) {
            if (offset + frames_per_buf > w->buffer_len) {
                break;
            }
            double yoff;

            if (w->read) {
                min = 1.0; max = -1.0; rms = 0.0;
            }
            else {
                min = 0.0; max = 0.0; rms = 0.0;
            }

            int offset_temp = offset;
            for (int j = offset; j < offset + frames_per_buf; j = j + frames_size) {
                max = MAX (max, w->buffer[j]);
                min = MIN (min, w->buffer[j+1]);
                rms = (rms + w->buffer[j+2])/2;
                offset_temp += frames_size;
            }
            offset = offset_temp;

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

            /* Draw Foreground - line */
            if (CONFIG_RENDER_METHOD == SPIKES) {
                DRECT pts0 = { left + x - x_off, top + yoff - pmin, left + x + x_off, top + yoff - min };
                draw_cairo_line_path (min_cr, &pts0, &render.c_fg);
                if (!render.rectified) {
                    DRECT pts1 = { left + x - x_off, top + yoff - pmax, left + x + x_off, top + yoff - max };
                    draw_cairo_line_path (max_cr, &pts1, &render.c_fg);
                }
            }
            else if (CONFIG_RENDER_METHOD == BARS) {
                DRECT pts0 = { left + x - x_off, top + yoff - pmin, left + x + x_off, top + yoff - min };
                draw_cairo_line (temp_cr, &pts0, &render.c_fg);
                if (!render.rectified) {
                    DRECT pts1 = { left + x - x_off, top + yoff - pmax, left + x + x_off, top + yoff - max };
                    draw_cairo_line (temp_cr, &pts1, &render.c_fg);
                }
            }

            if (CONFIG_DISPLAY_RMS && CONFIG_RENDER_METHOD == SPIKES) {
                DRECT pts0 = { left + x - x_off, top + yoff - prms, left + x + x_off, top + yoff - rms };
                draw_cairo_line_path (rms_min_cr, &pts0, &render.c_rms);

                if (!render.rectified) {
                    DRECT pts1 = { left + x - x_off, top + yoff + prms, left + x + x_off, top + yoff + rms };
                    draw_cairo_line_path (rms_max_cr, &pts1, &render.c_rms);
                }
            }
            else if (CONFIG_DISPLAY_RMS && CONFIG_RENDER_METHOD == BARS) {
                DRECT pts0 = { left + x - x_off, top + yoff - prms, left + x + x_off, top + yoff - rms };
                draw_cairo_line (temp_cr, &pts0, &render.c_rms);

                if (!render.rectified) {
                    DRECT pts1 = { left + x - x_off, top + yoff + prms, left + x + x_off, top + yoff + rms };
                    draw_cairo_line (temp_cr, &pts1, &render.c_rms);
                }
            }

            // /* Draw background - box */
            // if (FALSE) {
            //     if (render.rectified) {
            //         DRECT pts2 = { left + x, top + yoff - MIN (min, pmin), left + x, top + yoff };
            //         draw_cairo_line (temp_cr, &pts2, &render.c_fg);
            //     }
            //     else {
            //         DRECT pts2 = { left + x, top + yoff - MAX (pmin, min), left + x, top + yoff - MIN (pmax, max) };
            //         draw_cairo_line (temp_cr, &pts2, &render.c_fg);
            //     }
            // }

            // if (FALSE) {
            //     if (render.rectified) {
            //         DRECT pts2 = { left + x, top + yoff - MIN (prms, rms), left + x, top + yoff };
            //         draw_cairo_line (temp_cr, &pts2, &render.c_rms);
            //     }
            //     else {
            //         DRECT pts2 = { left + x, top + yoff - MIN (prms, rms), left + x, top + yoff + MIN (prms, rms) };
            //         draw_cairo_line (temp_cr, &pts2, &render.c_rms);
            //     }
            //}

            if (CONFIG_RENDER_METHOD == BARS) {
                pmin = 0;
                pmax = 0;
                prms = 0;
            }
            else {
                pmin = min;
                pmax = max;
                prms = rms;
            }
            f_offset += frames_per_buf;
            frames_per_buf = floorf ((x + 1) * frames_per_x * frames_size) - f_offset;
            frames_per_buf = frames_per_buf > (max_frames_per_x * frames_size) ? (max_frames_per_x * frames_size) : frames_per_buf;
            frames_per_buf = frames_per_buf + ((frames_size) - (frames_per_buf % frames_size));
        }
        if (CONFIG_RENDER_METHOD == SPIKES) {
            cairo_line_to (max_cr, a.width, top + height/2);
            cairo_line_to (min_cr, a.width, top + height/2);
            cairo_line_to (rms_max_cr, a.width, top + height/2);
            cairo_line_to (rms_min_cr, a.width, top + height/2);
            cairo_close_path (max_cr);
            cairo_close_path (min_cr);
            cairo_close_path (rms_max_cr);
            cairo_close_path (rms_min_cr);
            cairo_fill (max_cr);
            cairo_fill (min_cr);
            cairo_fill (rms_max_cr);
            cairo_fill (rms_min_cr);
        }
        // center line
        if (!render.rectified) {
            DRECT pts = { left, top + (0.5 * height), left + width, top + (0.5 * height) };
            draw_cairo_line (temp_cr, &pts, &render.c_cl);
        }
    }
    deadbeef->mutex_unlock (w->mutex);
    cairo_destroy (temp_cr);
    cairo_destroy (max_cr);
    cairo_destroy (min_cr);
    cairo_destroy (rms_max_cr);
    cairo_destroy (rms_min_cr);
    return;
}

void
waveform_db_cache (gpointer user_data, char const *uri)
{
    w_waveform_t *w = user_data;
    deadbeef->mutex_lock (w->mutex);
    waveform_db_open (cache_path, cache_path_size);
    waveform_db_init (uri);
    waveform_db_write (uri, w->buffer, w->buffer_len * sizeof(float), w->channels, 0);
    waveform_db_close ();
    deadbeef->mutex_unlock (w->mutex);
}

gboolean
waveform_generate_wavedata (gpointer user_data)
{
    w_waveform_t *w = user_data;
    double width = MAX_VALUES_PER_CHANNEL;
    long buffer_len;

    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    DB_fileinfo_t *fileinfo = NULL;
    if (it) {
        char const *uri = deadbeef->pl_find_meta_raw (it, ":URI");
        gboolean cache_enabled = CONFIG_CACHE_ENABLED;
        if (cache_enabled) {
            deadbeef->mutex_lock (w->mutex);
            waveform_db_open (cache_path, cache_path_size);
            w->buffer_len = waveform_db_read (uri, w->buffer, w->max_buffer_len, &w->channels);
            if ( w->buffer_len > 0 ) {
                w->read = 1;
                deadbeef->pl_item_unref (it);
                waveform_db_close ();
                deadbeef->mutex_unlock (w->mutex);
                return TRUE;
            }
            waveform_db_close ();
            deadbeef->mutex_unlock (w->mutex);
        }
        if (deadbeef->pl_get_item_duration (it)/60 >= CONFIG_MAX_FILE_LENGTH && CONFIG_MAX_FILE_LENGTH != -1) {
            deadbeef->pl_item_unref (it);
            deadbeef->mutex_lock (w->mutex);
            memset (w->buffer, 0, w->max_buffer_len);
            w->buffer_len = 0;
            deadbeef->mutex_unlock (w->mutex);
            printf ("waveform: file too long.\n");
            return TRUE;
        }

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
                w->channels = fileinfo->fmt.channels;
                int nframes_per_channel = (int)deadbeef->pl_get_item_duration(it) * fileinfo->fmt.samplerate;
                const long frames_per_buf = floorf((float) nframes_per_channel / (float) width);
                const long max_frames_per_buf = 1 + frames_per_buf;

                deadbeef->mutex_lock (w->mutex);
                data = malloc (sizeof (float) * max_frames_per_buf * fileinfo->fmt.channels);
                if (!data) {
                    printf ("out of memory.\n");
                    deadbeef->pl_item_unref (it);
                    deadbeef->mutex_unlock (w->mutex);
                    return FALSE;
                }
                memset (data, 0, sizeof (float) * max_frames_per_buf * fileinfo->fmt.channels);
                deadbeef->mutex_unlock (w->mutex);

                deadbeef->mutex_lock (w->mutex);
                buffer = malloc (sizeof (float) * max_frames_per_buf * fileinfo->fmt.channels);
                if (!buffer) {
                    printf ("out of memory.\n");
                    deadbeef->pl_item_unref (it);
                    deadbeef->mutex_unlock (w->mutex);
                    return FALSE;
                }
                memset (buffer, 0, sizeof (float) * max_frames_per_buf * fileinfo->fmt.channels);
                deadbeef->mutex_unlock (w->mutex);

                int channels = (!CONFIG_MIX_TO_MONO) ? 1 : fileinfo->fmt.channels;
                buffer_len = frames_per_buf * fileinfo->fmt.channels;

                ddb_waveformat_t out_fmt = {
                    .bps = 32,
                    .channels = fileinfo->fmt.channels,
                    .samplerate = fileinfo->fmt.samplerate,
                    .channelmask = fileinfo->fmt.channelmask,
                    .is_float = 1,
                    .is_bigendian = 0
                };

                if (buffer_len % (2*fileinfo->fmt.channels) != 0) {
                    buffer_len -= fileinfo->fmt.channels;
                }

                int eof = 0;
                int counter = 0;
                while (!eof) {
                    //ugly hack
                    //buffer_len = buffer_len + ((fileinfo->fmt.channels * 2) -(buffer_len % (fileinfo->fmt.channels * 2)));

                    int sz = dec->read (fileinfo, (char *)buffer, buffer_len);
                    if (sz != buffer_len) {
                        eof = 1;
                    } else if (sz == 0) {
                        break;
                    }

                    deadbeef->pcm_convert (&fileinfo->fmt, (char *)buffer, &out_fmt, (char *)data, sz);

                    int frame;
                    float min, max, rms;
                    int ch;

                    for (ch = 0; ch < fileinfo->fmt.channels; ch++) {
                        if (counter >= w->max_buffer_len) {
                            break;
                        }
                        min = 1.0; max = -1.0; rms = 0.0;
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
                        deadbeef->mutex_lock (w->mutex);
                        w->buffer[counter] = max;
                        w->buffer[counter+1] = min;
                        w->buffer[counter+2] = rms;
                        deadbeef->mutex_unlock (w->mutex);
                        counter += 3;
                    }
                }
                w->buffer_len = counter;
                if (cache_enabled) {
                    waveform_db_cache (w, uri);
                }
                w->read = 1;
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
    waveform_generate_wavedata (w);
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
    w->resizetimer = g_timeout_add (500, waveform_redraw_thread, w);

    // int fps = deadbeef->conf_get_int ("gtkui.refresh_rate", 10);
    // if (fps < 1) {
    //     fps = 1;
    // }
    // else if (fps > 30) {
    //     fps = 30;
    // }

    // int tm = 1000/fps;

    // if (w->drawtimer) {
    //     g_source_remove (w->drawtimer);
    //     w->drawtimer = 0;
    // }
    // w->drawtimer = g_timeout_add (tm, w_waveform_draw_cb, w);

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
    if (event->button == 3) {
      return TRUE;
    }
    w->seekbar_moving = 1;
    w->seekbar_moved = 0.0;
    GtkAllocation a;
    gtk_widget_get_allocation (widget, &a);
    w->seekbar_move_x = event->x;
    w->seekbar_move_x_clicked = event->x;
    return TRUE;
}

gboolean
waveform_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    if (event->button == 3) {
      gtk_menu_popup (GTK_MENU (w->popup), NULL, NULL, NULL, w->drawarea, 0, gtk_get_current_event_time());
      return TRUE;
    }
    w->seekbar_moving = 0;
    w->seekbar_move_x_clicked = 0;
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
        w->read = 0;
        tid = deadbeef->thread_start_low_priority (waveform_get_wavedata, w);
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

void
w_waveform_init (ddb_gtkui_widget_t *w)
{
    w_waveform_t *wf = (w_waveform_t *)w;
    GtkAllocation a;
    gtk_widget_get_allocation (wf->drawarea, &a);
    wf->max_buffer_len = VALUES_PER_FRAME * sizeof(float) * MAX_VALUES_PER_CHANNEL * 4;
    deadbeef->mutex_lock (wf->mutex);
    wf->buffer = malloc (sizeof(float) * wf->max_buffer_len);
    memset (wf->buffer, 0, sizeof(float) * wf->max_buffer_len);
    wf->surf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, a.width, a.height);
    deadbeef->mutex_unlock (wf->mutex);
    wf->rendering = 0;
    wf->seekbar_moving = 0;
    wf->seekbar_moved = 0;
    wf->height = a.height;
    wf->width = a.width;

    cache_path_size = make_cache_dir (cache_path, sizeof(cache_path));

    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    if (it) {
        intptr_t tid = deadbeef->thread_start_low_priority (waveform_get_wavedata, w);
        deadbeef->thread_detach (tid);
        deadbeef->pl_item_unref (it);
    }
    if (wf->resizetimer) {
        g_source_remove (wf->resizetimer);
        wf->resizetimer = 0;
    }

    int fps = deadbeef->conf_get_int ("gtkui.refresh_rate", 10);
    if (fps < 1) {
        fps = 1;
    }
    else if (fps > 30) {
        fps = 30;
    }

    int tm = 1000/fps;

    if (wf->drawtimer) {
        g_source_remove (wf->drawtimer);
        wf->drawtimer = 0;
    }
    wf->drawtimer = g_timeout_add (tm, w_waveform_draw_cb, w);
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
    //gtk_container_add (GTK_CONTAINER (w->drawarea), w->popup);
    gtk_widget_show (w->popup_item);
    gtk_container_add (GTK_CONTAINER (w->popup), w->popup_item);

#if !GTK_CHECK_VERSION(3,0,0)
    g_signal_connect_after ((gpointer) w->drawarea, "expose_event", G_CALLBACK (waveform_expose_event), w);
#else
    g_signal_connect_after ((gpointer) w->drawarea, "draw", G_CALLBACK (waveform_expose_event), w);
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
            //printf ("fb api2\n");
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

static const char settings_dlg[] =
    "property \"Ignore files longer than x minutes "
                "(-1 scans every file): \"          spinbtn[-1,9999,1] "     CONFSTR_WF_MAX_FILE_LENGTH        " 180 ;\n"
    "property \"Enable cache (experimental) \"      checkbox "               CONFSTR_WF_CACHE_ENABLED          " 0 ;\n"
;

static DB_misc_t plugin = {
    //DB_PLUGIN_SET_API_VERSION
    .plugin.type            = DB_PLUGIN_MISC,
    .plugin.api_vmajor      = 1,
    .plugin.api_vminor      = 0,
    .plugin.version_major   = 0,
    .plugin.version_minor   = 1,
#if GTK_CHECK_VERSION(3,0,0)
    .plugin.id              = "waveform_seekbar-gtk3",
#else
    .plugin.id              = "waveform_seekbar",
#endif
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
    .plugin.website         = "https://github.com/cboxdoerfer/ddb_waveform_seekbar",
    .plugin.start           = waveform_start,
    .plugin.stop            = waveform_stop,
    .plugin.connect         = waveform_connect,
    .plugin.disconnect      = waveform_disconnect,
    .plugin.configdialog    = settings_dlg,
};

#if !GTK_CHECK_VERSION(3,0,0)
DB_plugin_t *
ddb_misc_waveform_GTK2_load (DB_functions_t *ddb) {
    deadbeef = ddb;
    return &plugin.plugin;
}
#else
DB_plugin_t *
ddb_misc_waveform_GTK3_load (DB_functions_t *ddb) {
    deadbeef = ddb;
    return &plugin.plugin;
}
#endif
