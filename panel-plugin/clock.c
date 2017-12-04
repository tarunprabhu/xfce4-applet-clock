/*
 *  Generic Monitor plugin for the Xfce4 panel
 *  Main file for the Battmon plugin
 *  Copyright (c) 2004 Roger Seguin <roger_seguin@msn.com>
 *                                  <http://rmlx.dyndns.org>
 *  Copyright (c) 2006 Julien Devemy <jujucece@gmail.com>
 *  Copyright (c) 2012 John Lindgren <john.lindgren@aol.com>
 *  Copyright (c) 2017 Tarun Prabhu <tarun.prabhu@gmail.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.

 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.

 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>

#include <libxfce4panel/xfce-panel-convenience.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BORDER 2

#define CLOCK_SCALE 0.1
#define TICKS_TO_RADIANS(x) (G_PI - (G_PI / 30.0) * (x))
#define HOURS_TO_RADIANS(x, y)                                                 \
  (G_PI - (G_PI / 6.0) * (((x) > 12 ? (x)-12 : (x)) + (y) / 60.0))

typedef struct gui_t {
  /* Configuration GUI widgets */
  GtkWidget *wTitleFont;
  GtkWidget *wTitle;
  GtkWidget *wShowTitle;
  GtkWidget *wDateFont;
  GtkWidget *wDateFormat;
  GtkWidget *wShowDate;
  GtkWidget *wTimeFont;
  GtkWidget *wTimeFormat;
  GtkWidget *wShowTime;
  GtkWidget *wTimezone;
} gui_t;

typedef struct param_t {
  /* Configurable parameters */
  gchar *titleFont;
  gchar *dateFont;
  gchar *timeFont;
  gchar *timezone;
  gchar *title;
  gchar *dateFormat;
  gchar *timeFormat;
  gboolean showTime;
  gboolean showDate;
  gboolean showTitle;
} param_t;

typedef struct conf_t {
  GtkWidget *wTopLevel;
  struct gui_t oGUI; /* Configuration/option dialog */
  struct param_t oParam;
} conf_t;

typedef struct monitor_t {
  /* Plugin monitor */
  GtkWidget *wEventBox;
  GtkWidget *wBox;
  GtkWidget *wTitle;
  GtkWidget *wDay;
  GtkWidget *wDate;
  GtkWidget *wImgBox;
  GtkWidget *wTime;
  GtkWidget *wClock;
} monitor_t;

typedef struct analog_clock_t {
  XfcePanelPlugin *plugin;
  unsigned int iTimerId; /* Cyclic update */
  struct conf_t oConf;
  struct monitor_t oMonitor;
  guint day;
  guint month;
  guint hr;
  guint min;
  GTimeZone *tz;
} analog_clock_t;

static const gchar *GetWeekdayAsString(guint day) {
  switch (day) {
  case 1:
    return "Mon";
  case 2:
    return "Tue";
  case 3:
    return "Wed";
  case 4:
    return "Thu";
  case 5:
    return "Fri";
  case 6:
    return "Sat";
  case 7:
    return "Sun";
  default:
    return "---";
  }
}

static void DisplayClock(struct analog_clock_t *poPlugin) {
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);
  gtk_widget_queue_draw(poMonitor->wClock);
}

static void DrawTicks(cairo_t *cr, gdouble xc, gdouble yc, gdouble radius) {
  gint i;
  gdouble x, y, angle;

  for (i = 0; i < 12; i++) {
    /* calculate */
    angle = HOURS_TO_RADIANS(i, 0);
    x = xc + sin(angle) * (radius * (1.0 - CLOCK_SCALE));
    y = yc + cos(angle) * (radius * (1.0 - CLOCK_SCALE));

    /* draw arc */
    cairo_move_to(cr, x, y);
    cairo_arc(cr, x, y, radius * CLOCK_SCALE, 0, 2 * G_PI);
    cairo_close_path(cr);
  }

  /* fill the arcs */
  cairo_fill(cr);
}

