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
#include <string.h>
#include <assert.h>
#include <math.h>
#include <fcntl.h>
#include <gtk/gtk.h>

#include <deadbeef/deadbeef.h>
#include <deadbeef/gtkui_api.h>
#include "cache.h"

#define C_COLOUR(X) (X)->r, (X)->g, (X)->b, (X)->a

//#define M_PI (3.1415926535897932384626433832795029)
#define LINE_WIDTH   (1.0)
#define BORDER_WIDTH (1)
// min, max, rms
#define VALUES_PER_SAMPLE (3)
#define MAX_CHANNELS (6)
#define MAX_SAMPLES (4096)

#define     CONFSTR_WF_LOG_ENABLED       "waveform.log_enabled"
#define     CONFSTR_WF_MIX_TO_MONO       "waveform.mix_to_mono"
#define     CONFSTR_WF_DISPLAY_RMS       "waveform.display_rms"
#define     CONFSTR_WF_RENDER_METHOD     "waveform.render_method"
#define     CONFSTR_WF_SOUNDCLOUD_STYLE  "waveform.soundcloud_style"
#define     CONFSTR_WF_SHADE_WAVEFORM    "waveform.shade_waveform"
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

#define     CONFSTR_WF_REFRESH_INTERVAL  "waveform.refresh_interval"
#define     CONFSTR_WF_BORDER_WIDTH      "waveform.border_width"
#define     CONFSTR_WF_CURSOR_WIDTH      "waveform.cursor_width"
#define     CONFSTR_WF_FONT_SIZE         "waveform.font_size"
#define     CONFSTR_WF_MAX_FILE_LENGTH   "waveform.max_file_length"
#define     CONFSTR_WF_CACHE_ENABLED     "waveform.cache_enabled"
#define     CONFSTR_WF_SCROLL_ENABLED    "waveform.scroll_enabled"
#define     CONFSTR_WF_NUM_SAMPLES       "waveform.num_samples"


/* Global variables */
static DB_misc_t            plugin;
static DB_functions_t *     deadbeef = NULL;
static ddb_gtkui_t *        gtkui_plugin = NULL;

static char cache_path[PATH_MAX];
static int cache_path_size;

typedef struct COLOUR
{
    double r;
    double g;
    double b;
    double a;
} COLOUR;

typedef struct
{
    COLOUR c_fg, c_rms, c_bg;
} RENDER;

typedef struct
{
    ddb_gtkui_widget_t base;
    GtkWidget *popup;
    GtkWidget *popup_item;
    GtkWidget *drawarea;
    GtkWidget *frame;
    guint drawtimer;
    guint resizetimer;
    short *buffer;
    size_t max_buffer_len;
    size_t buffer_len;
    int channels;
    int nsamples;
    int seekbar_moving;
    float seekbar_moved;
    float seekbar_move_x;
    float seekbar_move_x_clicked;
    float height;
    float width;
    intptr_t mutex;
    intptr_t mutex_rendering;
    cairo_surface_t *surf;
    cairo_surface_t *surf_shaded;
} w_waveform_t;

typedef struct cache_query_s
{
    char *fname;
    struct cache_query_s *next;
} cache_query_t;

typedef struct DRECT
{
    double x1, y1;
    double x2, y2;
} DRECT;

enum STYLE { BARS = 1, SPIKES = 2 };

static cache_query_t *queue;
static cache_query_t *queue_tail;
static uintptr_t mutex;

static gboolean CONFIG_LOG_ENABLED = FALSE;
static gboolean CONFIG_MIX_TO_MONO = FALSE;
static gboolean CONFIG_CACHE_ENABLED = TRUE;
static gboolean CONFIG_SCROLL_ENABLED = TRUE;
static gboolean CONFIG_DISPLAY_RMS = TRUE;
static gboolean CONFIG_SHADE_WAVEFORM = FALSE;
static gboolean CONFIG_SOUNDCLOUD_STYLE = FALSE;
static GdkColor CONFIG_BG_COLOR;
static GdkColor CONFIG_FG_COLOR;
static GdkColor CONFIG_PB_COLOR;
static GdkColor CONFIG_FG_RMS_COLOR;
static guint16  CONFIG_BG_ALPHA;
static guint16  CONFIG_FG_ALPHA;
static guint16  CONFIG_PB_ALPHA;
static guint16  CONFIG_FG_RMS_ALPHA;
static gint     CONFIG_RENDER_METHOD = SPIKES;
static gint     CONFIG_BORDER_WIDTH = 1;
static gint     CONFIG_CURSOR_WIDTH = 3;
static gint     CONFIG_FONT_SIZE = 18;
static gint     CONFIG_MAX_FILE_LENGTH = 180;
static gint     CONFIG_NUM_SAMPLES = 2048;
static gint     CONFIG_REFRESH_INTERVAL = 33;

static RENDER render;

static gboolean
waveform_draw_cb (void *user_data);

static gboolean
waveform_redraw_cb (void *user_data);

void
waveform_draw (void *user_data, int shaded);

