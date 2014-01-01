/*
  Copyright 2013 Christian Pellegrin <chripell@fsfe.org>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see http://www.gnu.org/licenses/.
 */

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <png.h>

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>

#include "yaaca.h"

#ifndef MAX_PATH
#define MAX_PATH 255
#endif

static struct yaaca_ctrl *ctrls;
/* TODO: support more cameras and get type from command line */
extern struct yaaca_cam_s ZWO_CAM;
struct yaaca_cam_s *ccam = &ZWO_CAM;
static void * cam;

static GtkWidget **ctrl_val;
static GtkWidget **ctrl_auto;
static GtkWidget *im;
static int imbw, imbh;
static GdkPixbuf *imb;
static GtkWidget *status2;

static GtkWidget *zoom_im, *cross_pos, *zoom_factor, *m_box;
static GtkWidget *m_text, *m_save;
static FILE  *f_m_save = NULL;
static GdkPixbuf *zoom_imb;
static int zoom_imbw = 300, zoom_imbh = 300;
static int zoom_f;

static GtkWidget *histo_min, *histo_max, *histo_im;
static GdkPixbuf *histo_imb;
static int histo_on;

static int cimg_present = 0;
static int cimg_w;
static int cimg_h;
static int cimg_format;
static int cimg_bpp;
static unsigned char *cimg;

static GtkWidget *w_capture_path, *w_do_capture, *w_capture_raw, *w_capture_compress;
static char capture_path[MAX_PATH];
static int do_capture;
static int capture_raw, capture_compress;

static int gain, exposure, format, resolution;

int writeImage(const char* filename, int width, int height, int bpp, int color, unsigned char *buffer, int compress)
{
   int code = 0;
   FILE *fp;
   png_structp png_ptr;
   png_infop info_ptr = NULL;
   int y;

   fp = fopen(filename, "wb");
   if (fp == NULL) {
     fprintf(stderr, "Could not open file %s for writing\n", filename);
     code = 1;
     goto finalise;
   }

   png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
   if (png_ptr == NULL) {
     fprintf(stderr, "Could not allocate write struct\n");
     code = 1;
     goto finalise;
   }

   if (!compress) {
     png_set_compression_level(png_ptr, 0);
     png_set_filter(png_ptr, 0, PNG_FILTER_NONE);
   }

   info_ptr = png_create_info_struct(png_ptr);
   if (info_ptr == NULL) {
      fprintf(stderr, "Could not allocate info struct\n");
      code = 1;
      goto finalise;
   }

   if (setjmp(png_jmpbuf(png_ptr))) {
     fprintf(stderr, "Error during png creation\n");
     code = 1;
     goto finalise;
   }

   png_init_io(png_ptr, fp);

   png_set_IHDR(png_ptr, info_ptr, width, height,
		bpp, color ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

   png_write_info(png_ptr, info_ptr);

   for (y=0 ; y<height ; y++) {
     png_write_row(png_ptr, &buffer[y * width * (bpp / 8) * (color ? 3 : 1)]);
   }

   png_write_end(png_ptr, NULL);

finalise:
   if (fp != NULL) fclose(fp);
   if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
   if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);

   return code;
}

static gboolean delete_event( GtkWidget *widget,
                              GdkEvent  *event,
                              gpointer   data )
{
  return FALSE;
}

static void destroy( GtkWidget *widget,
                     gpointer   data )
{
    gtk_main_quit ();
}

static int is_cross, cross_x, cross_y;
static int is_cross_saved, ocross_x, ocross_y;
static unsigned char *save_row, *save_col;

static void show_cross(int draw, int restore)
{
  int i;
  unsigned char *o = gdk_pixbuf_get_pixels(imb);
#define O(X,Y) &o[3 * ((Y) * imbw + (X))]

  if (restore && is_cross_saved) {
    for(i = 0; i < imbw; i++) {
      memcpy(O(i, ocross_y), &save_row[i * 3], 3);
    }
    for(i = 0; i < imbh; i++) {
      memcpy(O(ocross_x, i), &save_col[i * 3], 3);
    }
  }

  if (draw && is_cross) {
    ocross_x = cross_x;
    ocross_y = cross_y;
    for(i = 0; i < imbw; i++) {
      memcpy(&save_row[i * 3], O(i, ocross_y), 3);
    }
    for(i = 0; i < imbh; i++) {
      memcpy(&save_col[i * 3], O(ocross_x, i), 3);
    }
    is_cross_saved = 1;
    for(i = 0; i < imbw; i++) {
      (O(i, cross_y))[0] = 255;
      (O(i, cross_y))[1] = 0;
      (O(i, cross_y))[2] = 0;
    }
    for(i = 0; i < imbh; i++) {
      (O(cross_x, i))[0] = 255;
      (O(cross_x, i))[1] = 0;
      (O(cross_x, i))[2] = 0;
    }
  }

#undef O
}