static void DrawPointer(cairo_t *cr, gdouble xc, gdouble yc, gdouble radius,
                        gdouble angle, gdouble scale, gboolean line) {
  gdouble xs, ys;
  gdouble xt, yt;

  /* calculate tip position */
  xt = xc + sin(angle) * radius * scale;
  yt = yc + cos(angle) * radius * scale;

  if (line) {
    /* draw the line */
    cairo_move_to(cr, xc, yc);
    cairo_line_to(cr, xt, yt);

    /* draw the line */
    cairo_stroke(cr);
  } else {
    /* calculate start position */
    xs = xc + sin(angle - 0.5 * G_PI) * radius * CLOCK_SCALE;
    ys = yc + cos(angle - 0.5 * G_PI) * radius * CLOCK_SCALE;

    /* draw the pointer */
    cairo_move_to(cr, xs, ys);
    cairo_arc(cr, xc, yc, radius * CLOCK_SCALE, -angle + G_PI, -angle);
    cairo_line_to(cr, xt, yt);
    cairo_close_path(cr);

    /* fill the pointer */
    cairo_fill(cr);
  }
}

static void draw_area_cb(GtkWidget *da, cairo_t *cr, gpointer pdata) {
  gint w, h;
  gdouble xc, yc;
  gdouble radius;
  double angle;
  GDateTime *date_time;
  guint hr, min;
  guint day, month;
  gchar weekday[4];
  gchar time[6];
  gchar date[6];

  struct analog_clock_t *clock = (struct analog_clock_t *)pdata;
  GtkStyleContext *css_context = gtk_widget_get_style_context(GTK_WIDGET(da));

  w = gtk_widget_get_allocated_width(da);
  h = gtk_widget_get_allocated_height(da);
  xc = w / 2;
  yc = h / 2;
  radius = ((xc < yc) ? xc : yc);

  DrawTicks(cr, xc, yc, radius);

  /* get the local time */
  date_time = g_date_time_new_now(clock->tz);
  hr = g_date_time_get_hour(date_time);
  min = g_date_time_get_minute(date_time);
  day = g_date_time_get_day_of_month(date_time);
  month = g_date_time_get_month(date_time);

  /* minute pointer */
  angle = TICKS_TO_RADIANS(min);
  DrawPointer(cr, xc, yc, radius, angle, 0.8, FALSE);

  /* hour pointer */
  angle = HOURS_TO_RADIANS(hr, min);
  DrawPointer(cr, xc, yc, radius, angle, 0.5, FALSE);

  if (clock->hr != hr || clock->min != min) {
    g_snprintf(time, sizeof(time), "%02d:%02d", hr, min);
    gtk_label_set_text(GTK_LABEL(clock->oMonitor.wTime), time);
    clock->hr = hr;
    clock->min = min;
  }

  if (clock->day != day) {
    gtk_label_set_text(
        GTK_LABEL(clock->oMonitor.wDay),
        GetWeekdayAsString(g_date_time_get_day_of_week(date_time)));
    g_snprintf(date, sizeof(date), "%02d/%02d", day, month);
    gtk_label_set_text(GTK_LABEL(clock->oMonitor.wDate), date);

    clock->day = day;
    clock->month = month;
  }

  g_date_time_unref(date_time);
}

static gboolean SetTimer(void *p_pvPlugin) {
  struct analog_clock_t *poPlugin = (analog_clock_t *)p_pvPlugin;
  struct param_t *poConf = &(poPlugin->oConf.oParam);

  DisplayClock(poPlugin);

  if (poPlugin->iTimerId == 0) {
    poPlugin->iTimerId = g_timeout_add(1000, (GSourceFunc)SetTimer, poPlugin);
    return FALSE;
  }
  return TRUE;
}

static gboolean SetTitle(void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t*) data;
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct gui_t *poGUI = &(poPlugin->oConf.oGUI);

  gtk_label_set_text(GTK_LABEL(poMonitor->wTitle), poConf->title);

  return TRUE;
}

static gboolean SetTimezone(void* data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t*) data;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct gui_t *poGUI = &(poPlugin->oConf.oGUI);

  poPlugin->tz = g_time_zone_new(poConf->timezone);
  DisplayClock(poPlugin);

  return TRUE;
}

static gboolean SetVisibilityTitle(void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t*) data;
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct gui_t *poGUI = &(poPlugin->oConf.oGUI);

  if (poConf->showTitle == TRUE) {
    gtk_widget_show(poMonitor->wTitle);
    gtk_widget_set_sensitive(poGUI->wTitle, TRUE);
  } else {
    gtk_widget_hide(poMonitor->wTitle);
    gtk_widget_set_sensitive(poGUI->wTitle, FALSE);
  }
  
  return TRUE;
}