static void
save_config (void)
{
    deadbeef->conf_set_int (CONFSTR_WF_LOG_ENABLED,         CONFIG_LOG_ENABLED);
    deadbeef->conf_set_int (CONFSTR_WF_MIX_TO_MONO,         CONFIG_MIX_TO_MONO);
    deadbeef->conf_set_int (CONFSTR_WF_DISPLAY_RMS,         CONFIG_DISPLAY_RMS);
    deadbeef->conf_set_int (CONFSTR_WF_SHADE_WAVEFORM,      CONFIG_SHADE_WAVEFORM);
    deadbeef->conf_set_int (CONFSTR_WF_SOUNDCLOUD_STYLE,    CONFIG_SOUNDCLOUD_STYLE);
    deadbeef->conf_set_int (CONFSTR_WF_RENDER_METHOD,       CONFIG_RENDER_METHOD);
    deadbeef->conf_set_int (CONFSTR_WF_BORDER_WIDTH,        CONFIG_BORDER_WIDTH);
    deadbeef->conf_set_int (CONFSTR_WF_CURSOR_WIDTH,        CONFIG_CURSOR_WIDTH);
    deadbeef->conf_set_int (CONFSTR_WF_FONT_SIZE,           CONFIG_FONT_SIZE);
    deadbeef->conf_set_int (CONFSTR_WF_MAX_FILE_LENGTH,     CONFIG_MAX_FILE_LENGTH);
    deadbeef->conf_set_int (CONFSTR_WF_REFRESH_INTERVAL,    CONFIG_REFRESH_INTERVAL);
    deadbeef->conf_set_int (CONFSTR_WF_NUM_SAMPLES,         CONFIG_NUM_SAMPLES);
    deadbeef->conf_set_int (CONFSTR_WF_CACHE_ENABLED,       CONFIG_CACHE_ENABLED);
    deadbeef->conf_set_int (CONFSTR_WF_SCROLL_ENABLED,      CONFIG_SCROLL_ENABLED);
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
    CONFIG_SHADE_WAVEFORM = deadbeef->conf_get_int (CONFSTR_WF_SHADE_WAVEFORM,       FALSE);
    CONFIG_SOUNDCLOUD_STYLE = deadbeef->conf_get_int (CONFSTR_WF_SOUNDCLOUD_STYLE,   FALSE);
    CONFIG_RENDER_METHOD = deadbeef->conf_get_int (CONFSTR_WF_RENDER_METHOD,        SPIKES);
    CONFIG_BORDER_WIDTH = deadbeef->conf_get_int (CONFSTR_WF_BORDER_WIDTH,               1);
    CONFIG_CURSOR_WIDTH = deadbeef->conf_get_int (CONFSTR_WF_CURSOR_WIDTH,               3);
    CONFIG_FONT_SIZE = deadbeef->conf_get_int (CONFSTR_WF_FONT_SIZE,                    18);
    CONFIG_REFRESH_INTERVAL = deadbeef->conf_get_int (CONFSTR_WF_REFRESH_INTERVAL,      33);
    CONFIG_MAX_FILE_LENGTH = deadbeef->conf_get_int (CONFSTR_WF_MAX_FILE_LENGTH,       180);
    CONFIG_NUM_SAMPLES = deadbeef->conf_get_int (CONFSTR_WF_NUM_SAMPLES,              2048);
    CONFIG_CACHE_ENABLED = deadbeef->conf_get_int (CONFSTR_WF_CACHE_ENABLED,          TRUE);
    CONFIG_SCROLL_ENABLED = deadbeef->conf_get_int (CONFSTR_WF_SCROLL_ENABLED,        TRUE);

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

    render =
    (RENDER) {
        /*foreground*/  { CONFIG_FG_COLOR.red/65535.f,CONFIG_FG_COLOR.green/65535.f,CONFIG_FG_COLOR.blue/65535.f, CONFIG_FG_ALPHA/65535.f },
        /*wave-rms*/    { CONFIG_FG_RMS_COLOR.red/65535.f,CONFIG_FG_RMS_COLOR.green/65535.f,CONFIG_FG_RMS_COLOR.blue/65535.f, CONFIG_FG_RMS_ALPHA/65535.f },
        /*background*/  { CONFIG_BG_COLOR.red/65535.f,CONFIG_BG_COLOR.green/65535.f,CONFIG_BG_COLOR.blue/65535.f, CONFIG_BG_ALPHA/65535.f },
    };
}

static int
on_config_changed (void *widget)
{
    w_waveform_t *w = (w_waveform_t *) widget;
    load_config ();
    // enable/disable border
    switch (CONFIG_BORDER_WIDTH) {
        case 0:
            gtk_frame_set_shadow_type ((GtkFrame *)w->frame, GTK_SHADOW_NONE);
            break;
        case 1:
            gtk_frame_set_shadow_type ((GtkFrame *)w->frame, GTK_SHADOW_IN);
            break;
    }

    if (w->drawtimer) {
        g_source_remove (w->drawtimer);
        w->drawtimer = 0;
    }
    w->drawtimer = g_timeout_add (CONFIG_REFRESH_INTERVAL, waveform_draw_cb, w);
    g_idle_add (waveform_redraw_cb, w);
    return 0;
}

#if !GTK_CHECK_VERSION(2,14,0)
#define gtk_widget_get_window(widget) ((widget)->window)
#define gtk_dialog_get_content_area(dialog) (dialog->vbox)
#define gtk_dialog_get_action_area(dialog) (dialog->action_area)
#endif

#if !GTK_CHECK_VERSION(2,18,0)
void
gtk_widget_get_allocation (GtkWidget *widget, GtkAllocation *allocation) {
    (allocation)->x = widget->allocation.x;
    (allocation)->y = widget->allocation.y;
    (allocation)->width = widget->allocation.width;
    (allocation)->height = widget->allocation.height;
}
#define gtk_widget_set_can_default(widget, candefault) {if (candefault) GTK_WIDGET_SET_FLAGS (widget, GTK_CAN_DEFAULT); else GTK_WIDGET_UNSET_FLAGS(widget, GTK_CAN_DEFAULT);}
#endif