static void update_cross_pos(void)
{
  char b[50];

  snprintf(b, 50, " (%d,%d) ", cross_x, cross_y);
  if (cross_pos)
    gtk_label_set_text(GTK_LABEL(cross_pos), b);
}

static void update_cross( GtkWidget *w, gpointer p )
{
  is_cross = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
  show_cross(is_cross, !is_cross);
  gtk_image_set_from_pixbuf(GTK_IMAGE(im), imb);
}

static void histo_cb( GtkWidget *w, gpointer p )
{
  histo_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
  if (histo_on)
    gtk_widget_show(histo_im);
  else
    gtk_widget_hide(histo_im);
}

static void m_cb( GtkWidget *w, gpointer p )  
{
  int m_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));

  if (m_on && !f_m_save)
    f_m_save = fopen("yaaca_pos.log", "a");
  else if (!m_on && f_m_save) {
    fclose(f_m_save);
    f_m_save = NULL;
  }
}

static void setup_im(nimg_w, nimg_h)
{
  if (nimg_w != imbw || nimg_h != imbh) {
    GdkPixbuf *oimb;

    oimb = imb;
    imbw = nimg_w;
    imbh = nimg_h;
    imb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, imbw, imbh);
    if (oimb)
      g_object_unref(oimb);
    cross_x = imbw / 2;
    cross_y = imbh / 2;
    update_cross_pos();
  }
}

static void update_do_capture( GtkWidget *w, gpointer p )
{
  const char *dir = gtk_entry_get_text(GTK_ENTRY(w_capture_path));

  mkdir(dir, 0777);
  snprintf(capture_path, MAX_PATH, "%s", dir);
  capture_raw = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_capture_raw));
  capture_compress = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w_capture_compress));
  do_capture = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
}

static void set_w_int(GtkWidget *w, int v)
{
  char b[200];

  snprintf(b, 200, "%d", v);
  gtk_entry_set_text(GTK_ENTRY(w), b);
}

static void update_ctrl( GtkWidget *w, int n )
{
  int autov = 0;
  double val = 0;
  int cx, cy;
  int sx = 0, sy = 0;

  if (ctrls[n].flags & YAACA_AUTO) {
    autov = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctrl_auto[n]));
  }
  if (ctrls[n].type == YAACA_ENUM) {
    val = gtk_option_menu_get_history(GTK_OPTION_MENU(ctrl_val[n]));
  }
  else {
    val = atof(gtk_entry_get_text(GTK_ENTRY(ctrl_val[n])));
  }
  switch(n) {			/* note: it's specific to zwo */
  case 0:
    format = val;
    break;
  case 12:
    gain = autov ? 0: val;
    break;
  case 13:
    exposure = autov ? 0 : val;
    break;
  case 11:
    resolution = val;
    break;
  }
  cx = cross_x;
  cy = cross_y;
  ccam->set(cam, n,
	    val,
	    autov);
  /* note: it's zwo specific adjustment of ROI centered on the crosshair */
  if (n == 11) {
    if (resolution == 19) {
      set_w_int(ctrl_val[9], 0);
      set_w_int(ctrl_val[10], 0);
    }
    else {
      int w,h;
      int osx, osy;

      ccam->get_pars(cam, &w, &h, NULL, NULL, &osx, &osy);
      sx = cx + osx - w/2;
      if (sx < 0) sx = 0;
      if (sx + w >= 1280) sx = 1280 - w;
      ccam->set(cam, 9, sx, 0);
      set_w_int(ctrl_val[9], sx);
      sy = cy + osy - h/2;
      if (sy < 0) sy = 0;
      if (sy + h >= 960) sy = 960 - h;
      ccam->set(cam, 10, sy, 0);
      set_w_int(ctrl_val[10], sy);
      //fprintf(stderr, "DELME %d-%d %d-%d %d-%d %d-%d\n", w, h, sx, sy, osx, osy, cx, cy);
    }
  }
}