static gboolean SetVisibilityDate(void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t*) data;
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct gui_t *poGUI = &(poPlugin->oConf.oGUI);

  if (poConf->showDate == TRUE) {
    gtk_widget_show(poMonitor->wDay);
    gtk_widget_show(poMonitor->wDate);
  } else {
    gtk_widget_hide(poMonitor->wDay);
    gtk_widget_hide(poMonitor->wDate);
  }

  return TRUE;
}

static gboolean SetVisibilityTime(void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t*) data;
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct gui_t *poGUI = &(poPlugin->oConf.oGUI);

  if (poConf->showTime == TRUE) {
    gtk_widget_show(poMonitor->wTime);
  } else {
    gtk_widget_hide(poMonitor->wTime);
  }

  return TRUE;
}

static GtkWidget *create_label(const gchar *title) {
  GtkWidget *label;

  label = gtk_label_new("");
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
  gtk_label_set_line_wrap(GTK_LABEL(label), FALSE);
  gtk_widget_set_hexpand(GTK_WIDGET(label), TRUE);
  gtk_label_set_text(GTK_LABEL(label), title);
  return label;
}

static analog_clock_t *clock_create_control(XfcePanelPlugin *plugin) {
  struct analog_clock_t *poPlugin;
  struct param_t *poConf;
  struct monitor_t *poMonitor;
  GtkOrientation orientation = xfce_panel_plugin_get_orientation(plugin);
  GtkSettings *settings;
  gchar *default_font;

  GtkStyleContext *context;
  GtkCssProvider *css_provider;

  poPlugin = g_new(analog_clock_t, 1);
  memset(poPlugin, 0, sizeof(analog_clock_t));
  poConf = &(poPlugin->oConf.oParam);
  poMonitor = &(poPlugin->oMonitor);

  poPlugin->plugin = plugin;

  poPlugin->iTimerId = 0;

  poConf->title = g_strdup("Title");
  poConf->timezone = g_strdup("UTC");
  poConf->showTitle = TRUE;
  poConf->showDate = TRUE;
  poConf->showTime = TRUE;
  poConf->dateFormat = g_strdup("%e/%m");
  poConf->timeFormat = g_strdup("%H:%M");

  poPlugin->tz = g_time_zone_new(poConf->timezone);
  poPlugin->day = 0;
  poPlugin->month = 0;
  poPlugin->hr = 0;
  poPlugin->min = 0;

  settings = gtk_settings_get_default();
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings),
                                   "gtk-font-name")) {
    g_object_get(settings, "gtk-font-name", &default_font, NULL);
    poConf->titleFont = g_strdup(default_font);
    poConf->timeFont = g_strdup(default_font);
    poConf->dateFont = g_strdup(default_font);
  } else {
    poConf->titleFont = g_strdup("Sans Bold 9.8");
    poConf->timeFont = g_strdup("Sans Bold 9.8");
    poConf->dateFont = g_strdup("Sans Bold 9.8");
  }

  poMonitor->wEventBox = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(poMonitor->wEventBox), FALSE);
  gtk_widget_show(poMonitor->wEventBox);

  xfce_panel_plugin_add_action_widget(plugin, poMonitor->wEventBox);

  poMonitor->wBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER);
  context = gtk_widget_get_style_context(poMonitor->wBox);
  gtk_style_context_add_class(context, "clock_plugin");
  gtk_widget_show(poMonitor->wBox);
  gtk_container_set_border_width(GTK_CONTAINER(poMonitor->wBox), 0);
  gtk_container_add(GTK_CONTAINER(poMonitor->wEventBox), poMonitor->wBox);

  /* Add Title */
  poMonitor->wTitle = create_label(poConf->title);
  gtk_box_pack_start(GTK_BOX(poMonitor->wBox), GTK_WIDGET(poMonitor->wTitle),
                     TRUE, FALSE, 0);
  gtk_widget_show(poMonitor->wTitle);

  /* Add day */
  poMonitor->wDay = create_label(GetWeekdayAsString(poPlugin->day));
  gtk_box_pack_start(GTK_BOX(poMonitor->wBox), GTK_WIDGET(poMonitor->wDay),
                     TRUE, FALSE, 0);
  gtk_widget_show(poMonitor->wDay);

  /* Add date */
  poMonitor->wDate = create_label("00/00");
  gtk_box_pack_start(GTK_BOX(poMonitor->wBox), GTK_WIDGET(poMonitor->wDate),
                     TRUE, FALSE, 0);
  gtk_widget_show(poMonitor->wDate);

  /* Add Image */
  poMonitor->wClock = gtk_drawing_area_new();
  gtk_box_pack_start(GTK_BOX(poMonitor->wBox), GTK_WIDGET(poMonitor->wClock),
                     TRUE, FALSE, 0);
  g_signal_connect(poMonitor->wClock, "draw", G_CALLBACK(draw_area_cb),
                   poPlugin);
  gtk_widget_show(poMonitor->wClock);

  /* Add Time */
  poMonitor->wTime = create_label("00:00");
  gtk_box_pack_start(GTK_BOX(poMonitor->wBox), GTK_WIDGET(poMonitor->wTime),
                     TRUE, FALSE, 0);
  gtk_widget_show(poMonitor->wTime);

  css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css_provider,
                                  "label: { text-align: center; }", -1, NULL);
  gtk_style_context_add_provider(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(
                                     GTK_WIDGET(poMonitor->wTitle))),
                                 GTK_STYLE_PROVIDER(css_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_css_provider_load_from_data(css_provider,
                                  "label: { text-align: center; }", -1, NULL);
  gtk_style_context_add_provider(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(
                                     GTK_WIDGET(poMonitor->wDay))),
                                 GTK_STYLE_PROVIDER(css_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_css_provider_load_from_data(css_provider,
                                  "label: { text-align: center; }", -1, NULL);
  gtk_style_context_add_provider(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(
                                     GTK_WIDGET(poMonitor->wDate))),
                                 GTK_STYLE_PROVIDER(css_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css_provider,
                                  "label: { text-align: center; }", -1, NULL);
  gtk_style_context_add_provider(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(
                                     GTK_WIDGET(poMonitor->wTime))),
                                 GTK_STYLE_PROVIDER(css_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_style_context_add_provider(GTK_STYLE_CONTEXT(gtk_widget_get_style_context(
                                     GTK_WIDGET(poMonitor->wClock))),
                                 GTK_STYLE_PROVIDER(css_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_free(default_font);

  return poPlugin;
}

static void clock_free(XfcePanelPlugin *plugin, analog_clock_t *poPlugin) {
  TRACE("clock_free()\n");

  if (poPlugin->iTimerId)
    g_source_remove(poPlugin->iTimerId);
  g_free(poPlugin->tz);

  g_free(poPlugin->oConf.oParam.titleFont);
  g_free(poPlugin->oConf.oParam.dateFont);
  g_free(poPlugin->oConf.oParam.timeFont);
  g_free(poPlugin->oConf.oParam.title);
  g_free(poPlugin->oConf.oParam.timezone);
  g_free(poPlugin->oConf.oParam.dateFormat);
  g_free(poPlugin->oConf.oParam.timeFormat);
  g_free(poPlugin);
}

static void SetFont(GtkWidget *widget, const gchar *name) {
  GtkCssProvider *css_provider = NULL;
  gchar *css = NULL;
  PangoFontDescription *font = NULL;

  font = pango_font_description_from_string(name);
  if (G_LIKELY(font)) {
    css = g_strdup_printf(
        "label { font-family: %s; \
                 font-size: %dpx; \
                 font-style: %s; \
                 font-weight: %s; \
                 text-align: center; \
               }",
        pango_font_description_get_family(font),
        pango_font_description_get_size(font) / PANGO_SCALE,
        (pango_font_description_get_style(font) == PANGO_STYLE_ITALIC ||
         pango_font_description_get_style(font) == PANGO_STYLE_OBLIQUE)
            ? "italic"
            : "normal",
        (pango_font_description_get_weight(font) >= PANGO_WEIGHT_BOLD)
            ? "bold"
            : "normal");
    pango_font_description_free(font);
  } else {
    css = g_strdup_printf("label { font: %s; }", name);
  }

  css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css_provider, css, strlen(css), NULL);
  gtk_style_context_add_provider(
      GTK_STYLE_CONTEXT(gtk_widget_get_style_context(widget)),
      GTK_STYLE_PROVIDER(css_provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_free(css);
}

static int SetMonitorFont(void *p_pvPlugin) {
  struct analog_clock_t *poPlugin = (analog_clock_t *)p_pvPlugin;
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);
  struct param_t *poConf = &(poPlugin->oConf.oParam);

  SetFont(poMonitor->wTitle, poConf->titleFont);
  SetFont(poMonitor->wDay, poConf->dateFont);
  SetFont(poMonitor->wDate, poConf->dateFont);
  SetFont(poMonitor->wTime, poConf->timeFont);

  return 0;
}

static void clock_read_config(XfcePanelPlugin *plugin, analog_clock_t *poPlugin)
/* Plugin API */
/* Executed when the panel is started - Read the configuration
   previously stored in xml file */
{
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);
  const char *pc;
  char *file;
  XfceRc *rc;

  if (!(file = xfce_panel_plugin_lookup_rc_file(plugin)))
    return;

  rc = xfce_rc_simple_open(file, TRUE);
  g_free(file);

  if (!rc)
    return;

  if ((pc = xfce_rc_read_entry(rc, "TitleFont", NULL))) {
    g_free(poConf->titleFont);
    poConf->titleFont = g_strdup(pc);
  }

  if ((pc = xfce_rc_read_entry(rc, "DateFont", NULL))) {
    g_free(poConf->dateFont);
    poConf->dateFont = g_strdup(pc);
  }

  if ((pc = xfce_rc_read_entry(rc, "TimeFont", NULL))) {
    g_free(poConf->timeFont);
    poConf->timeFont = g_strdup(pc);
  }

  if ((pc = xfce_rc_read_entry(rc, "Title", NULL))) {
    g_free(poConf->title);
    poConf->title = g_strdup(pc);
  }

  if ((pc = xfce_rc_read_entry(rc, "Timezone", NULL))) {
    g_free(poConf->timezone);
    poConf->timezone = g_strdup(pc);
  }

  poConf->showTitle =
      xfce_rc_read_int_entry(rc, "ShowTitle", poConf->showTitle);
  poConf->showDate = xfce_rc_read_int_entry(rc, "ShowDate", poConf->showDate);
  poConf->showTime = xfce_rc_read_int_entry(rc, "ShowTime", poConf->showTime);

  xfce_rc_close(rc);
}

static void clock_write_config(XfcePanelPlugin *plugin,
                               analog_clock_t *poPlugin) {
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  XfceRc *rc;
  char *file;

  if (!(file = xfce_panel_plugin_save_location(plugin, TRUE)))
    return;

  rc = xfce_rc_simple_open(file, FALSE);
  g_free(file);

  if (!rc)
    return;

  TRACE("clock_write_config()\n");

  xfce_rc_write_entry(rc, "TitleFont", poConf->titleFont);
  xfce_rc_write_entry(rc, "DateFont", poConf->dateFont);
  xfce_rc_write_entry(rc, "TimeFont", poConf->timeFont);
  xfce_rc_write_entry(rc, "Title", poConf->title);
  xfce_rc_write_entry(rc, "Timezone", poConf->timezone);
  xfce_rc_write_int_entry(rc, "ShowTitle", poConf->showTitle);
  xfce_rc_write_int_entry(rc, "ShowDate", poConf->showDate);
  xfce_rc_write_int_entry(rc, "ShowTime", poConf->showTime);

  xfce_rc_close(rc);
}

static void UpdateConf(void *p_pvPlugin)
/* Called back when the configuration/options window is closed */
{
  struct analog_clock_t *poPlugin = (analog_clock_t *)p_pvPlugin;
  struct conf_t *poConf = &(poPlugin->oConf);
  struct gui_t *poGUI = &(poConf->oGUI);

  TRACE("UpdateConf()\n");
  SetMonitorFont(poPlugin);
  /* Restart timer */
  if (poPlugin->iTimerId) {
    g_source_remove(poPlugin->iTimerId);
    poPlugin->iTimerId = 0;
  }
  SetTimer(p_pvPlugin);
  SetTitle(p_pvPlugin);
  SetTimezone(p_pvPlugin);
  SetVisibilityTitle(p_pvPlugin);
  SetVisibilityDate(p_pvPlugin);
  SetVisibilityTime(p_pvPlugin);
}

static void About(XfcePanelPlugin *plugin) {
  GdkPixbuf *icon;

  const gchar *auth[] = {"Tarun Prabhu <tarun.prabhu@gmail.com>", NULL};

  icon = xfce_panel_pixbuf_from_source("clock", NULL, 32);
  gtk_show_about_dialog(NULL, "logo", icon, "license",
                        xfce_get_license_text(XFCE_LICENSE_TEXT_GPL), "version",
                        VERSION, "program-name", PACKAGE, "comments",
                        _("Analyg clock"), "website", "", "copyright",
                        _("Copyright \xc2\xa9 2017 Tarun Prabhu\n"), "author",
                        auth, NULL);

  if (icon)
    g_object_unref(G_OBJECT(icon));
}

static void ChooseFont(GtkWidget *button, struct analog_clock_t *poPlugin,
                       gchar **p_font) {
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  GtkWidget *wDialog;
  const char *pcFont;
  int iResponse;

  wDialog = gtk_font_chooser_dialog_new(
      _("Font Selection"), GTK_WINDOW(gtk_widget_get_toplevel(button)));
  gtk_window_set_transient_for(GTK_WINDOW(wDialog),
                               GTK_WINDOW(poPlugin->oConf.wTopLevel));

  gtk_font_chooser_set_font(GTK_FONT_CHOOSER(wDialog), *p_font);
  iResponse = gtk_dialog_run(GTK_DIALOG(wDialog));
  if (iResponse == GTK_RESPONSE_OK) {
    pcFont = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(wDialog));
    if (pcFont) {
      g_free(*p_font);
      *p_font = g_strdup(pcFont);
      gtk_button_set_label(GTK_BUTTON(button), *p_font);
    }
  }
  gtk_widget_destroy(wDialog);
}

static void ChooseTitleFont(GtkWidget *button, void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t *)data;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  ChooseFont(button, poPlugin, &(poConf->titleFont));
}

static void ChooseDateFont(GtkWidget *button, void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t *)data;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  ChooseFont(button, poPlugin, &(poConf->dateFont));
}

