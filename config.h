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

#pragma once 

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <fcntl.h>
#include <gtk/gtk.h>

#include <deadbeef/deadbeef.h>
#include "cache.h"

#define     CONFSTR_WF_LOG_ENABLED       "waveform.log_enabled"
#define     CONFSTR_WF_MIX_TO_MONO       "waveform.mix_to_mono"
#define     CONFSTR_WF_DISPLAY_RMS       "waveform.display_rms"
#define     CONFSTR_WF_DISPLAY_RULER     "waveform.display_ruler"
#define     CONFSTR_WF_RENDER_METHOD     "waveform.render_method"
#define     CONFSTR_WF_FILL_WAVEFORM     "waveform.fill_waveform"
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

extern gboolean CONFIG_LOG_ENABLED;
extern gboolean CONFIG_MIX_TO_MONO;
extern gboolean CONFIG_CACHE_ENABLED;
extern gboolean CONFIG_SCROLL_ENABLED;
extern gboolean CONFIG_DISPLAY_RMS;
extern gboolean CONFIG_DISPLAY_RULER;
extern gboolean CONFIG_SHADE_WAVEFORM;
extern gboolean CONFIG_SOUNDCLOUD_STYLE;
extern GdkColor CONFIG_BG_COLOR;
extern GdkColor CONFIG_FG_COLOR;
extern GdkColor CONFIG_PB_COLOR;
extern GdkColor CONFIG_FG_RMS_COLOR;
extern guint16  CONFIG_BG_ALPHA;
extern guint16  CONFIG_FG_ALPHA;
extern guint16  CONFIG_PB_ALPHA;
extern guint16  CONFIG_FG_RMS_ALPHA;
extern gint     CONFIG_RENDER_METHOD;
extern gint     CONFIG_FILL_WAVEFORM;
extern gint     CONFIG_BORDER_WIDTH;
extern gint     CONFIG_CURSOR_WIDTH;
extern gint     CONFIG_FONT_SIZE;
extern gint     CONFIG_MAX_FILE_LENGTH;
extern gint     CONFIG_NUM_SAMPLES;
extern gint     CONFIG_REFRESH_INTERVAL;


void
save_config (void);

void
load_config (void);