static void update_zoom( GtkWidget *w, int n)
{
  zoom_f = atoi(gtk_entry_get_text(GTK_ENTRY(w)));
  if (zoom_f) {
    gtk_widget_show(zoom_im);
    gtk_widget_show(m_box);
  }
  else {
    gtk_widget_hide(zoom_im);
    gtk_widget_hide(m_box);
  }
}

static int get_raw8_pix(int i, int j, int w)
{
  int y = (j / 2) * 2;
  int x = (i / 2) * 2;

  return cimg[y * cimg_w + x + w];
}

static int get_raw16_pix(int i, int j, int w)
{
  int y = (j / 2) * 2;
  int x = (i / 2) * 2;
  unsigned short *im16 = (unsigned short *) cimg;

  return im16[y * cimg_w + x + w];
}

static gboolean redraw_image(gpointer user_data)
{
  int i,j;
  char stat[200];

  snprintf(stat, 200, "T: %.1f, Dropped: %.0f", ccam->get(cam, 3), ccam->get(cam, 4));
  gtk_label_set_text(GTK_LABEL(status2), stat);

  if (cimg_present) {
    int hmin, hmax;
#define PXY(x, y, c)  o[3*((x) + (y) * cimg_w) + (c)]
    unsigned char *o;
#define HXY(x, y, c)  ho[3*((x) + (y) * 256) + (c)]
    unsigned char *ho = gdk_pixbuf_get_pixels(histo_imb);

    hmin = atoi(gtk_entry_get_text(GTK_ENTRY(histo_min)));
    hmax = atoi(gtk_entry_get_text(GTK_ENTRY(histo_max)));

    //fprintf(stderr, "DRAW %d %d\n", cimg_format, YAACA_FMT_RGB24);
    setup_im(cimg_w, cimg_h);
    o = gdk_pixbuf_get_pixels(imb);
    if (cimg_format == YAACA_FMT_RGB24) {
#if 1
      memcpy(o, cimg, cimg_w * cimg_h * 3);
#else
      for (i = 0; i < cimg_w * cimg_h; i++) {
	o[3*i + 0] = cimg[3*i + 2];
	o[3*i + 1] = cimg[3*i + 1];
	o[3*i + 2] = cimg[3*i + 0];
      }
#endif
    }
    else if (cimg_format == YAACA_FMT_Y8) {
      for (i = 0; i < cimg_w * cimg_h; i++) {
	o[3*i + 0] = cimg[i];
	o[3*i + 1] = cimg[i];
	o[3*i + 2] = cimg[i];
      }
    }
    else if (cimg_format == YAACA_FMT_RAW8) { 
      for(j = 0; j < cimg_h; j++)
	for(i = 0; i < cimg_w; i++) {
	  int z = 3 * (j * cimg_w + i);

	  /* TODO: serious debayer, gamma */
	  o[z + 0] = get_raw8_pix(i, j, 1);
	  o[z + 1] = get_raw8_pix(i, j, 0);
	  o[z + 2] = get_raw8_pix(i, j, 2);
	}
    }
    else if (cimg_format == YAACA_FMT_RAW16) {
      for(j = 0; j < cimg_h; j++)
	for(i = 0; i < cimg_w; i++) {
	  int z = 3 * (j * cimg_w + i);

	  /* TODO: serious debayer, gamma */
	  o[z + 0] = get_raw16_pix(i, j, 1);
	  o[z + 1] = get_raw16_pix(i, j, 0);
	  o[z + 2] = get_raw16_pix(i, j, 2);
	}
    }

    if (histo_on) {
      unsigned char zero[] = {0, 0, 0};
      unsigned char one[] = {0xff, 0xff, 0xff};
      int i, j, mi;
      double h[256], mm = 0.0;

      for(i = 0; i < 256; i++) h[i] = 0.0;
      mi = 0;
      for(i = 0; i < cimg_w * cimg_h * 3; i++) {
	h[o[i]] += 1.0;
	if (o[i] > mi) mi = o[i];
      }
      h[mi] = 0.0;		/* revisit this */
      for(i = 0; i < 256; i++) {
	if (h[i] > mm) mm = h[i];
      }
      for(i = 0; i < 256; i++) {
	double mmm = h[i] / mm * 100.0;

	for(j = 0; j < 100; j++)
	  if (j < mmm)
	    memcpy(&HXY(i, (99-j), 0), &zero, 3);
	  else
	    memcpy(&HXY(i, (99-j), 0), &one, 3);
      }
    }

    if (hmin < 0) hmin = 0;
    if (hmin > 254) hmin = 254;
    if (hmax < (hmin + 1)) hmax = hmin + 1;
    if (hmax > 255) hmax = 255;
    if (hmin != 0 || hmax != 255) {
      int i, j;
      double f = 255.0 / (hmax - hmin);

      for (i = 0; i < cimg_w * cimg_h * 3; i++) {
	double x = o[i];

	x = (x - hmin) * f;
	if (x < 0) x = 0;
	if (x > 255) x = 255;
	o[i] = x;
      }
      if (histo_on) {
	for(i = 0; i < 100; i++) {
	  for (j = hmin; j <= hmax; j++) {
	    HXY(j, i, 0) = 255;
	    HXY(j, i, 0) = 255;
	  }
	}
      }
    }

    if (histo_on) {
      gtk_image_set_from_pixbuf(GTK_IMAGE(histo_im), histo_imb);
    }

    if (zoom_f) {
      int nx = zoom_imbw / zoom_f;
      int ny = zoom_imbh / zoom_f;
      int i, j, c;
#define OXY(x, y, c)  oo[3*((x) + (y) * zoom_imbw) + (c)]
      unsigned char *oo = gdk_pixbuf_get_pixels(zoom_imb);
      char m_t[200];
      double xm = 0, ym = 0, xd = 0, yd = 0, s = 0;
      int m_m = 0, m_mm = 0;

      for(i = -nx/2 ; i < nx/2; i++)
	for(j = -ny/2 ; j < ny/2; j++) {
	  int fx = cross_x + i;
	  int fy = cross_y + j;

	  if (fx >= 0 && fx < imbw && fy >= 0 && fy < imbh) {
	    int dx, dy;

	    for(dx = 0; dx < zoom_f; dx++)
	      for(dy = 0; dy < zoom_f; dy++) {
		int gx = zoom_imbw / 2 + i * zoom_f + dx;
		int gy = zoom_imbh / 2 + j * zoom_f + dy;

		if (gx >= 0 && gx < zoom_imbw && gy >= 0 && gy < zoom_imbh)
		  memcpy(&OXY(gx, gy, 0), &PXY(fx, fy, 0), 3);
	      }
	  }
	}
      gtk_image_set_from_pixbuf(GTK_IMAGE(zoom_im), zoom_imb);
      for(i = 0; i < zoom_imbw; i++)
	for(j = 0; j < zoom_imbh; j++)
	  for(c = 0; c < 3; c++) {
	    int v = OXY(i, j, c);

	    xm = xm + i * v;
	    ym = ym + j * v;
	    s += v;
	    if (v > m_m)
	      m_m = v;
	  }
      xm /= s;
      ym /= s;
      for(i = 0; i < zoom_imbw; i++)
	for(j = 0; j < zoom_imbh; j++)
	  for(c = 0; c < 3; c++) {
	    int v = OXY(i, j, c);
	    double xx = i - xm;
	    double yy = j - ym;

	    xd += xx * xx * v;
	    yd += yy * yy * v;
	  }
      xd = sqrt(xd / s);
      yd = sqrt(yd / s);
      for(i = -1; i < 2; i++)
	for(j = -1; j < 2; j++) {
	  int x = xm + i;
	  int y = ym + j;

	  if (x >= 0 && x < zoom_imbw && y >= 0 && y < zoom_imbh) {
	    for(c = 0; c < 3; c++) {
	      int v = OXY(x, y, c);

	      if (v > m_mm)
		m_mm = v;
	    }
	  }
	}
      snprintf(m_t, 200, "(%5.1f,%5.1f) (%5.1f,%5.1f) (%3d,%3d)", xm, ym, xd, yd, m_m, m_mm);
      gtk_label_set_text(GTK_LABEL(m_text), m_t);
      if (f_m_save) {
	static struct timeval last_save;
	struct timeval now;

	gettimeofday(&now, NULL);
	if (last_save.tv_sec != now.tv_sec) {
	  fprintf(f_m_save, "%ld.%06ld %s\n", now.tv_sec, now.tv_usec, m_t);
	  last_save = now;
	}
      }
    }

    show_cross(1, 0);
    gtk_image_set_from_pixbuf(GTK_IMAGE(im), imb);
    cimg_present = 0;
  }
  return FALSE;
}
#undef PXY
#undef OXY
#undef HXY

