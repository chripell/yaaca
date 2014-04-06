/*
  Copyright 2014 Christian Pellegrin <chripell@fsfe.org>

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

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/types.h>

#include <glib.h>

#include "yaaca.h"

#include "asill.h"

static struct yaaca_ctrl *c;

struct zwoll_s {
  int res;
  int fmt;
  struct asill_s *A;
};

static char **resolutions;
static char *all_resolutions_asi120[] = {
  "1280X960",
  "960X960",
  "1024X768",
  "800X800",
  "800X640",
  "728X512",
  "640X480",
  "512X512",
  "480X320",
  "320X240",
  "2X2Bin:640X480",
  NULL,
};

static int *resolutions_x;
static int all_resolutions_x_asi120[] = {
  1280,
  960,
  1024,
  800,
  800,
  728,
  640,
  512,
  480,
  320,
  1280,
};

static int *resolutions_y;
static int all_resolutions_y_asi120[] = {
  960,
  960,
  768,
  800,
  640,
  512,
  480,
  512,
  320,
  240,
  960,
};

static int *resolutions_bin;
static int all_resolutions_bin_asi120[] = {
  1,
  1,
  1,
  1,
  1,
  1,
  1,
  1,
  1,
  1,
  2,
};

static int resolutions_n;

static char *all_formats[] = {
  "RAW16",
  "RAW8",
  NULL,
};
static int n_formats = 2;

static char *NY[] = {
  "no",
  "yes",
  NULL,
};

static char *plcks[] = {
  "25",
  "24",
  "40",
  "48",
  "96",
  "8",
  "2",
  NULL,
};

/* 
   n is 0-9 refered to ASI120MM
   n is 10-11 refered to ASI120MC

 */