static void ChooseTimeFont(GtkWidget *button, void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t *)data;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  ChooseFont(button, poPlugin, &(poConf->timeFont));
}

static void ToggleShowTitle(GtkWidget *button, void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t *)data;
  struct gui_t *poGUI = &(poPlugin->oConf.oGUI);
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);

  poConf->showTitle = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
}

static void ToggleShowDate(GtkWidget *button, void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t *)data;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);

  poConf->showDate = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
}

static void ToggleShowTime(GtkWidget *button, void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t *)data;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);

  poConf->showTime = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
}

static void UpdateTitle(GtkWidget *entry, void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t *)data;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);

  g_free(poConf->title);

  poConf->title = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
  SetTitle(poPlugin);
}

static void UpdateTimezone(GtkWidget *entry, void *data) {
  struct analog_clock_t *poPlugin = (struct analog_clock_t *)data;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct monitor_t *poMonitor = &(poPlugin->oMonitor);

  g_free(poConf->timezone);
  if (poPlugin->tz)
    g_time_zone_unref(poPlugin->tz);

  poConf->timezone = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
  SetTimezone(poPlugin);
}

static void clock_dialog_response(GtkWidget *dlg, int response,
                                  analog_clock_t *clock) {
  UpdateConf(clock);
  gtk_widget_destroy(dlg);
  xfce_panel_plugin_unblock_menu(clock->plugin);
  clock_write_config(clock->plugin, clock);
  DisplayClock(clock);
}