static gboolean mpress( GtkWidget *widget, GdkEventButton *event, gpointer data )
{
  int rx = 0, ry = 0;
  GtkAllocation aim;

  gtk_widget_get_allocation(im, &aim);
  if (aim.width > imbw)
    rx = (aim.width - imbw) / 2;
  if (aim.height > imbh)
    ry = (aim.height - imbh) / 2;
  cross_x = event->x - rx;
  if (cross_x < 0) cross_x = 0;
  if (cross_x >= imbw) cross_x = imbw - 1;
  cross_y = event->y - ry;
  if (cross_y < 0) cross_y = 0;
  if (cross_y >= imbh) cross_y = imbh - 1;
  //fprintf(stderr, "%f %f, %d %d\n", event->x, event->y, cross_x, cross_y);
  update_cross_pos();
  if (is_cross) {
    show_cross(1, 1);
    gtk_image_set_from_pixbuf(GTK_IMAGE(im), imb);
  }
  return TRUE;
}

static GtkWidget *pulse_n;

static void pulse_press( GtkWidget *widget, gpointer data )
{
  long dir = (long) data;

  ccam->pulse(dir, atoi(gtk_entry_get_text(GTK_ENTRY(pulse_n))));
}

static GtkWidget *create_ctrl(struct yaaca_ctrl *ctrl, int i, GtkWidget **ctrl_val, GtkWidget **ctrl_auto)
{
  gchar fname[200];
  GtkWidget *box, *label;

  box = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (box);

  if (ctrl->type == YAACA_ENUM) {
    snprintf(fname, 200, "%s:", ctrl->name);
  }
  else {
    snprintf(fname, 200, "%s (%.0f-%.0f):", ctrl->name, ctrl->min, ctrl->max);
  }
  label = gtk_label_new(fname);
  gtk_box_pack_start(GTK_BOX (box), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  if (ctrl->type == YAACA_ENUM) {
    gchar **t;
    int j;
    GtkWidget *menu;

    menu = gtk_menu_new();
    j = 0;
    t = ctrl->text;
    while(t[j]) {
      GtkWidget *item = gtk_menu_item_new_with_label(t[j]);

      gtk_widget_show (item);
      gtk_menu_shell_append (GTK_MENU_SHELL(menu), item);
      j++;
    }
    gtk_widget_show (menu);
    *ctrl_val = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(*ctrl_val), menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(*ctrl_val), ctrl->def);
    g_signal_connect (*ctrl_val, "changed",
		      G_CALLBACK (update_ctrl), (gpointer)(long) i);
    gtk_box_pack_start(GTK_BOX (box), *ctrl_val, FALSE, FALSE, 0);
    gtk_widget_show (*ctrl_val);
  }
  else {
    char b[200];

    *ctrl_val = gtk_entry_new();
    sprintf(b, "%d", (int) ctrl->max);
    gtk_entry_set_width_chars (GTK_ENTRY(*ctrl_val), strlen(b));
    gtk_entry_set_max_length (GTK_ENTRY(*ctrl_val), strlen(b));
    g_signal_connect (*ctrl_val, "activate",
		      G_CALLBACK (update_ctrl), (gpointer)(long) i);
    gtk_box_pack_start(GTK_BOX (box), *ctrl_val, TRUE, TRUE, 0);
    snprintf(fname, 200, "%.0f", ctrl->def);
    gtk_entry_set_text(GTK_ENTRY(*ctrl_val), fname);
    gtk_widget_show (*ctrl_val);

    if (ctrl->flags & YAACA_AUTO) {
      label = gtk_label_new("->");
      gtk_widget_show(label);
      gtk_box_pack_start(GTK_BOX (box), label, FALSE, FALSE, 0);
      *ctrl_auto = gtk_check_button_new_with_label (NULL);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(*ctrl_auto), ctrl->def_auto);
      g_signal_connect (*ctrl_auto, "toggled",
			G_CALLBACK (update_ctrl), (gpointer)(long) i);
      gtk_box_pack_start(GTK_BOX (box), *ctrl_auto, FALSE, FALSE, 0);
      gtk_widget_show (*ctrl_auto);
    }
  }
  return box;
}

