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

#include "config.h"
#include "waveform.h"

gboolean CONFIG_LOG_ENABLED = FALSE;
gboolean CONFIG_MIX_TO_MONO = FALSE;
gboolean CONFIG_CACHE_ENABLED = TRUE;
gboolean CONFIG_SCROLL_ENABLED = TRUE;
gboolean CONFIG_DISPLAY_RMS = TRUE;
gboolean CONFIG_DISPLAY_RULER = FALSE;
gboolean CONFIG_SHADE_WAVEFORM = FALSE;
gboolean CONFIG_SOUNDCLOUD_STYLE = FALSE;
GdkColor CONFIG_BG_COLOR;
GdkColor CONFIG_FG_COLOR;
GdkColor CONFIG_PB_COLOR;
GdkColor CONFIG_FG_RMS_COLOR;
guint16  CONFIG_BG_ALPHA;
guint16  CONFIG_FG_ALPHA;
guint16  CONFIG_PB_ALPHA;
guint16  CONFIG_FG_RMS_ALPHA;
gint     CONFIG_RENDER_METHOD = SPIKES;
gint     CONFIG_FILL_WAVEFORM = 1;
gint     CONFIG_BORDER_WIDTH = 1;
gint     CONFIG_CURSOR_WIDTH = 3;
gint     CONFIG_FONT_SIZE = 18;
gint     CONFIG_MAX_FILE_LENGTH = 180;
gint     CONFIG_NUM_SAMPLES = 2048;
gint     CONFIG_REFRESH_INTERVAL = 33;

void
save_config (void)
{
    deadbeef->conf_set_int (CONFSTR_WF_LOG_ENABLED,         CONFIG_LOG_ENABLED);
    deadbeef->conf_set_int (CONFSTR_WF_MIX_TO_MONO,         CONFIG_MIX_TO_MONO);
    deadbeef->conf_set_int (CONFSTR_WF_DISPLAY_RMS,         CONFIG_DISPLAY_RMS);
    deadbeef->conf_set_int (CONFSTR_WF_DISPLAY_RULER,       CONFIG_DISPLAY_RULER);
    deadbeef->conf_set_int (CONFSTR_WF_SHADE_WAVEFORM,      CONFIG_SHADE_WAVEFORM);
    deadbeef->conf_set_int (CONFSTR_WF_SOUNDCLOUD_STYLE,    CONFIG_SOUNDCLOUD_STYLE);
    deadbeef->conf_set_int (CONFSTR_WF_RENDER_METHOD,       CONFIG_RENDER_METHOD);
    deadbeef->conf_set_int (CONFSTR_WF_FILL_WAVEFORM,       CONFIG_FILL_WAVEFORM);
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

void
load_config (void)
{
    deadbeef->conf_lock ();
    CONFIG_LOG_ENABLED = deadbeef->conf_get_int (CONFSTR_WF_LOG_ENABLED,             FALSE);
    CONFIG_MIX_TO_MONO = deadbeef->conf_get_int (CONFSTR_WF_MIX_TO_MONO,             FALSE);
    CONFIG_DISPLAY_RMS = deadbeef->conf_get_int (CONFSTR_WF_DISPLAY_RMS,              TRUE);
    CONFIG_DISPLAY_RULER = deadbeef->conf_get_int (CONFSTR_WF_DISPLAY_RULER,         FALSE);
    CONFIG_SHADE_WAVEFORM = deadbeef->conf_get_int (CONFSTR_WF_SHADE_WAVEFORM,       FALSE);
    CONFIG_SOUNDCLOUD_STYLE = deadbeef->conf_get_int (CONFSTR_WF_SOUNDCLOUD_STYLE,   FALSE);
    CONFIG_RENDER_METHOD = deadbeef->conf_get_int (CONFSTR_WF_RENDER_METHOD,        SPIKES);
    CONFIG_FILL_WAVEFORM = deadbeef->conf_get_int (CONFSTR_WF_FILL_WAVEFORM,             1);
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
        /*foreground*/  { CONFIG_FG_COLOR.red/65535.f,CONFIG_FG_COLOR.green/65535.f,
                          CONFIG_FG_COLOR.blue/65535.f, CONFIG_FG_ALPHA/65535.f },
        /*wave-rms*/    { CONFIG_FG_RMS_COLOR.red/65535.f,CONFIG_FG_RMS_COLOR.green/65535.f,
                          CONFIG_FG_RMS_COLOR.blue/65535.f, CONFIG_FG_RMS_ALPHA/65535.f },
        /*background*/  { CONFIG_BG_COLOR.red/65535.f,CONFIG_BG_COLOR.green/65535.f,
                          CONFIG_BG_COLOR.blue/65535.f, CONFIG_BG_ALPHA/65535.f },
    };
}

