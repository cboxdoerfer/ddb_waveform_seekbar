
# Waveform seekbar plugin for the DeaDBeeF audio player
#
# Copyright (C) 2013 Christian Boxdörfer <christian.boxdoerfer@posteo.de>
#
# Based on sndfile-tools waveform by Erik de Castro Lopo.
#     waveform.c - v1.04
#     Copyright (C) 2007-2012 Erik de Castro Lopo <erikd@mega-nerd.com>
#     Copyright (C) 2012 Robin Gareus <robin@gareus.org>
#     Copyright (C) 2013 driedfruit <driedfruit@mindloop.net>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


OUT_GTK2?=ddb_misc_waveform.so
GTK2_CFLAGS?=`pkg-config --cflags --libs gtk+-2.0`

CC?=gcc
CFLAGS+=-std=c99 -fPIC -Wall
LDFLAGS+=-shared -O2

SOURCES?=waveform.c

all: $(OUT_GTK2)

$(OUT_GTK2):
	$(CC) -I/usr/local/include $(CFLAGS) $(LDFLAGS) $(GTK2_CFLAGS) -o $(OUT_GTK2) $(SOURCES)