static GtkWidget *create_label(char *c)
{
  GtkWidget *l;

  l = gtk_label_new(c);
  gtk_widget_show (l); 
  return l;
}

static GtkWidget *create_entry(char *c, GtkWidget **e, char *def, GCallback cb, int len, int mlen)
{
  GtkWidget *b, *l;

  b = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (b);
  if (c) {
    l = gtk_label_new(c);
    gtk_widget_show (l); 
    gtk_box_pack_start(GTK_BOX (b), l, FALSE, FALSE, 0);
  }
  *e = gtk_entry_new();
  gtk_entry_set_width_chars (GTK_ENTRY(*e), len);
  if (mlen > 0)
    gtk_entry_set_max_length (GTK_ENTRY(*e), mlen);
  gtk_widget_show (*e);   
  gtk_box_pack_start(GTK_BOX (b), *e, TRUE, TRUE, 0);
  if (def)
    gtk_entry_set_text(GTK_ENTRY(*e), def);
  if (cb)
    g_signal_connect (*e, "activate", cb, NULL);
  return b;
}

static GtkWidget *create_check(char *c, GCallback cb)
{
  static GtkWidget *e;

  e = gtk_check_button_new_with_label (c);
  gtk_widget_show (e);
  if (cb) {
    g_signal_connect (e, "toggled",
		      G_CALLBACK (cb), NULL);
  }
  return e;
}