static void
on_button_config (GtkMenuItem *menuitem, gpointer user_data)
{
    GtkWidget *waveform_properties;
    GtkWidget *config_dialog;
    GtkWidget *vbox01;
    GtkWidget *color_label;
    GtkWidget *color_frame;
    GtkWidget *color_table;
    GtkWidget *color_background_label;
    GtkWidget *background_color;
    GtkWidget *color_waveform_label;
    GtkWidget *foreground_color;
    GtkWidget *color_rms_label;
    GtkWidget *foreground_rms_color;
    GtkWidget *color_progressbar_label;
    GtkWidget *progressbar_color;
    GtkWidget *downmix_to_mono;
    GtkWidget *log_scale;
    GtkWidget *display_rms;
    GtkWidget *style_label;
    GtkWidget *style_frame;
    GtkWidget *vbox02;
    GtkWidget *render_method_spikes;
    GtkWidget *render_method_bars;
    GtkWidget *shade_waveform;
    GtkWidget *soundcloud_style;
    GtkWidget *dialog_action_area13;
    GtkWidget *applybutton1;
    GtkWidget *cancelbutton1;
    GtkWidget *okbutton1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    waveform_properties = gtk_dialog_new ();
    gtk_window_set_title (GTK_WINDOW (waveform_properties), "Waveform Properties");
    gtk_window_set_type_hint (GTK_WINDOW (waveform_properties), GDK_WINDOW_TYPE_HINT_DIALOG);

    config_dialog = gtk_dialog_get_content_area (GTK_DIALOG (waveform_properties));
    gtk_widget_show (config_dialog);

    vbox01 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox01);
    gtk_box_pack_start (GTK_BOX (config_dialog), vbox01, FALSE, FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (vbox01), 12);

    color_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (color_label),"<b>Colors</b>");
    gtk_widget_show (color_label);

    color_frame = gtk_frame_new ("Colors");
    gtk_frame_set_label_widget ((GtkFrame *)color_frame, color_label);
    gtk_frame_set_shadow_type ((GtkFrame *)color_frame, GTK_SHADOW_IN);
    gtk_widget_show (color_frame);
    gtk_box_pack_start (GTK_BOX (vbox01), color_frame, TRUE, FALSE, 0);

    color_table = gtk_table_new (2, 4, TRUE);
    gtk_widget_show (color_table);
    gtk_container_add (GTK_CONTAINER (color_frame), color_table);
    gtk_table_set_col_spacings ((GtkTable *) color_table, 8);
    gtk_container_set_border_width (GTK_CONTAINER (color_table), 6);

    color_background_label = gtk_label_new ("Background");
    gtk_widget_show (color_background_label);
    gtk_table_attach_defaults ((GtkTable *) color_table, color_background_label, 0,1,0,1);

    color_waveform_label = gtk_label_new ("Waveform");
    gtk_widget_show (color_waveform_label);
    gtk_table_attach_defaults ((GtkTable *) color_table, color_waveform_label, 1,2,0,1);

    color_rms_label = gtk_label_new ("RMS");
    gtk_widget_show (color_rms_label);
    gtk_table_attach_defaults ((GtkTable *) color_table, color_rms_label, 2,3,0,1);

    color_progressbar_label = gtk_label_new ("Progressbar");
    gtk_widget_show (color_progressbar_label);
    gtk_table_attach_defaults ((GtkTable *) color_table, color_progressbar_label, 3,4,0,1);

    background_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)background_color, TRUE);
    gtk_widget_show (background_color);
    gtk_table_attach_defaults ((GtkTable *) color_table, background_color, 0,1,1,2);

    foreground_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)foreground_color, TRUE);
    gtk_widget_show (foreground_color);
    gtk_table_attach_defaults ((GtkTable *) color_table, foreground_color, 1,2,1,2);

    foreground_rms_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)foreground_rms_color, TRUE);
    gtk_widget_show (foreground_rms_color);
    gtk_table_attach_defaults ((GtkTable *) color_table, foreground_rms_color, 2,3,1,2);

    progressbar_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)progressbar_color, TRUE);
    gtk_widget_show (progressbar_color);
    gtk_table_attach_defaults ((GtkTable *) color_table, progressbar_color, 3,4,1,2);

    style_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (style_label),"<b>Style</b>");
    gtk_widget_show (style_label);

    style_frame = gtk_frame_new ("Style");
    gtk_frame_set_label_widget ((GtkFrame *)style_frame, style_label);
    gtk_frame_set_shadow_type ((GtkFrame *)style_frame, GTK_SHADOW_IN);
    gtk_widget_show (style_frame);
    gtk_box_pack_start (GTK_BOX (vbox01), style_frame, FALSE, FALSE, 0);

    vbox02 = gtk_vbox_new (FALSE, 6);
    gtk_widget_show (vbox02);
    gtk_container_add (GTK_CONTAINER (style_frame), vbox02);

    render_method_spikes = gtk_radio_button_new_with_label (NULL, "Spikes");
    gtk_widget_show (render_method_spikes);
    gtk_box_pack_start (GTK_BOX (vbox02), render_method_spikes, TRUE, TRUE, 0);

    render_method_bars = gtk_radio_button_new_with_label_from_widget ((GtkRadioButton *)render_method_spikes, "Bars");
    gtk_widget_show (render_method_bars);
    gtk_box_pack_start (GTK_BOX (vbox02), render_method_bars, TRUE, TRUE, 0);

    soundcloud_style = gtk_check_button_new_with_label ("Soundcloud style");
    gtk_widget_show (soundcloud_style);
    gtk_box_pack_start (GTK_BOX (vbox02), soundcloud_style, TRUE, TRUE, 0);

    shade_waveform = gtk_check_button_new_with_label ("Shade waveform");
    gtk_widget_show (shade_waveform);
    gtk_box_pack_start (GTK_BOX (vbox02), shade_waveform, TRUE, TRUE, 0);

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
    gtk_widget_set_can_default (applybutton1, TRUE);

    cancelbutton1 = gtk_button_new_from_stock ("gtk-cancel");
    gtk_widget_show (cancelbutton1);
    gtk_dialog_add_action_widget (GTK_DIALOG (waveform_properties), cancelbutton1, GTK_RESPONSE_CANCEL);
    gtk_widget_set_can_default (cancelbutton1, TRUE);

    okbutton1 = gtk_button_new_from_stock ("gtk-ok");
    gtk_widget_show (okbutton1);
    gtk_dialog_add_action_widget (GTK_DIALOG (waveform_properties), okbutton1, GTK_RESPONSE_OK);
    gtk_widget_set_can_default (okbutton1, TRUE);

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
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shade_waveform), CONFIG_SHADE_WAVEFORM);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (soundcloud_style), CONFIG_SOUNDCLOUD_STYLE);

    gtk_widget_set_sensitive (display_rms, !CONFIG_SOUNDCLOUD_STYLE);

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
            CONFIG_SHADE_WAVEFORM = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (shade_waveform));
            CONFIG_SOUNDCLOUD_STYLE = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (soundcloud_style));
            gtk_widget_set_sensitive (display_rms, !CONFIG_SOUNDCLOUD_STYLE);
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
#pragma GCC diagnostic pop
    return;
}

