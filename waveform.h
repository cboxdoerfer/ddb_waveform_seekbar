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

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

#include <deadbeef/deadbeef.h>

extern DB_functions_t *deadbeef;

typedef struct wavedata_s
{
    char *fname;
    short *data;
    size_t data_len;
    int channels;
} wavedata_t;

typedef struct color_s
{
    double r;
    double g;
    double b;
    double a;
} color_t;

typedef struct
{
    double x;
    double y;
    double width;
    double height;
} waveform_rect_t;

typedef struct waveform_colors_s
{
    color_t fg, rms, bg, pb, rlr, font, font_pb;
} waveform_colors_t;

enum STYLE { BARS = 1, SPIKES = 2 };