static GtkWidget *create_button(char *c, int w, int h, GCallback cb)
{
  static GtkWidget *b;

  b = gtk_button_new_with_label (c);
  if (cb) {
    g_signal_connect (b, "clicked", cb, (gpointer) 0);
  }
  gtk_widget_show(b);
  gtk_widget_set_size_request(b, w, h);
  return b;
}

int main(int argc, char *argv[])
{
  int n_ctrls;
  int maxw, maxh;
  GtkWidget *window, *top_pane, *scrolled_window,
    *scrolled_window1, *right, *status1;
  GtkWidget *capture_box;
  GtkWidget *evbox, *box;
  GtkWidget *pulse_table;
  GtkWidget *cross_box, *zoom_box;
  GtkWidget *histo_box, *histo1_box;

  g_thread_init (NULL);
  gtk_init (&argc, &argv);

  cam = ccam->init(0, &ctrls, &n_ctrls, &maxw, &maxh);
  if (!cam) {
    fprintf(stderr, "CAM not found!\n");
    exit(1);
  }
  cimg =  malloc(maxw * maxh * 3);
  save_row = malloc(maxw * 3);
  save_col = malloc(maxh * 3);
  cimg_present = 0;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "YAACA");
  gtk_window_resize (GTK_WINDOW (window), 900, 600);
  g_signal_connect (window, "delete-event",
		    G_CALLBACK (delete_event), NULL);
  g_signal_connect (window, "destroy",
		    G_CALLBACK (destroy), NULL);

  top_pane = gtk_hbox_new(FALSE, 2);
  gtk_container_add (GTK_CONTAINER (window), top_pane);
  gtk_widget_show (top_pane);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX (top_pane), scrolled_window, TRUE, TRUE, 0);
  gtk_widget_show (scrolled_window);

  evbox = gtk_event_box_new();
  gtk_event_box_set_above_child(GTK_EVENT_BOX(evbox), TRUE);
  gtk_widget_add_events(evbox, GDK_BUTTON_PRESS_MASK);
  g_signal_connect (evbox, "button_press_event", G_CALLBACK (mpress), NULL);
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolled_window), evbox);
  gtk_widget_show (evbox);

  im = gtk_image_new();
  setup_im(maxw, maxh);		/* assume we start with maximum resolution */
  gtk_image_set_from_pixbuf(GTK_IMAGE(im), imb);
  gtk_container_add(GTK_CONTAINER(evbox), im);
  gtk_widget_show (im);

  scrolled_window1 = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_size_request(scrolled_window1, 360, -1);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window1),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX (top_pane), scrolled_window1, FALSE, FALSE, 0);
  gtk_widget_show (scrolled_window1);

  right = gtk_vbox_new (FALSE, 0);
  gtk_widget_set_size_request(right, 340, -1);
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolled_window1), right);
  gtk_widget_show (right);

  status1 = create_label(ccam->get_str(cam, 7));
  gtk_box_pack_start(GTK_BOX (right), status1, FALSE, FALSE, 0);

  status2 = create_label("Loading");
  gtk_box_pack_start(GTK_BOX (right), status2, FALSE, FALSE, 0);

  ctrl_val = calloc(n_ctrls, sizeof(GtkWidget *));
  ctrl_auto = calloc(n_ctrls, sizeof(GtkWidget *));