static int clock_create_config_gui(GtkWidget *, struct param_t *,
                                   struct gui_t *);
static void clock_create_options(XfcePanelPlugin *plugin,
                                 analog_clock_t *poPlugin) {
  GtkWidget *dlg, *vbox;
  struct param_t *poConf = &(poPlugin->oConf.oParam);
  struct gui_t *poGUI = &(poPlugin->oConf.oGUI);

  TRACE("clock_create_options()\n");

  xfce_panel_plugin_block_menu(plugin);

  dlg = xfce_titled_dialog_new_with_buttons(
      _("Analog Clock Configuration"),
      GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(plugin))),
      GTK_DIALOG_DESTROY_WITH_PARENT, "gtk-close", GTK_RESPONSE_OK, NULL);

  g_signal_connect(dlg, "response", G_CALLBACK(clock_dialog_response),
                   poPlugin);

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER + 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), BORDER + 4);
  gtk_widget_show(vbox);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                     vbox, TRUE, TRUE, 0);

  poPlugin->oConf.wTopLevel = dlg;

  (void)clock_create_config_gui(GTK_WIDGET(vbox), poConf, poGUI);

  gtk_button_set_label(GTK_BUTTON(poGUI->wTitleFont), poConf->titleFont);
  g_signal_connect(G_OBJECT(poGUI->wTitleFont), "clicked",
                   G_CALLBACK(ChooseTitleFont), poPlugin);
  g_signal_connect(G_OBJECT(poGUI->wTitle), "changed", G_CALLBACK(UpdateTitle),
                   poPlugin);
  g_signal_connect(G_OBJECT(poGUI->wShowTitle), "toggled",
                   G_CALLBACK(ToggleShowTitle), poPlugin);

  gtk_button_set_label(GTK_BUTTON(poGUI->wDateFont), poConf->dateFont);
  g_signal_connect(G_OBJECT(poGUI->wDateFont), "clicked",
                   G_CALLBACK(ChooseDateFont), poPlugin);
  g_signal_connect(G_OBJECT(poGUI->wShowDate), "toggled",
                   G_CALLBACK(ToggleShowDate), poPlugin);

  gtk_button_set_label(GTK_BUTTON(poGUI->wTimeFont), poConf->timeFont);
  g_signal_connect(G_OBJECT(poGUI->wTimeFont), "clicked",
                   G_CALLBACK(ChooseTimeFont), poPlugin);
  g_signal_connect(G_OBJECT(poGUI->wShowTime), "toggled",
                   G_CALLBACK(ToggleShowTime), poPlugin);

  g_signal_connect(G_OBJECT(poGUI->wTimezone), "changed",
                   G_CALLBACK(UpdateTimezone), poPlugin);

  gtk_widget_show(dlg);
}

