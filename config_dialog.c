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
#include <fcntl.h>
#include <gtk/gtk.h>

#include <deadbeef/deadbeef.h>

#include "support.h"
#include "config.h"
#include "waveform.h"
#include "config_dialog.h"

void
on_button_config (GtkMenuItem *menuitem, gpointer user_data)
{
    GtkWidget *waveform_properties;
    GtkWidget *config_dialog;
    GtkWidget *vbox01;
    GtkWidget *hbox01;
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
    GtkWidget *color_ruler_label;
    GtkWidget *progressbar_color;
    GtkWidget *ruler_color;
    GtkWidget *downmix_to_mono;
    GtkWidget *log_scale;
    GtkWidget *display_rms;
    GtkWidget *display_ruler;
    GtkWidget *style_label;
    GtkWidget *style_frame;
    GtkWidget *options_label;
    GtkWidget *options_frame;
    GtkWidget *vbox02;
    GtkWidget *vbox03;
    GtkWidget *render_method_spikes;
    GtkWidget *render_method_bars;
    GtkWidget *shade_waveform;
    GtkWidget *fill_waveform;
    GtkWidget *soundcloud_style;
    GtkWidget *dialog_action_area13;
    GtkWidget *applybutton1;
    GtkWidget *cancelbutton1;
    GtkWidget *okbutton1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    waveform_properties = gtk_dialog_new ();
    gtk_window_set_transient_for (GTK_WINDOW (waveform_properties), GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(menuitem))));
    gtk_window_set_title (GTK_WINDOW (waveform_properties), "Waveform Properties");
    gtk_window_set_type_hint (GTK_WINDOW (waveform_properties), GDK_WINDOW_TYPE_HINT_DIALOG);

    config_dialog = gtk_dialog_get_content_area (GTK_DIALOG (waveform_properties));
    gtk_widget_show (config_dialog);

    vbox01 = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox01);
    gtk_box_pack_start (GTK_BOX (config_dialog), vbox01, FALSE, FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (vbox01), 12);

    hbox01 = gtk_hbox_new (FALSE, 8);
    gtk_widget_show (hbox01);
    gtk_box_pack_start (GTK_BOX (vbox01), hbox01, FALSE, FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (hbox01), 0);

    color_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (color_label),"<b>Colors</b>");
    gtk_widget_show (color_label);

    color_frame = gtk_frame_new ("Colors");
    gtk_frame_set_label_widget ((GtkFrame *)color_frame, color_label);
    gtk_frame_set_shadow_type ((GtkFrame *)color_frame, GTK_SHADOW_IN);
    gtk_widget_show (color_frame);
    gtk_box_pack_start (GTK_BOX (hbox01), color_frame, TRUE, FALSE, 0);

    color_table = gtk_table_new (5, 2, TRUE);
    gtk_widget_show (color_table);
    gtk_container_add (GTK_CONTAINER (color_frame), color_table);
    gtk_table_set_col_spacings ((GtkTable *) color_table, 8);
    gtk_table_set_row_spacings ((GtkTable *) color_table, 4);
    gtk_container_set_border_width (GTK_CONTAINER (color_table), 6);

    color_background_label = gtk_label_new ("Background");
    gtk_misc_set_alignment(GTK_MISC(color_background_label), 0, .5);
    gtk_widget_show (color_background_label);
    gtk_table_attach_defaults ((GtkTable *) color_table, color_background_label, 1,2,0,1);

    color_waveform_label = gtk_label_new ("Waveform");
    gtk_misc_set_alignment(GTK_MISC(color_waveform_label), 0, .5);
    gtk_widget_show (color_waveform_label);
    gtk_table_attach_defaults ((GtkTable *) color_table, color_waveform_label, 1,2,1,2);

    color_rms_label = gtk_label_new ("RMS");
    gtk_misc_set_alignment(GTK_MISC(color_rms_label), 0, .5);
    gtk_widget_show (color_rms_label);
    gtk_table_attach_defaults ((GtkTable *) color_table, color_rms_label, 1,2,2,3);

    color_progressbar_label = gtk_label_new ("Progressbar");
    gtk_misc_set_alignment(GTK_MISC(color_progressbar_label), 0, .5);
    gtk_widget_show (color_progressbar_label);
    gtk_table_attach_defaults ((GtkTable *) color_table, color_progressbar_label, 1,2,3,4);

    color_ruler_label = gtk_label_new ("Ruler");
    gtk_misc_set_alignment(GTK_MISC(color_ruler_label), 0, .5);
    gtk_widget_show (color_ruler_label);
    gtk_table_attach_defaults ((GtkTable *) color_table, color_ruler_label, 1,2,4,5);

    background_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)background_color, TRUE);
    gtk_widget_show (background_color);
    gtk_table_attach_defaults ((GtkTable *) color_table, background_color, 0,1,0,1);

    foreground_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)foreground_color, TRUE);
    gtk_widget_show (foreground_color);
    gtk_table_attach_defaults ((GtkTable *) color_table, foreground_color, 0,1,1,2);

    foreground_rms_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)foreground_rms_color, TRUE);
    gtk_widget_show (foreground_rms_color);
    gtk_table_attach_defaults ((GtkTable *) color_table, foreground_rms_color, 0,1,2,3);

    progressbar_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)progressbar_color, TRUE);
    gtk_widget_show (progressbar_color);
    gtk_table_attach_defaults ((GtkTable *) color_table, progressbar_color, 0,1,3,4);

    ruler_color = gtk_color_button_new ();
    gtk_color_button_set_use_alpha ((GtkColorButton *)ruler_color, TRUE);
    gtk_widget_show (ruler_color);
    gtk_table_attach_defaults ((GtkTable *) color_table, ruler_color, 0,1,4,5);

    style_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (style_label),"<b>Style</b>");
    gtk_widget_show (style_label);

    style_frame = gtk_frame_new ("Style");
    gtk_frame_set_label_widget ((GtkFrame *)style_frame, style_label);
    gtk_frame_set_shadow_type ((GtkFrame *)style_frame, GTK_SHADOW_IN);
    gtk_widget_show (style_frame);
    gtk_box_pack_start (GTK_BOX (hbox01), style_frame, FALSE, FALSE, 0);

    vbox02 = gtk_vbox_new (FALSE, 6);
    gtk_widget_show (vbox02);
    gtk_container_add (GTK_CONTAINER (style_frame), vbox02);

    render_method_spikes = gtk_radio_button_new_with_label (NULL, "Spikes");
    gtk_widget_show (render_method_spikes);
    gtk_box_pack_start (GTK_BOX (vbox02), render_method_spikes, TRUE, TRUE, 0);

    render_method_bars = gtk_radio_button_new_with_label_from_widget ((GtkRadioButton *)render_method_spikes, "Bars");
    gtk_widget_show (render_method_bars);
    gtk_box_pack_start (GTK_BOX (vbox02), render_method_bars, TRUE, TRUE, 0);

    fill_waveform = gtk_check_button_new_with_label ("Fill waveform");
    gtk_widget_show (fill_waveform);
    gtk_box_pack_start (GTK_BOX (vbox02), fill_waveform, TRUE, TRUE, 0);

    soundcloud_style = gtk_check_button_new_with_label ("Soundcloud style");
    gtk_widget_show (soundcloud_style);
    gtk_box_pack_start (GTK_BOX (vbox02), soundcloud_style, TRUE, TRUE, 0);

    shade_waveform = gtk_check_button_new_with_label ("Shade waveform");
    gtk_widget_show (shade_waveform);
    gtk_box_pack_start (GTK_BOX (vbox02), shade_waveform, TRUE, TRUE, 0);

    options_label = gtk_label_new (NULL);
    gtk_widget_show (options_label);

    options_frame = gtk_frame_new ("Options");
    gtk_frame_set_label_widget ((GtkFrame *)options_frame, options_label);
    gtk_frame_set_shadow_type ((GtkFrame *)options_frame, GTK_SHADOW_IN);
    gtk_widget_show (options_frame);
    gtk_box_pack_start (GTK_BOX (vbox01), options_frame, FALSE, FALSE, 0);

    vbox03 = gtk_vbox_new (FALSE, 6);
    gtk_widget_show (vbox03);
    gtk_container_add (GTK_CONTAINER (options_frame), vbox03);

    downmix_to_mono = gtk_check_button_new_with_label ("Downmix to mono");
    gtk_widget_show (downmix_to_mono);
    gtk_box_pack_start (GTK_BOX (vbox03), downmix_to_mono, FALSE, FALSE, 0);

    log_scale = gtk_check_button_new_with_label ("Logarithmic scale");
    gtk_widget_show (log_scale);
    gtk_box_pack_start (GTK_BOX (vbox03), log_scale, FALSE, FALSE, 0);

    display_rms = gtk_check_button_new_with_label ("Display RMS");
    gtk_widget_show (display_rms);
    gtk_box_pack_start (GTK_BOX (vbox03), display_rms, FALSE, FALSE, 0);

    display_ruler = gtk_check_button_new_with_label ("Display Ruler");
    gtk_widget_show (display_ruler);
    gtk_box_pack_start (GTK_BOX (vbox03), display_ruler, FALSE, FALSE, 0);

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
    gtk_color_button_set_color (GTK_COLOR_BUTTON (ruler_color), &CONFIG_RLR_COLOR);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (foreground_rms_color), &CONFIG_FG_RMS_COLOR);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON (background_color), CONFIG_BG_ALPHA);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON (foreground_color), CONFIG_FG_ALPHA);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON (progressbar_color), CONFIG_PB_ALPHA);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON (ruler_color), CONFIG_RLR_ALPHA);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON (foreground_rms_color), CONFIG_FG_RMS_ALPHA);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (downmix_to_mono), CONFIG_MIX_TO_MONO);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (log_scale), CONFIG_LOG_ENABLED);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (display_rms), CONFIG_DISPLAY_RMS);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (display_ruler), CONFIG_DISPLAY_RULER);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (shade_waveform), CONFIG_SHADE_WAVEFORM);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (soundcloud_style), CONFIG_SOUNDCLOUD_STYLE);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fill_waveform), CONFIG_FILL_WAVEFORM);

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
            gtk_color_button_get_color (GTK_COLOR_BUTTON (ruler_color), &CONFIG_RLR_COLOR);
            gtk_color_button_get_color (GTK_COLOR_BUTTON (foreground_rms_color), &CONFIG_FG_RMS_COLOR);
            CONFIG_BG_ALPHA = gtk_color_button_get_alpha (GTK_COLOR_BUTTON (background_color));
            CONFIG_FG_ALPHA = gtk_color_button_get_alpha (GTK_COLOR_BUTTON (foreground_color));
            CONFIG_PB_ALPHA = gtk_color_button_get_alpha (GTK_COLOR_BUTTON (progressbar_color));
            CONFIG_RLR_ALPHA = gtk_color_button_get_alpha (GTK_COLOR_BUTTON (ruler_color));
            CONFIG_FG_RMS_ALPHA = gtk_color_button_get_alpha (GTK_COLOR_BUTTON (foreground_rms_color));
            CONFIG_MIX_TO_MONO = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (downmix_to_mono));
            CONFIG_LOG_ENABLED = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (log_scale));
            CONFIG_DISPLAY_RMS = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (display_rms));
            CONFIG_DISPLAY_RULER = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (display_ruler));
            CONFIG_SHADE_WAVEFORM = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (shade_waveform));
            CONFIG_SOUNDCLOUD_STYLE = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (soundcloud_style));
            CONFIG_FILL_WAVEFORM = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fill_waveform));
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