#if 1
#define ADD(B, i) gtk_box_pack_start(GTK_BOX (B), create_ctrl(&ctrls[i], i, &ctrl_val[i], &ctrl_auto[i]), B==box, B==box, B==box ? 0 : 1)

  box = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (box);
  ADD(box, 0);
  ADD(box, 1);
  ADD(box, 2);
  gtk_box_pack_start(GTK_BOX (right), box, FALSE, FALSE, 1);

  box = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (box);
  ADD(box, 9);
  ADD(box, 10);
  gtk_box_pack_start(GTK_BOX (right), box, FALSE, FALSE, 1);
  
  ADD(right, 11);
  ADD(right, 13);

  box = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (box);
  ADD(box, 12);
  ADD(box, 14);
  gtk_box_pack_start(GTK_BOX (right), box, FALSE, FALSE, 1);

  box = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (box);
  ADD(box, 15);
  ADD(box, 16);
  gtk_box_pack_start(GTK_BOX (right), box, FALSE, FALSE, 1);

  box = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (box);
  ADD(box, 17);
  ADD(box, 18);
  gtk_box_pack_start(GTK_BOX (right), box, FALSE, FALSE, 1);

#undef ADD
#else
  for(i = 0; i < n_ctrls; i++) {
    if ((ctrls[i].flags & (YAACA_OFF | YAACA_RO)) == 0) {
      GtkWidget *box = create_ctrl(&ctrls[i], i, &ctrl_val[i], &ctrl_auto[i]);

      gtk_box_pack_start(GTK_BOX (right), box, FALSE, FALSE, 0);
    }
  }
#endif

  /* zwo specific */
  format = ctrls[0].def;
  gain = ctrls[12].def_auto ? 0 : ctrls[12].def;
  exposure = ctrls[13].def_auto ? 0 : ctrls[13].def;
  resolution = ctrls[11].def;

  capture_box = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start(GTK_BOX (right), capture_box, FALSE, FALSE, 1);
  gtk_widget_show (capture_box);
  gtk_box_pack_start(GTK_BOX (capture_box), create_entry("dir:", &w_capture_path, "/tmp/", NULL, 10, 0), TRUE, TRUE, 0);
  box = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (box);
  gtk_box_pack_start(GTK_BOX (box), (w_do_capture = create_check("capture", G_CALLBACK (update_do_capture))), TRUE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (box), (w_capture_raw = create_check("PBM", NULL)), TRUE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (box), (w_capture_compress = create_check("compress", NULL)), TRUE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (capture_box), box, TRUE, TRUE, 0);

  pulse_table = gtk_table_new(3, 3, TRUE);
  gtk_table_attach_defaults (GTK_TABLE (pulse_table), create_button("N", 30, -1, G_CALLBACK (pulse_press)), 1, 2, 0, 1);
  gtk_table_attach_defaults (GTK_TABLE (pulse_table), create_button("S", 30, -1, G_CALLBACK (pulse_press)), 1, 2, 2, 3);
  gtk_table_attach_defaults (GTK_TABLE (pulse_table), create_button("E", 30, -1, G_CALLBACK (pulse_press)), 0, 1, 1, 2);
  gtk_table_attach_defaults (GTK_TABLE (pulse_table), create_button("W", 30, -1, G_CALLBACK (pulse_press)), 2, 3, 1, 2);
  gtk_table_attach_defaults (GTK_TABLE (pulse_table), create_entry(NULL, &pulse_n, "1", NULL, 4, 4), 1, 2, 1, 2);
  gtk_box_pack_start(GTK_BOX (right), pulse_table, FALSE, FALSE, 1);
  gtk_widget_show(pulse_table);

  zoom_box = gtk_vbox_new (FALSE, 0);
  gtk_widget_show(zoom_box);
  cross_box = gtk_hbox_new (FALSE, 0);
  gtk_widget_show(cross_box);
  gtk_box_pack_start(GTK_BOX (cross_box), create_check("show cross", G_CALLBACK (update_cross)), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (cross_box), (cross_pos = create_label("(0,0)")), FALSE, FALSE, 0);
  update_cross_pos();
  gtk_box_pack_start(GTK_BOX (cross_box), create_entry("zoom (0 off):", &zoom_factor, "0", G_CALLBACK(update_zoom), 1, 1), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (zoom_box), cross_box, FALSE, FALSE, 0);
  zoom_im = gtk_image_new();
  zoom_imb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, zoom_imbw, zoom_imbh);
  gtk_image_set_from_pixbuf(GTK_IMAGE(zoom_im), zoom_imb);
  gtk_box_pack_start(GTK_BOX (zoom_box), zoom_im, FALSE, FALSE, 0);
  m_box = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start(GTK_BOX (m_box), (m_text = create_label("(0,0)(0,0)(0,0)")), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (m_box), (m_save = create_check("save", G_CALLBACK(m_cb))), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (zoom_box), m_box, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (right), zoom_box, FALSE, FALSE, 1);

  histo1_box = gtk_vbox_new (FALSE, 0);
  gtk_widget_show(histo1_box);
  histo_box = gtk_hbox_new (FALSE, 0);
  gtk_widget_show(histo_box);
  gtk_box_pack_start(GTK_BOX (histo_box), create_check("historam ", G_CALLBACK(histo_cb)), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (histo_box), create_entry("min:", &histo_min, "0", NULL, 3, 3), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (histo_box), create_entry("max:", &histo_max, "255", NULL, 3, 3), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX (histo1_box), histo_box, FALSE, FALSE, 0);  
  histo_im = gtk_image_new();
  histo_imb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 256, 100);
  gtk_image_set_from_pixbuf(GTK_IMAGE(histo_im), histo_imb);
  gtk_box_pack_start(GTK_BOX (histo1_box), histo_im, FALSE, FALSE, 0);  
  gtk_box_pack_start(GTK_BOX (right), histo1_box, FALSE, FALSE, 1);  

  gtk_widget_show (window);
  ccam->run(cam, 1);
  gtk_main ();

  return 0;
}