static gboolean clock_remote_event(XfcePanelPlugin *plugin, const gchar *name,
                                   const GValue *value, analog_clock_t *clock) {
  g_return_val_if_fail(value == NULL || G_IS_VALUE(value), FALSE);
  if (strcmp(name, "refresh") == 0) {
    if (value != NULL && G_VALUE_HOLDS_BOOLEAN(value) &&
        g_value_get_boolean(value)) {
      /* update the display */
      DisplayClock(clock);
    }
    return TRUE;
  }

  return FALSE;
}

static gboolean size_cb(XfcePanelPlugin *plugin, guint size, void *base) {
  gint frame_h, frame_v, history;
  struct analog_clock_t *clock = (struct analog_clock_t *)base;
  struct monitor_t *poMonitor = &(clock->oMonitor);

  frame_h = size - BORDER;
  frame_v = size - BORDER;

  gtk_widget_set_size_request(GTK_WIDGET(poMonitor->wClock), frame_h, frame_v);

  return TRUE;
}

static int clock_create_config_gui(GtkWidget *vbox, struct param_t *poConf,
                                   struct gui_t *gui) {
  GtkWidget *table1;
  GtkWidget *eventbox1;

  GtkWidget *grid;
  GtkWidget *wShowTitle;
  GtkWidget *wTitle;
  GtkWidget *wTitleFont;

  GtkWidget *wShowDate;
  GtkWidget *wDateFormat;
  GtkWidget *wDateFont;

  GtkWidget *wShowTime;
  GtkWidget *wTimeFormat;
  GtkWidget *wTimeFont;

  GtkWidget *hboxTZ;
  GtkWidget *wLabelTZ;
  GtkWidget *wTimezone;

  table1 = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(table1), 2);
  gtk_grid_set_row_spacing(GTK_GRID(table1), 2);
  gtk_widget_show(table1);
  gtk_box_pack_start(GTK_BOX(vbox), table1, FALSE, TRUE, 0);

  eventbox1 = gtk_event_box_new();
  gtk_widget_show(eventbox1);
  gtk_grid_attach(GTK_GRID(table1), eventbox1, 1, 2, 1, 1);
  gtk_widget_set_valign(GTK_WIDGET(eventbox1), GTK_ALIGN_CENTER);
  gtk_widget_set_halign(GTK_WIDGET(eventbox1), GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(GTK_WIDGET(eventbox1), TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(eventbox1), TRUE);

  grid = gtk_grid_new();
  gtk_widget_show(grid);

  /* Time zone */
  hboxTZ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_show(hboxTZ);

  wLabelTZ = gtk_label_new("Timezone");
  gtk_widget_show(wLabelTZ);
  gtk_box_pack_start(GTK_BOX(hboxTZ), wLabelTZ, TRUE, TRUE, 0);

  wTimezone = gtk_entry_new();
  gtk_widget_show(wTimezone);
  /* TODO: Find local timezone */
  gtk_entry_set_text(GTK_ENTRY(wTimezone), poConf->timezone);
  gtk_box_pack_start(GTK_BOX(hboxTZ), wTimezone, TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(vbox), hboxTZ, TRUE, TRUE, 0);

  /* Title */
  /* Show title check box */
  wShowTitle = gtk_check_button_new_with_mnemonic("Tit_le");
  gtk_widget_show(wShowTitle);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wShowTitle),
                               poConf->showTitle);
  gtk_grid_attach(GTK_GRID(grid), wShowTitle, 0, 0, 1, 1);

  /* Title entry box */
  wTitle = gtk_entry_new();
  gtk_widget_show(wTitle);
  gtk_entry_set_text(GTK_ENTRY(wTitle), poConf->title);
  gtk_grid_attach(GTK_GRID(grid), wTitle, 1, 0, 1, 1);

  /* Choose title font */
  wTitleFont = gtk_button_new_with_label(_("Select the title font..."));
  gtk_widget_show(wTitleFont);
  gtk_widget_set_tooltip_text(wTitleFont, "Press to change font...");
  gtk_grid_attach(GTK_GRID(grid), wTitleFont, 2, 0, 1, 1);

  /* Date */
  /* Show date check box */
  wShowDate = gtk_check_button_new_with_mnemonic("_Date");
  gtk_widget_show(wShowDate);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wShowDate), poConf->showDate);
  gtk_grid_attach(GTK_GRID(grid), wShowDate, 0, 1, 1, 1);

  /* Date format */
  wDateFormat = gtk_entry_new();
  gtk_widget_show(wDateFormat);
  gtk_entry_set_text(GTK_ENTRY(wDateFormat), poConf->dateFormat);
  gtk_widget_set_sensitive(wDateFormat, FALSE);
  gtk_grid_attach(GTK_GRID(grid), wDateFormat, 1, 1, 1, 1);

  /* Choose date font */
  wDateFont = gtk_button_new_with_label(_("Select the date font..."));
  gtk_widget_show(wDateFont);
  gtk_widget_set_tooltip_text(wDateFont, "Press to change font...");
  gtk_grid_attach(GTK_GRID(grid), wDateFont, 2, 1, 1, 1);

  /* Time */
  /* Show time option box */
  wShowTime = gtk_check_button_new_with_mnemonic("_Time");
  gtk_widget_show(wShowTime);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wShowTime), poConf->showTime);
  gtk_grid_attach(GTK_GRID(grid), wShowTime, 0, 2, 1, 1);

  /* Time format */
  wTimeFormat = gtk_entry_new();
  gtk_widget_show(wTimeFormat);
  gtk_entry_set_text(GTK_ENTRY(wTimeFormat), poConf->timeFormat);
  gtk_widget_set_sensitive(wTimeFormat, FALSE);
  gtk_grid_attach(GTK_GRID(grid), wTimeFormat, 1, 2, 1, 1);

  /* Choose time font */
  wTimeFont = gtk_button_new_with_label(_("Select the time font..."));
  gtk_widget_show(wTimeFont);
  gtk_widget_set_tooltip_text(wTimeFont, "Press to change font...");
  gtk_grid_attach(GTK_GRID(grid), wTimeFont, 2, 2, 1, 1);

  gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 0);

  gui->wShowTitle = wShowTitle;
  gui->wTitle = wTitle;
  gui->wTitleFont = wTitleFont;
  gui->wShowDate = wShowDate;
  gui->wDateFormat = wDateFormat;
  gui->wDateFont = wDateFont;
  gui->wShowTime = wShowTime;
  gui->wTimeFormat = wTimeFormat;
  gui->wTimeFont = wTimeFont;
  gui->wTimezone = wTimezone;

  return (0);
}

static void clock_construct(XfcePanelPlugin *plugin) {
  analog_clock_t *clock;

  clock = clock_create_control(plugin);

  clock_read_config(plugin, clock);

  gtk_container_add(GTK_CONTAINER(plugin), clock->oMonitor.wEventBox);

  UpdateConf(clock);

  g_signal_connect(plugin, "free-data", G_CALLBACK(clock_free), clock);
  g_signal_connect(plugin, "save", G_CALLBACK(clock_write_config), clock);
  g_signal_connect(plugin, "size-changed", G_CALLBACK(size_cb), clock);

  xfce_panel_plugin_menu_show_about(plugin);
  g_signal_connect(plugin, "about", G_CALLBACK(About), plugin);

  xfce_panel_plugin_menu_show_configure(plugin);
  g_signal_connect(plugin, "configure-plugin", G_CALLBACK(clock_create_options),
                   clock);

  g_signal_connect(plugin, "remote-event", G_CALLBACK(clock_remote_event),
                   clock);
}

XFCE_PANEL_PLUGIN_REGISTER(clock_construct)
