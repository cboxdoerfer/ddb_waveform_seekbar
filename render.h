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

#pragma once

#include <stdbool.h>
#include "waveform.h"

typedef struct {
    float max;
    float min;
    float rms;
} waveform_sample_t;

typedef struct {
    waveform_sample_t **samples;
    int num_channels;
    // samples per channel
    int num_samples;
} waveform_data_render_t;

void
waveform_data_render_free (waveform_data_render_t *w_render_ctx);

waveform_data_render_t *
waveform_render_data_build (wavedata_t *wave_data, int width, bool downmix_mono);

void
waveform_draw_wave_default (waveform_sample_t *samples,
                            waveform_colors_t *colors,
                            cairo_t *cr_ctx,
                            double x,
                            double y,
                            double width,
                            double height);

void
waveform_draw_wave_bars (waveform_sample_t *samples,
                         waveform_colors_t *colors,
                         cairo_t *cr_ctx,
                         double x,
                         double y,
                         double width,
                         double height);