int yaac_new_image(unsigned char *data, int w, int h, int format, int bpp)
{
  int i;

  //fprintf(stderr, "NI\n");
  if (format == YAACA_FMT_RGB24) {
    for(i = 0; i < cimg_w * cimg_h; i++) {
      unsigned char t = data[3*i];

      data[3*i + 0] = data[3*i + 2];
      data[3*i + 2] = t;
    }
  }
  else if (format == YAACA_FMT_RAW16) {
    for(i = 0; i < 2 *cimg_w * cimg_h; i += 2) {
      unsigned char t = data[i];

      data[i] = data[i + 1];
      data[i + 1] = t;
    }
  }
  if (!cimg_present) {
    //fprintf(stderr, "SCH %d\n", bpp);
    memcpy(cimg, data, w * h * bpp);
    cimg_w = w;
    cimg_h = h;
    cimg_format = format;
    cimg_bpp = bpp;
    cimg_present = 1;
    g_timeout_add(1, redraw_image, NULL);
  }
  if (do_capture) {
    char fname[MAX_PATH];
    struct timeval tv;

    gettimeofday(&tv, NULL);
    if (capture_raw) {
      FILE *f;

      snprintf(fname, MAX_PATH, "%s/%010ld_%06ld:%d_%d_%d.%s", capture_path, tv.tv_sec, tv.tv_usec, format, gain, exposure,
	       format == YAACA_FMT_RGB24 ? "ppm" : "pgm");
      f = fopen(fname, "w");
      if (f) {
	fprintf(f, "P%d\n%d %d\n%d\n",
		format == YAACA_FMT_RGB24 ? 6 : 5,
		w, h,
		bpp == 2 ? 65535 : 255);
	fwrite(data, w * h * bpp, 1, f);
	fclose(f);
      }
    }
    else {
      snprintf(fname, MAX_PATH, "%s/%010ld_%06ld:%d_%d_%d.png", capture_path, tv.tv_sec, tv.tv_usec, format, gain, exposure);
      writeImage(fname, w, h, format == YAACA_FMT_RAW16 ? 16 : 8, format == YAACA_FMT_RGB24, data, capture_compress);
    }
    fprintf(stderr, "written %s\n", fname);
  }
  return 0;
}