static void *zwoll_cam_init(int n, struct yaaca_ctrl **ctrls, int *n_ctrls, int *maxw, int *maxh)
{
  struct asill_s *A;
  struct zwoll_s *Z;
  uint16_t model = (n >= 10) ? ASILL_ASI120MC : ASILL_ASI120MM;
  int nc;

  if (n >= 10)
    n -= 10;
  A = asill_new(model, n, 1, NULL);
  if (!A) {
    fprintf(stderr, "asill_new failed\n");
    return NULL;
  }

  *maxw = asill_get_maxw(A);
  *maxh = asill_get_maxh(A);

  c = calloc(30, sizeof(struct yaaca_ctrl));
  assert(c);
  *ctrls = c;

  resolutions = all_resolutions_asi120;
  resolutions_x = all_resolutions_x_asi120;
  resolutions_y = all_resolutions_y_asi120;
  resolutions_bin = all_resolutions_bin_asi120;

  while (*resolutions_x > *maxw || *resolutions_y > *maxh) {
    resolutions++;
    resolutions_x++;
    resolutions_y++;
    resolutions_bin++;
  }
  while (resolutions[resolutions_n])
    resolutions_n++;

#define NEW_CTRL(TYPE, NAME, MIN, MAX, TEXT, FLAGS, DEF)	\
  c[nc].type = TYPE;						\
  strcpy(c[nc].name, NAME);					\
  c[nc].min = MIN;						\
  c[nc].max = MAX;						\
  c[nc].text = TEXT;						\
  c[nc].flags = FLAGS;						\
  c[nc].def = (DEF);						\
  nc += 1

  nc = 0;

  NEW_CTRL(YAACA_ENUM, "format", 0, n_formats - 1, &all_formats[0], 0, 0); /* 0 */
  NEW_CTRL(YAACA_ENUM, "flipx", 0, 1, &NY[0], 0, 0);	/* 1 */
  NEW_CTRL(YAACA_ENUM, "flipy", 0, 1, &NY[0], 0, 0);	/* 2 */
  NEW_CTRL(YAACA_REAL, "temp", 0, 0, NULL, YAACA_RO, 0);	/* 3 */
  NEW_CTRL(YAACA_REAL, "dropped", 0, 0, NULL, YAACA_RO, 0); /* 4 */
  NEW_CTRL(YAACA_REAL, "pixel size", 0, 0, NULL, YAACA_RO, 0); /* 5 */
  NEW_CTRL(YAACA_REAL, "bayern", 0, 0, NULL, YAACA_RO, 0);	    /* 6 */
  NEW_CTRL(YAACA_STRING, "model", 0, 0, NULL, YAACA_RO, 0);    /* 7 */
  NEW_CTRL(YAACA_REAL, "color", 0, 1, NULL, YAACA_RO, 0);	    /* 8 */
  NEW_CTRL(YAACA_REAL, "start x", 0, *maxw, NULL, 0, 0);	    /* 9 */
  NEW_CTRL(YAACA_REAL, "start y", 0, *maxh, NULL, 0, 0);	    /* 10 */
  NEW_CTRL(YAACA_ENUM, "resolution", 0, resolutions_n, &resolutions[0], 0, 0); /* 11 */

  NEW_CTRL(YAACA_REAL, "anal gain", 1, 16, NULL, 0, 1); /* 12 */
  NEW_CTRL(YAACA_REAL, "digi gain", 0, 255, NULL, 0, 0x20); /* 13 */
  NEW_CTRL(YAACA_REAL, "digi gain R", 0, 255, NULL, 0, 0x20); /* 14 */
  NEW_CTRL(YAACA_REAL, "digi gain G1", 0, 255, NULL, 0, 0x20); /* 15 */
  NEW_CTRL(YAACA_REAL, "digi gain G2", 0, 255, NULL, 0, 0x20); /* 16 */
  NEW_CTRL(YAACA_REAL, "digi gain B", 0, 255, NULL, 0, 0x20);	 /* 17 */
  NEW_CTRL(YAACA_ENUM, "bias sub", 0, 1, &NY[0], 0, 1);	 /* 18 */
  NEW_CTRL(YAACA_ENUM, "row denoise", 0, 1, &NY[0], 0, 1);	 /* 19 */
  NEW_CTRL(YAACA_ENUM, "col denoise", 0, 1, &NY[0], 0, 1);	 /* 20 */
  NEW_CTRL(YAACA_ENUM, "plck mhz", 0, 7, plcks, 0, 0);	 /* 21 */
  NEW_CTRL(YAACA_REAL, "fps", 0, 0, NULL, YAACA_RO, 0); /* 22 */
  NEW_CTRL(YAACA_REAL, "exp us", 0, 1000000000, NULL, 0, 10000); /* 23 */
#if 0
  NEW_CTRL(YAACA_REAL, "exp min us", 0, 1000000000, NULL, 0, 10000); /* 24 */
  NEW_CTRL(YAACA_REAL, "exp max us", 0, 1000000000, NULL, 0, 10000); /* 25 */
#endif

  *n_ctrls = nc;
  Z = g_malloc0(sizeof(*Z));
  Z->fmt = ASILL_FMT_RAW16;
  Z->A = A;
  return Z;
}

static int par_maps[] = {
  [1] = ASILL_PAR_FLIP_X,
  [2] = ASILL_PAR_FLIP_Y,
  [12] = ASILL_PAR_ANALOG_GAIN,
  [13] = ASILL_PAR_DIGITAL_GAIN,
  [14] = ASILL_PAR_DIGITAL_GAIN_R,
  [15] = ASILL_PAR_DIGITAL_GAIN_G1,
  [16] = ASILL_PAR_DIGITAL_GAIN_G2,
  [17] = ASILL_PAR_DIGITAL_GAIN_B,
  [18] = ASILL_PAR_BIAS_SUB,
  [19] = ASILL_PAR_ROW_DENOISE,
  [20] = ASILL_PAR_COL_DENOISE,
};

static int zwoll_set(void * cam, int ctrl, double val, int autov)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  switch(ctrl) {
  case 0:
    Z->fmt = val ? ASILL_FMT_RAW8 : ASILL_FMT_RAW16;
    asill_set_wh(A, resolutions_x[Z->res], resolutions_y[Z->res], resolutions_bin[Z->res], Z->fmt);
    break;
  case 9:
    asill_set_xy(A, val, asill_get_y(A));
    break;
  case 10:
    asill_set_xy(A, asill_get_x(A), val);
    break;
  case 11:
    Z->res = val;
    asill_set_wh(A, resolutions_x[Z->res], resolutions_y[Z->res], resolutions_bin[Z->res], Z->fmt);
    break;
  case 1:
  case 2:
  case 12:
  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
  case 18:
  case 19:
  case 20:
    asill_set_int_par(A, par_maps[ctrl], val);
    break;
  case 21:
    asill_sel_pclk(A, val);
    break;
  case 23:
    asill_set_exp_us(A, val);
  default:
    return -1;
  }
  return 0;
}

