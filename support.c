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

#include "support.h"

#if !GTK_CHECK_VERSION(2,18,0)
void
gtk_widget_set_allocation (GtkWidget *widget, const GtkAllocation *allocation) {
    widget->allocation.x = (allocation)->x;
    widget->allocation.y = (allocation)->y;
    widget->allocation.width = (allocation)->width;
    widget->allocation.height = (allocation)->height;
}

void
gtk_widget_get_allocation (GtkWidget *widget, GtkAllocation *allocation) {
    (allocation)->x = widget->allocation.x;
    (allocation)->y = widget->allocation.y;
    (allocation)->width = widget->allocation.width;
    (allocation)->height = widget->allocation.height;
}
#endif