void
queue_add (const char *fname)
{
    deadbeef->mutex_lock (mutex);
    for (cache_query_t *q = queue; q; q = q->next) {
        if (!strcmp (fname, q->fname)) {
            // already queued
            deadbeef->mutex_unlock (mutex);
            return;
        }
    }
    cache_query_t *q = malloc (sizeof (cache_query_t));
    memset (q, 0, sizeof (cache_query_t));
    q->fname = strdup (fname);
    if (queue_tail) {
        queue_tail->next = q;
        queue_tail = q;
    }
    else {
        queue = queue_tail = q;
    }
    deadbeef->mutex_unlock (mutex);
}

void
queue_pop (void)
{
    deadbeef->mutex_lock (mutex);
    cache_query_t *next = queue ? queue->next : NULL;
    if (queue) {
        if (queue->fname) {
            free (queue->fname);
        }
        free (queue);
    }
    queue = next;
    if (!queue) {
        queue_tail = NULL;
    }
    deadbeef->mutex_unlock (mutex);
}

static int
check_dir (const char *dir, mode_t mode)
{
    char *tmp = strdup (dir);
    char *slash = tmp;
    struct stat stat_buf;
    do
    {
        slash = strstr (slash+1, "/");
        if (slash)
            *slash = 0;
        if (-1 == stat (tmp, &stat_buf))
        {
            if (0 != mkdir (tmp, mode))
            {
                free (tmp);
                return 0;
            }
        }
        if (slash)
            *slash = '/';
    } while (slash);
    free (tmp);
    return 1;
}

static int
make_cache_dir (char *path, int size)
{
    const char *cache = getenv ("XDG_CACHE_HOME");
    int sz;
    sz = snprintf (path, size, cache ? "%s/deadbeef/waveform/" : "%s/.cache/deadbeef/waveform/", cache ? cache : getenv ("HOME"));
    if (!check_dir (path, 0755)) {
        return 0;
    }
    return sz;
}

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

static void
color_contrast (GdkColor * color)
{
    // Counting the perceptive luminance - human eye favors green color...
    int a = 65535 - ( 2 * color->red + 3 * color->green + color->blue) / 6;
    if (a < 32768)
        a = 0; // bright colors - black font
    else
        a = 65535; // dark colors - white font
    color->red = a;
    color->blue = a;
    color->green = a;
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
    cairo_line_to (cr, pts->x2, pts->y2);
}

static inline void
draw_cairo_line (cairo_t* cr, DRECT *pts, const COLOUR *c)
{
    cairo_move_to (cr, pts->x1, pts->y1);
    cairo_line_to (cr, pts->x2, pts->y2);
    cairo_stroke (cr);
}

static inline void
draw_cairo_rectangle (cairo_t *cr, const GdkColor *c, int alpha, float x, int y, float width, int height)
{
    cairo_set_source_rgba (cr, c->red/65535.f, c->green/65535.f, c->blue/65535.f, alpha/65535.f);
    cairo_rectangle (cr, x, y, width, height);
    cairo_fill (cr);
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
    if (w->surf_shaded) {
        cairo_surface_destroy (w->surf_shaded);
        w->surf_shaded = NULL;
    }
    if (w->buffer) {
        free (w->buffer);
        w->buffer = NULL;
    }
    if (w->mutex) {
        deadbeef->mutex_free (w->mutex);
        w->mutex = 0;
    }
    if (w->mutex_rendering) {
        deadbeef->mutex_free (w->mutex_rendering);
        w->mutex_rendering = 0;
    }
    if (mutex) {
        deadbeef->mutex_free (mutex);
        mutex = 0;
    }
}

static gboolean
waveform_draw_cb (void *user_data)
{
    w_waveform_t *w = user_data;
    gtk_widget_queue_draw (w->drawarea);
    return TRUE;
}

static gboolean
waveform_redraw_cb (void *user_data)
{
    w_waveform_t *w = user_data;
    waveform_draw (w, 0);
    waveform_draw (w, 1);
    gtk_widget_queue_draw (w->drawarea);
    return FALSE;
}

//static gboolean
//waveform_redraw_thread (void *user_data)
//{
//    w_waveform_t *w = user_data;
//    w->resizetimer = 0;
//    intptr_t tid = deadbeef->thread_start_low_priority (waveform_draw, w, 0);
//    deadbeef->thread_detach (tid);
//    gtk_widget_queue_draw (w->drawarea);
//    return FALSE;
//}