static double zwoll_get(void * cam, int ctrl)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  switch(ctrl) {
  case 0:
    return asill_get_format(A);
  case 3:
    return asill_get_temp(A);
  case 8:
    return asill_is_color(A);
  case 9:
    return asill_get_x(A);
  case 10:
    return asill_get_y(A);
  case 11:
    return Z->res;
  case 1:
  case 2:
  case 12:
  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
  case 18:
  case 19:
  case 20:
    return asill_get_int_par(A, par_maps[ctrl]);
  case 21:
    return asill_get_pclk(A);
  case 22:
    return asill_get_fps(A);
  case 23:
    return asill_get_exp_us(A);
  case 24:
    return asill_get_min_exp_us(A);
  case 25:
    return asill_get_max_exp_us(A);
  }
  return 0;
}

static char * zwoll_get_str(void *cam, int ctrl)
{
  return "ZWO LL";
}

static void zwoll_close(void *cam_)
{
}

static void zwoll_get_pars(void *cam, int *w, int *h, int *format, int *Bpp, int *sx, int *sy)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  if (w)
    *w = asill_get_w(A);
  if (h)
    *h = asill_get_h(A);
  if (format)
    *format = asill_get_format(A);
  if (Bpp)
    *Bpp = 2;
  if (sx)
    *sx = asill_get_x(A);
  if (sy)
    *sy = asill_get_y(A);
}

static void zwoll_pulse (void *cam, int dir, int n)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  asill_pulse(A, dir, n);
}

static void zwoll_load(void *cam)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;
  int i;

  if (asill_load_pars(A) == 0) {
    Z->fmt = c[0].def = asill_get_format(A) == ASILL_FMT_RAW16 ? 0 : 1;
    for(i = 1; i <= 2; i++) 
      c[i].def = asill_get_int_par(A, par_maps[i]);
    c[9].def = asill_get_x(A);
    c[10].def = asill_get_y(A);

    i = 0;
    while (resolutions[i]) {
      if (resolutions_x[i] == asill_get_w(A) && resolutions_y[i] == asill_get_h(A) && resolutions_bin[i] == asill_get_bin(A)) {
	break;
      }
      i++;
    }
    if (resolutions[i])
      Z->res = c[11].def = i;
    else
      Z->res = c[11].def = 0;

    for(i = 12; i <= 20; i++) 
      c[i].def = asill_get_int_par(A, par_maps[i]);  
    c[21].def = asill_get_pclk(A);
    c[23].def = asill_get_exp_us(A);
  }
}

static void zwoll_save(void *cam)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  asill_save_pars(A);
}

static int zwoll_maxw(void *cam)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  return asill_get_maxw(A);
}

static int zwoll_maxh(void *cam)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  return asill_get_maxh(A);
}

static int zwoll_isbin(void *cam, int res)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  return asill_get_bin(A);
}

uint8_t *zwoll_get_buffer (void *cam, int done)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  if (done) {
    asill_done_buffer(A);
  }
  return asill_get_buffer(A);
}

static void zwoll_run(void * cam, int r)
{
}

static int zwoll_save_path(void *cam, const char *path)
{
  struct zwoll_s *Z = (struct zwoll_s *) cam;
  struct asill_s *A = Z->A;

  return asill_set_save(A, path);
}

struct yaaca_cam_s ZWO_CAMLL = {
  "ZWO LL Asi Camera",
  zwoll_cam_init,
  zwoll_close,
  zwoll_set,
  zwoll_get,
  zwoll_get_str,
  zwoll_run,
  zwoll_get_pars,
  zwoll_pulse,
  zwoll_save,
  zwoll_load,
  zwoll_maxw,
  zwoll_maxh,
  zwoll_isbin,
  zwoll_get_buffer,
  zwoll_save_path,
};