void
waveform_seekbar_draw (gpointer user_data, cairo_t *cr, int left, int top, int width, int height)
{
    w_waveform_t *w = user_data;
    int cursor_width = CONFIG_CURSOR_WIDTH;
    float pos = 0;
    float seek_pos = 0;

    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (trk) {
        float dur = deadbeef->pl_get_item_duration (trk);
        pos = (deadbeef->streamer_get_playpos () * width)/ dur + left;

        deadbeef->mutex_lock (w->mutex_rendering);
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
        deadbeef->mutex_unlock (w->mutex_rendering);

        draw_cairo_rectangle (cr, &CONFIG_PB_COLOR, 65535, pos - cursor_width, top, cursor_width, height);

        if (w->seekbar_moving && dur > 0) {
            if (w->seekbar_move_x < left) {
                seek_pos = left;
            }
            else if (w->seekbar_move_x > width + left) {
                seek_pos = width + left;
            }
            else {
                seek_pos = w->seekbar_move_x;
            }

            if (cursor_width == 0) {
                cursor_width = 1;
            }
            draw_cairo_rectangle (cr, &CONFIG_PB_COLOR, 65535, seek_pos - cursor_width, top, cursor_width, height);

            if (w->seekbar_move_x != w->seekbar_move_x_clicked || w->seekbar_move_x_clicked == -1) {
                w->seekbar_move_x_clicked = -1;
                float time = 0;

                if (w->seekbar_moved > 0) {
                    time = deadbeef->streamer_get_playpos ();
                }
                else {
                    time = w->seekbar_move_x * dur / (width);
                }
                time = CLAMP (time, 0, dur);

                char s[1000];
                int hr = time/3600;
                int mn = (time-hr*3600)/60;
                int sc = time-hr*3600-mn*60;
                snprintf (s, sizeof (s), "%02d:%02d:%02d", hr, mn, sc);

                cairo_save (cr);
                cairo_set_source_rgba (cr, CONFIG_PB_COLOR.red/65535.f, CONFIG_PB_COLOR.green/65535.f, CONFIG_PB_COLOR.blue/65535.f, 1);
                cairo_set_font_size (cr, CONFIG_FONT_SIZE);

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

                clearlooks_rounded_rectangle (cr, rec_pos, (height - ex.height - 10)/2, rec_width, rec_height, 3, corners);
                cairo_fill (cr);
                cairo_move_to (cr, text_pos, (height + ex.height)/2);
                GdkColor color_text = CONFIG_PB_COLOR;
                color_contrast (&color_text);
                cairo_set_source_rgba (cr, color_text.red/65535.f, color_text.green/65535.f, color_text.blue/65535.f, 1);
                cairo_show_text (cr, s);
                cairo_restore (cr);
            }
        }
        else if (!deadbeef->is_local_file (deadbeef->pl_find_meta_raw (trk, ":URI"))) {
            const char *text = "Streaming...";
            cairo_save (cr);
            cairo_set_source_rgba (cr, CONFIG_PB_COLOR.red/65535.f, CONFIG_PB_COLOR.green/65535.f, CONFIG_PB_COLOR.blue/65535.f, 1);
            cairo_set_font_size (cr, CONFIG_FONT_SIZE);
            cairo_text_extents_t ex;
            cairo_text_extents (cr, text, &ex);
            int text_x = (width - ex.width)/2;
            int text_y = (height + ex.height - 3)/2;
            cairo_move_to (cr, text_x, text_y);
            GdkColor color_text = CONFIG_BG_COLOR;
            color_contrast (&color_text);
            cairo_set_source_rgba (cr, color_text.red/65535.f, color_text.green/65535.f, color_text.blue/65535.f, 1);
            cairo_show_text (cr, text);
            cairo_restore (cr);
        }
        deadbeef->pl_item_unref (trk);
    }
}

void
waveform_draw (void *user_data, int shaded)
{
    w_waveform_t *w = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);

    double width = a.width;
    double height = a.height;
    double waveform_height = height * 0.9;
    double left = 0;
    double top = (height - waveform_height)/2;
    w->width = width;
    w->height = height;

    float pmin = 0;
    float pmax = 0;
    float prms = 0;

    COLOUR color_fg, color_rms;
    if (CONFIG_SHADE_WAVEFORM == 1 && shaded == 1) {
        color_fg = (COLOUR) { CONFIG_PB_COLOR.red/65535.f,CONFIG_PB_COLOR.green/65535.f,CONFIG_PB_COLOR.blue/65535.f, 1.0};
        color_rms = (COLOUR) { (CONFIG_PB_COLOR.red - 0.2 * CONFIG_PB_COLOR.red)/65535.f,(CONFIG_PB_COLOR.green - 0.2 * CONFIG_PB_COLOR.green)/65535.f,(CONFIG_PB_COLOR.blue - 0.2 * CONFIG_PB_COLOR.blue)/65535.f, 1.0};
    }
    else  {
        color_fg = render.c_fg;
        color_rms = render.c_rms;
    }

    deadbeef->mutex_lock (w->mutex_rendering);
    if (!shaded || !w->surf || cairo_image_surface_get_width (w->surf) != width || cairo_image_surface_get_height (w->surf) != height) {
        if (w->surf) {
            cairo_surface_destroy (w->surf);
            w->surf = NULL;
        }
        w->surf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
    }
    if (shaded || !w->surf_shaded || cairo_image_surface_get_width (w->surf_shaded) != width || cairo_image_surface_get_height (w->surf_shaded) != height) {
        if (w->surf_shaded) {
            cairo_surface_destroy (w->surf_shaded);
            w->surf_shaded = NULL;
        }
        w->surf_shaded = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
    }
    deadbeef->mutex_unlock (w->mutex_rendering);

    cairo_surface_t *surface;
    if (shaded == 0) {
        surface = w->surf;
    }
    else {
        surface = w->surf_shaded;
    }
    cairo_surface_flush (surface);
    cairo_t *cr = cairo_create (surface);
    cairo_t *max_cr = cairo_create (surface);
    cairo_t *min_cr = cairo_create (surface);
    cairo_t *rms_max_cr = cairo_create (surface);
    cairo_t *rms_min_cr = cairo_create (surface);

    // Draw background
    draw_cairo_rectangle (cr, &CONFIG_BG_COLOR, 65535, 0, 0, width, height);

    cairo_set_line_width (cr, LINE_WIDTH);
    cairo_set_line_width (max_cr, LINE_WIDTH);
    cairo_set_line_width (min_cr, LINE_WIDTH);
    cairo_set_line_width (rms_max_cr, LINE_WIDTH);
    cairo_set_line_width (rms_min_cr, LINE_WIDTH);

    if (CONFIG_RENDER_METHOD == BARS) {
        cairo_set_line_width (min_cr, 1);
        cairo_set_antialias (min_cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_line_width (max_cr, 1);
        cairo_set_antialias (max_cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_line_width (rms_min_cr, 1);
        cairo_set_antialias (rms_min_cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_line_width (rms_max_cr, 1);
        cairo_set_antialias (rms_max_cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgba (min_cr, color_fg.r, color_fg.g, color_fg.b, 1);
        cairo_set_source_rgba (max_cr, color_fg.r, color_fg.g, color_fg.b, 1);
        cairo_set_source_rgba (rms_min_cr, color_rms.r, color_rms.g, color_rms.b, 1);
        cairo_set_source_rgba (rms_max_cr, color_rms.r, color_rms.g, color_rms.b, 1);
    }

    float x_off = 0.5;

    int channels = w->channels;
    int samples_size = VALUES_PER_SAMPLE * channels;
    float samples_per_x;

    if (channels != 0) {
        samples_per_x = w->buffer_len / (float)(width * samples_size);
        waveform_height /= channels;
        top /= channels;
    }
    else {
        samples_per_x = w->buffer_len / (float)(width * VALUES_PER_SAMPLE);
    }
    int max_samples_per_x = 1 + ceilf (samples_per_x);
    int samples_per_buf = floorf (samples_per_x * (float)(samples_size));

    if (CONFIG_MIX_TO_MONO) {
        samples_size = VALUES_PER_SAMPLE;
        channels = 1;
        samples_per_x = w->buffer_len / ((float)width * samples_size);
        max_samples_per_x = 1 + ceilf (samples_per_x);
        samples_per_buf = floorf (samples_per_x * (float)(samples_size));
        waveform_height = height * 0.9;
        top = (height - waveform_height)/2;
    }

    int samples_per_buf_temp = samples_per_buf;
    int offset;
    int f_offset;
    float min, max, rms;

    for (int ch = 0; ch < channels; ch++, top += (height / channels)) {
        if (w->channels == 0) {
            break;
        }
        f_offset = 0;
        offset = ch * VALUES_PER_SAMPLE;
        samples_per_buf = samples_per_buf_temp;

        double center;
        if (CONFIG_SOUNDCLOUD_STYLE) {
            center = floor (top + waveform_height * 0.7 + 0.5);
        }
        else {
            center = floor (top + waveform_height/2 + 0.5);
        }

        if (CONFIG_RENDER_METHOD == SPIKES) {
            cairo_move_to (max_cr, left, center);
            cairo_move_to (min_cr, left, center);
            cairo_move_to (rms_max_cr, left, center);
            cairo_move_to (rms_min_cr, left, center);
        }

        cairo_pattern_t *pat;
        cairo_pattern_t *pat1;
        if (CONFIG_SOUNDCLOUD_STYLE) {
            pat = cairo_pattern_create_linear (0, top, 0, center);
            cairo_pattern_add_color_stop_rgba (pat, 1, color_fg.r, color_fg.g, color_fg.b, 1);
            cairo_pattern_add_color_stop_rgba (pat, 0, color_fg.r, color_fg.g, color_fg.b, 0.7);

            pat1 = cairo_pattern_create_linear (0, center, 0, center + 0.3 * waveform_height);
            cairo_pattern_add_color_stop_rgba (pat1, 0, color_fg.r - 0.1 * color_fg.r, color_fg.g - 0.1 * color_fg.g, color_fg.b - 0.1 * color_fg.b, 1);
            cairo_pattern_add_color_stop_rgba (pat1, 0, color_fg.r, color_fg.g, color_fg.b, 0.7);
            if (CONFIG_RENDER_METHOD == BARS) {
                cairo_set_source (min_cr, pat1);
                cairo_set_source (max_cr, pat);
            }
        }

        for (int x = 0; x < width; x++) {
            if (offset + samples_per_buf > w->buffer_len) {
                break;
            }

            min = 0.0; max = 0.0; rms = 0.0;

            int counter = 0;
            int offset_temp = offset;
            for (; offset < offset_temp + samples_per_buf; offset += samples_size, counter++) {
                max += (float)w->buffer[offset]/1000;
                min += (float)w->buffer[offset+1]/1000;
                rms += (float)w->buffer[offset+2]/1000;
            }

            max /= counter;
            min /= counter;
            rms /= counter;

            if (CONFIG_LOG_ENABLED) {
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

            double yoff;
            if (CONFIG_SOUNDCLOUD_STYLE) {
                yoff = 0.7 * waveform_height;
                min = 0.3 * waveform_height * min;
                max = 0.7 * waveform_height * max;
                rms = waveform_height * rms;
            }
            else {
                yoff = 0.5 * waveform_height;
                min = min * yoff;
                max = max * yoff;
                rms = rms * yoff;
            }

            /* Draw Foreground - line */
            if (CONFIG_RENDER_METHOD == SPIKES) {
                DRECT pts0 = { left + x - x_off, top + yoff - pmin, left + x + x_off, top + yoff - min };
                draw_cairo_line_path (min_cr, &pts0, &color_fg);
                DRECT pts1 = { left + x - x_off, top + yoff - pmax, left + x + x_off, top + yoff - max };
                draw_cairo_line_path (max_cr, &pts1, &color_fg);
            }
            else if (CONFIG_RENDER_METHOD == BARS) {
                DRECT pts0 = { left + x, top + yoff, left + x, top + yoff - min };
                draw_cairo_line (min_cr, &pts0, &color_fg);
                DRECT pts1 = { left + x, top + yoff, left + x, top + yoff - max };
                draw_cairo_line (max_cr, &pts1, &color_fg);
            }

            if (!CONFIG_SOUNDCLOUD_STYLE) {
                if (CONFIG_DISPLAY_RMS && CONFIG_RENDER_METHOD == SPIKES) {
                    DRECT pts0 = { left + x - x_off, top + yoff - prms, left + x + x_off, top + yoff - rms };
                    draw_cairo_line_path (rms_min_cr, &pts0, &color_rms);
                    DRECT pts1 = { left + x - x_off, top + yoff + prms, left + x + x_off, top + yoff + rms };
                    draw_cairo_line_path (rms_max_cr, &pts1, &color_rms);
                }
                else if (CONFIG_DISPLAY_RMS && CONFIG_RENDER_METHOD == BARS) {
                    DRECT pts0 = { left + x, top + yoff, left + x, top + yoff - rms };
                    draw_cairo_line (rms_min_cr, &pts0, &color_rms);
                    DRECT pts1 = { left + x, top + yoff, left + x, top + yoff + rms };
                    draw_cairo_line (rms_max_cr, &pts1, &color_rms);
                }
            }

            pmin = min;
            pmax = max;
            prms = rms;

            f_offset += samples_per_buf;
            samples_per_buf = floorf ((x + 1) * samples_per_x * samples_size) - f_offset;
            samples_per_buf = samples_per_buf > (max_samples_per_x * samples_size) ? (max_samples_per_x * samples_size) : samples_per_buf;
            samples_per_buf = samples_per_buf + ((samples_size) - (samples_per_buf % samples_size));
        }
        if (CONFIG_RENDER_METHOD == SPIKES) {
            double right = floor (left + width + 0.5);
            cairo_line_to (max_cr, right, center);
            cairo_line_to (min_cr, right, center);
            cairo_line_to (rms_min_cr, right, center);
            cairo_line_to (rms_max_cr, right, center);
            cairo_close_path (max_cr);
            cairo_close_path (min_cr);
            cairo_close_path (rms_max_cr);
            cairo_close_path (rms_min_cr);
            if (CONFIG_SOUNDCLOUD_STYLE) {
                cairo_set_source (min_cr, pat1);
                cairo_set_source (max_cr, pat);
            }
            cairo_fill (max_cr);
            cairo_fill (min_cr);
            cairo_fill (rms_max_cr);
            cairo_fill (rms_min_cr);
        }

        if (CONFIG_SOUNDCLOUD_STYLE) {
            cairo_pattern_destroy (pat);
            cairo_pattern_destroy (pat1);
        }
    }
    if (!CONFIG_SHADE_WAVEFORM && shaded == 1) {
        draw_cairo_rectangle (cr, &CONFIG_PB_COLOR, CONFIG_PB_ALPHA, 0, 0, width, height);
    }
    cairo_destroy (cr);
    cairo_destroy (max_cr);
    cairo_destroy (min_cr);
    cairo_destroy (rms_max_cr);
    cairo_destroy (rms_min_cr);
    return;
}

void
waveform_scale (void *user_data, cairo_t *cr, int x, int y, int width, int height)
{
    w_waveform_t *w = user_data;

    deadbeef->mutex_lock (w->mutex_rendering);
    if (height != w->height || width != w->width) {
        cairo_save (cr);
        cairo_translate (cr, x, y);
        cairo_scale (cr, width/w->width, height/w->height);
        cairo_set_source_surface (cr, w->surf, x, y);
        cairo_paint (cr);
        cairo_restore (cr);
    }
    else {
        cairo_set_source_surface (cr, w->surf, x, y);
        cairo_paint (cr);
    }
    deadbeef->mutex_unlock (w->mutex_rendering);
}

void
waveform_db_cache (gpointer user_data, const char *uri)
{
    w_waveform_t *w = user_data;
    deadbeef->mutex_lock (w->mutex);
    waveform_db_open (cache_path, cache_path_size);
    waveform_db_init (uri);
    waveform_db_write (uri, w->buffer, w->buffer_len * sizeof (short), w->channels, 0);
    waveform_db_close ();
    deadbeef->mutex_unlock (w->mutex);
}

int
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

gboolean
waveform_generate_wavedata (gpointer user_data, DB_playItem_t *it, const char *uri)
{
    w_waveform_t *w = user_data;
    double width = CONFIG_NUM_SAMPLES;
    long buffer_len;

    deadbeef->mutex_lock (w->mutex);
    memset (w->buffer, 0, w->max_buffer_len);
    deadbeef->mutex_unlock (w->mutex);
    w->buffer_len = 0;

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

    if (dec) {
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
            w->channels = fileinfo->fmt.channels;
            const int nsamples_per_channel = (int)deadbeef->pl_get_item_duration (it) * fileinfo->fmt.samplerate;
            const int samples_per_buf = floorf ((float) nsamples_per_channel / (float) width);
            const int max_samples_per_buf = 1 + samples_per_buf;

            int bytes_per_sample = fileinfo->fmt.bps / 8;
            int samplesize = fileinfo->fmt.channels * bytes_per_sample;
            data = malloc (sizeof (float) * max_samples_per_buf * samplesize);
            if (!data) {
                printf ("out of memory.\n");
                goto out;
            }
            memset (data, 0, sizeof (float) * max_samples_per_buf * samplesize);

            buffer = malloc (sizeof (float) * max_samples_per_buf * samplesize);
            if (!buffer) {
                printf ("out of memory.\n");
                goto out;
            }
            memset (buffer, 0, sizeof (float) * max_samples_per_buf * samplesize);

            buffer_len = samples_per_buf * samplesize;

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
            deadbeef->mutex_lock (w->mutex);
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
                int ch;

                for (ch = 0; ch < fileinfo->fmt.channels; ch++) {
                    min = 1.0; max = -1.0; rms = 0.0;
                    for (sample = 0; sample < sz/(bytes_per_sample*fileinfo->fmt.channels); sample++) {
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
                    w->buffer[counter] = (short)(max*1000);
                    w->buffer[counter+1] = (short)(min*1000);
                    w->buffer[counter+2] = (short)(rms*1000);
                    counter += 3;
                }
            }
            w->buffer_len = counter;
            if (CONFIG_CACHE_ENABLED) {
                waveform_db_cache (w, uri);
            }
            deadbeef->mutex_unlock (w->mutex);
            if (data) {
                free (data);
            }
            if (buffer) {
                free (buffer);
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

int
waveform_delete (const char *uri)
{
    waveform_db_open (cache_path, cache_path_size);
    waveform_db_init (NULL);
    int result = waveform_db_delete (uri);
    waveform_db_close ();
    return result;
}

int
waveform_cached (const char *uri)
{
    waveform_db_open (cache_path, cache_path_size);
    waveform_db_init (NULL);
    int result = waveform_db_cached (uri);
    waveform_db_close ();
    return result;
}

void
waveform_get_from_cache (gpointer user_data, const char *uri)
{
    w_waveform_t *w = user_data;
    deadbeef->mutex_lock (w->mutex);
    waveform_db_open (cache_path, cache_path_size);
    w->buffer_len = waveform_db_read (uri, w->buffer, w->max_buffer_len, &w->channels);
    waveform_db_close ();
    deadbeef->mutex_unlock (w->mutex);
}

void
waveform_get_wavedata (gpointer user_data)
{
    w_waveform_t *w = user_data;
    deadbeef->background_job_increment ();
    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    if (it) {
        char *uri = strdup (deadbeef->pl_find_meta_raw (it, ":URI"));
        if (uri && waveform_valid_track (it, uri)) {
            if (CONFIG_CACHE_ENABLED && waveform_cached (uri)) {
                waveform_get_from_cache (w, uri);
            }
            else {
                waveform_generate_wavedata (w, it, uri);
            }
        }
        if (uri) {
            free (uri);
        }
    }
    waveform_draw (w, 0);
    waveform_draw (w, 1);
    if (it) {
        deadbeef->pl_item_unref (it);
    }
    deadbeef->background_job_decrement ();
}

void
waveform_expose_event (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);
    cairo_t *cr = gdk_cairo_create (gtk_widget_get_window (w->drawarea));

    int x = 0;
    int y = 0;
    int width = a.width;
    int height = a.height;
    waveform_scale (w, cr, x, y, width, height);
    waveform_seekbar_draw (w, cr, x, y, width, height);
    cairo_destroy (cr);
}

gboolean
waveform_configure_event (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    if (w->resizetimer) {
        g_source_remove (w->resizetimer);
    }
    w->resizetimer = g_timeout_add (500, waveform_redraw_cb, w);
    return FALSE;
}

gboolean
waveform_motion_notify_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);

    if (w->seekbar_moving) {
        w->seekbar_move_x = event->x - a.x;
        gtk_widget_queue_draw (widget);
    }
    return TRUE;
}

gboolean
waveform_scroll_event (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    GdkEventScroll *ev = (GdkEventScroll *)event;
    if (!CONFIG_SCROLL_ENABLED) {
        return TRUE;
    }

    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (trk) {
        int duration = (int)(deadbeef->pl_get_item_duration (trk) * 1000);
        int time = (int)(deadbeef->streamer_get_playpos () * 1000);
        int step = CLAMP (duration / 30, 1000, 3600000);

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

gboolean
waveform_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    if (event->button == 3) {
      return TRUE;
    }
    GtkAllocation a;
    gtk_widget_get_allocation (w->drawarea, &a);

    w->seekbar_moving = 1;
    w->seekbar_moved = 0.0;
    w->seekbar_move_x = event->x - a.x;
    w->seekbar_move_x_clicked = event->x - a.x;
    return TRUE;
}

gboolean
waveform_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    w_waveform_t *w = user_data;
    if (event->button == 3) {
      gtk_menu_popup (GTK_MENU (w->popup), NULL, NULL, NULL, w->drawarea, 0, gtk_get_current_event_time ());
      return TRUE;
    }
    w->seekbar_moving = 0;
    w->seekbar_move_x_clicked = 0;
    w->seekbar_moved = 1.0;
    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (trk) {
        GtkAllocation a;
        gtk_widget_get_allocation (w->drawarea, &a);
        float time = (event->x - a.x) * deadbeef->pl_get_item_duration (trk) / (a.width) * 1000.f;
        if (time < 0) {
            time = 0;
        }
        deadbeef->sendmessage (DB_EV_SEEK, 0, time, 0);
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
        tid = deadbeef->thread_start_low_priority (waveform_get_wavedata, w);
        deadbeef->thread_detach (tid);
        break;
    case DB_EV_SONGCHANGED:
        deadbeef->mutex_lock (w->mutex);
        memset (w->buffer, 0, sizeof (short) * w->max_buffer_len);
        deadbeef->mutex_unlock (w->mutex);
        w->buffer_len = 0;
        w->channels = 0;
        g_idle_add (waveform_redraw_cb, w);
        break;
    case DB_EV_CONFIGCHANGED:
        on_config_changed (w);

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
    load_config ();
    wf->max_buffer_len = MAX_SAMPLES * VALUES_PER_SAMPLE * MAX_CHANNELS * sizeof (short);
    deadbeef->mutex_lock (wf->mutex);
    wf->buffer = malloc (sizeof (short) * wf->max_buffer_len);
    memset (wf->buffer, 0, sizeof (short) * wf->max_buffer_len);
    wf->surf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, a.width, a.height);
    wf->surf_shaded = cairo_image_surface_create (CAIRO_FORMAT_RGB24, a.width, a.height);
    deadbeef->mutex_unlock (wf->mutex);
    wf->seekbar_moving = 0;
    wf->seekbar_moved = 0;
    wf->height = a.height;
    wf->width = a.width;

    cache_path_size = make_cache_dir (cache_path, sizeof (cache_path));

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

    on_config_changed (w);
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
    w->frame = gtk_frame_new (NULL);
    w->popup = gtk_menu_new ();
    w->popup_item = gtk_menu_item_new_with_mnemonic ("Configure");
    w->mutex = deadbeef->mutex_create ();
    w->mutex_rendering = deadbeef->mutex_create ();
    mutex = deadbeef->mutex_create ();
    gtk_container_add (GTK_CONTAINER (w->base.widget), w->frame);
    gtk_container_add (GTK_CONTAINER (w->frame), w->drawarea);
    gtk_container_add (GTK_CONTAINER (w->popup), w->popup_item);
    gtk_widget_show (w->drawarea);
    gtk_widget_show (w->frame);
    gtk_widget_show (w->popup);
    gtk_widget_show (w->popup_item);

#if !GTK_CHECK_VERSION(3,0,0)
    g_signal_connect_after ((gpointer) w->drawarea, "expose_event", G_CALLBACK (waveform_expose_event), w);
#else
    g_signal_connect_after ((gpointer) w->drawarea, "draw", G_CALLBACK (waveform_expose_event), w);
#endif
    g_signal_connect_after ((gpointer) w->drawarea, "configure_event", G_CALLBACK (waveform_configure_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "button_press_event", G_CALLBACK (waveform_button_press_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "button_release_event", G_CALLBACK (waveform_button_release_event), w);
    g_signal_connect_after ((gpointer) w->base.widget, "scroll-event", G_CALLBACK (waveform_scroll_event), w);
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
                    if (waveform_cached (uri)) {
                        waveform_delete (uri);
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
    deadbeef->pl_lock ();
    lookup_action.flags |= DB_ACTION_DISABLED;
    DB_playItem_t *current = deadbeef->pl_get_first (PL_MAIN);
    while (current) {
        if (deadbeef->pl_is_selected (current) && waveform_cached (deadbeef->pl_find_meta_raw (current, ":URI"))) {
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
    .plugin.version_minor   = 3,
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
