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

#include <glib.h>

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <pwd.h>

#ifndef MAX_PATH
#define MAX_PATH 255
#endif

#include "ASICamera.h"

#include "yaaca.h"

static char **resolutions;
static char *all_resolutions_asi120[] = {
  "1280X960",
  "1280X720",
  "1280X600",
  "1280X400",
  "960X960",
  "1024X768",
  "1024X600",
  "1024X400",
  "800X800",
  "800X640",
  "800X512",
  "800X400",
  "800X320",
  "728X512",
  "640X560",
  "640X480",
  "512X440",
  "512X400",
  "480X320",
  "320X240",
  "2X2Bin:640X480",
  NULL,
};
static char *all_resolutions_asi034[] = {
  "728x512",
  "640X480",
  "480X320",  
  "320X240",
  "2X2Bin:364x256",
  NULL,
};
static char *all_resolutions_asi130[] = {
  "1280X1024",
  "1280X600",
  "1280X400",
  "800x600",
  "800x400",
  "640x480",
  "600x400",
  "400x400",
  "480x320",
  "320x240",
  "2X2Bin:640X512",
  "4X4Bin:320x240",
  NULL,
};
static char *all_resolutions_asi035[] = {
  "752x480",
  "640x480",
  "600x400",
  "400x400",
  "320x240",
  "2x2Bin:376x240",
  NULL
};

static int *resolutions_x;
static int all_resolutions_x_asi120[] = {
  1280,
  1280,
  1280,
  1280,
  960,
  1024,
  1024,
  1024,
  800,
  800,
  800,
  800,
  800,
  728,
  640,
  640,
  512,
  512,
  480,
  320,
  640,
};
static int all_resolutions_x_asi034[] = {
  728,
  640,
  480,
  320,
  364,
};
static int all_resolutions_x_asi130[] = {
  1280,
  1280,
  1280,
  800,
  800,
  640,
  600,
  400,
  480,
  320,
  640,
  320,
};
static int all_resolutions_x_asi035[] = {
  752,
  640,
  600,
  400,
  320,
  376,
};

static int *resolutions_y;
static int all_resolutions_y_asi120[] = {
  960,
  720,
  600,
  400,
  960,
  768,
  600,
  400,
  800,
  640,
  512,
  400,
  320,
  512,
  560,
  480,
  440,
  400,
  320,
  240,
  480,
};
static int all_resolutions_y_asi034[] = {
  512,
  480,
  320,
  240,
  256,
};
static int all_resolutions_y_asi130[] = {
  1024,
  600,
  400,
  600,
  400,
  480,
  400,
  400,
  320,
  240,
  512,
  256,
};
static int all_resolutions_y_asi035[] = {
  480,
  480,
  400,
  400,
  240,
  240,
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
static int all_resolutions_bin_asi034[] = {
  1,
  1,
  1,
  1,
  2,
};
static int all_resolutions_bin_asi130[] = {
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
  2,
};
static int all_resolutions_bin_asi035[] = {
  1,
  1,
  1,
  1,
  1,
  2,
};

static int resolutions_n;

static char *all_formats[] = {
  "RAW8",
  "RGB24",
  "RAW16",
  "Y8",
  NULL,
};
static char *formats[5];
static int n_formats;

static int format_dim[] = {
  1,
  3,
  2,
  1,
};
static int format2[4];

static char *controls[] = {
  "gain",
  "exp",
  "gamma",
  "wb_r",
  "wb_b",
  "bright",
  "bwidth",
};

static char *NY[] = {
  "no",
  "yes",
  NULL,
};

struct zwo_cam {
#define ZWP(D) \
  volatile int D; \
  volatile int D ## _auto :1; \
  volatile int D ## _changed :1
  ZWP(format);
  ZWP(flipx);
  ZWP(flipy);
  ZWP(startx);
  ZWP(starty);
  ZWP(resolution);
  volatile float temp;
  volatile unsigned long dropped;
  double pixel_size;
  int bayern;
  char *model;
  int color;
  ZWP(gain);
  ZWP(exposure);
  ZWP(gamma);
  ZWP(wb_r);
  ZWP(wb_b);
  ZWP(brightness);
  ZWP(bandwidthoverload);
  volatile int run;
  GThread *worker;
  GStaticMutex lock;
  int maxw;
  int maxh;
};

static void zwo_setup(struct zwo_cam *z)
{
  int bin = resolutions_bin[z->resolution];
  int w = resolutions_x[z->resolution];
  int h = resolutions_y[z->resolution];

  fprintf(stderr, "SET: %dx%d %d f: %d-%d s: %d+%d\n", w, h, z->format, z->flipx, z->flipy, z->startx, z->starty);
  setImageFormat(w, h, bin, z->format);
  if (bin == 2)
    setStartPos(0, 0);
  else
    setStartPos(z->startx, z->starty);
  SetMisc(z->flipx, z->flipy);
#define CH0(x) z->x ## _changed = 0
  CH0(format);
  CH0(flipx);
  CH0(flipy);
  CH0(startx);
  CH0(starty);
  CH0(resolution);
}

static gpointer zwo_worker(gpointer z_)
{
  struct zwo_cam *z = z_;
  int old_run = 0;
  unsigned char *buf;
  int bs;

  bs = z->maxw * z->maxh * 3;
  buf = malloc(bs);
  while (1) {
    g_static_mutex_lock(&z->lock);
    if (old_run == 0 && z->run == 1) {
      zwo_setup(z);
      startCapture();
      old_run = 1;
    }
    else if (old_run == 1 && z->run == 0) {
      stopCapture();
      old_run = 0;
    }
#define CH(x) z->x ## _changed
    if (CH(format) || CH(flipx) || CH(flipy) || CH(startx) || CH(starty) || CH(resolution)) {
      //stopCapture();
      zwo_setup(z);
      //startCapture();
    }

#define PTAC(P, I) do {				\
      if (CH(P)) {				\
	setValue(I, z->P, z->P ## _auto);	\
	fprintf(stderr, "SET: %d=%d(%d)\n", I, z->P, z->P ## _auto); \
	CH0(P);					\
      }						\
    }  while(0)
    PTAC(gain, 0);
    PTAC(exposure, 1);
    PTAC(gamma, 2);
    PTAC(wb_r, 3);
    PTAC(wb_b, 4);
    PTAC(brightness, 5);
    PTAC(bandwidthoverload, 6);

    g_static_mutex_unlock(&z->lock);
    if (old_run) {
      if (getImageData(buf, resolutions_x[z->resolution] * resolutions_y[z->resolution] * format_dim[z->format] , 1000)) {
	yaac_new_image(buf, resolutions_x[z->resolution], resolutions_y[z->resolution], z->format, format_dim[z->format]);
      }
      z->dropped = getDroppedFrames();
    }
    else {
      sleep(1);
    }
    z->temp = getSensorTemp();
  }
  free(buf);
  return NULL;
}

static int zwo_n;
static struct yaaca_ctrl *c;

static void *zwo_cam_init(int n, struct yaaca_ctrl **ctrls, int *n_ctrls, int *maxw, int *maxh)
{
  struct zwo_cam *z;
  int nc, i;
  char aa;
  int av;
  int numDevices;
  struct utsname uts;

  numDevices = getNumberOfConnectedCameras();
  if(numDevices <= 0) {
    fprintf(stderr, "no camera connected\n");
    return NULL;
  }
  else {
    printf("attached cameras:\n");
  }
  for(i = 0; i < numDevices; i++)
    printf("%d %s\n",i, getCameraModel(i));

  if (!openCamera(n)) {
    fprintf(stderr, "openCamera %d failed\n", n);
    return NULL;
  }
  if (!initCamera()) {
    fprintf(stderr, "initCamera failed\n");
    return NULL;
  }
  zwo_n = n;

  uname(&uts);
  if (!strncmp(uts.machine, "arm", 3)) {
    fprintf(stderr, "Found ARM machine setting USB BW to min\n");
    setValue(CONTROL_BANDWIDTHOVERLOAD, getMin(CONTROL_BANDWIDTHOVERLOAD), 0);
  }

  *maxw = getMaxWidth();
  *maxh = getMaxHeight();
  fprintf(stderr, "DELME %d %d %d\n", *maxw, *maxh, getCameraType(n));

  switch(getCameraType(n)) {
  case CAMERA_ASI034MC:
    resolutions = all_resolutions_asi034;
    resolutions_x = all_resolutions_x_asi034;
    resolutions_y = all_resolutions_y_asi034;
    resolutions_bin = all_resolutions_bin_asi034;
    break;
  case CAMERA_ASI130MM:
    resolutions = all_resolutions_asi130;
    resolutions_x = all_resolutions_x_asi130;
    resolutions_y = all_resolutions_y_asi130;
    resolutions_bin = all_resolutions_bin_asi130;
    break;
  case CAMERA_ASI035MM:
  case CAMERA_ASI035MC:
    resolutions = all_resolutions_asi035;
    resolutions_x = all_resolutions_x_asi035;
    resolutions_y = all_resolutions_y_asi035;
    resolutions_bin = all_resolutions_bin_asi035;
    break;
  default:
    resolutions = all_resolutions_asi120;
    resolutions_x = all_resolutions_x_asi120;
    resolutions_y = all_resolutions_y_asi120;
    resolutions_bin = all_resolutions_bin_asi120;
    break;
  }

  while (*resolutions_x > *maxw || *resolutions_y > *maxh) {
    resolutions++;
    resolutions_x++;
    resolutions_y++;
    resolutions_bin++;
  }
  while (resolutions[resolutions_n])
    resolutions_n++;

  for(i = 0; i < 4; i++) {
    if (isImgTypeSupported(i)) {
      formats[n_formats] = all_formats[i];
      format2[n_formats] = i;
      n_formats++;
    }
  }

  av = getValue(CONTROL_GAIN, &aa);
  setValue(CONTROL_GAIN, av, 1);
  av = getValue(CONTROL_EXPOSURE, &aa);
  setValue(CONTROL_EXPOSURE, av, 1);

  c = calloc(30, sizeof(struct yaaca_ctrl));
  assert(c);

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

  NEW_CTRL(YAACA_ENUM, "format", 0, n_formats - 1, &formats[0], 0, n_formats == 4 ? 1 : 0); /* 0 */
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

  for (i = 0; i < 7; i++) {
    NEW_CTRL(YAACA_REAL, controls[i], getMin(i), getMax(i), NULL,
	     isAutoSupported(i) ? YAACA_AUTO : 0 |
	     isAvailable(i) ? 0 : YAACA_OFF,
	     getValue(i, &c[nc].def_auto)); /* 12 + i */
  }

  z = calloc(1, sizeof(*z));
  *ctrls = c;
  *n_ctrls = nc;
  z->maxw = *maxw;
  z->maxh = *maxh;
  z->temp = getSensorTemp();
  z->pixel_size = getPixelSize();
  z->bayern = getColorBayer();
  fprintf(stderr, "bayer is %d\n", z->bayern);
  z->model = getCameraModel(n);
  z->color = isColorCam();
  z->format = 1;
  g_static_mutex_init(&z->lock);
  z->worker = g_thread_create(zwo_worker, z, FALSE, NULL);
  return z;
}


static int zwo_set(void * cam, int ctrl, double val, int autov)
{
  struct zwo_cam *z = cam;

#define ZWO_SET(I, V) do {			\
    if (ctrl == I) {				\
      g_static_mutex_lock(&z->lock);		\
      z->V = val;				\
      z->V ## _auto = autov; 			\
      z->V ## _changed = 1;			\
      g_static_mutex_unlock(&z->lock);		\
      return 0;					\
    }						\
  } while(0)

  if (ctrl == 0)
    val = format2[(int) val];

  ZWO_SET(0, format);
  ZWO_SET(1, flipx);
  ZWO_SET(2, flipy);
  ZWO_SET(9, startx);
  ZWO_SET(10, starty);
  ZWO_SET(11, resolution);
  ZWO_SET(12, gain);
  ZWO_SET(13, exposure);
  ZWO_SET(14, gamma);
  ZWO_SET(15, wb_r);
  ZWO_SET(16, wb_b);
  ZWO_SET(17, brightness);
  ZWO_SET(18, bandwidthoverload);

  return -1;
}

static double zwo_get(void * cam, int ctrl)
{
  struct zwo_cam *z = cam;

#define ZWO_GET(I, V) do {				\
    if (ctrl == I) {					\
      return z->V;					\
    }							\
  } while(0)

  ZWO_GET(3, temp);
  ZWO_GET(4, dropped);
  ZWO_GET(5, pixel_size);
  ZWO_GET(6, bayern);
  ZWO_GET(8, color);
  ZWO_GET(4, dropped);

  return 0;
}

static char * zwo_get_str(void *cam, int ctrl)
{
  struct zwo_cam *z = cam;

  ZWO_GET(7, model);

  return "INVALID";
}

static void zwo_close(void *cam_)
{
  closeCamera();
}

static void zwo_run(void * cam, int r)
{
  struct zwo_cam *z = cam;

  z->run = r;
}

static void zwo_get_pars(void *cam, int *w, int *h, int *format, int *Bpp, int *sx, int *sy)
{
  struct zwo_cam *z = cam;

  if (w) *w = resolutions_x[z->resolution];
  if (h) *h = resolutions_y[z->resolution];
  if (format) *format = z->format;
  if (Bpp) *Bpp = format_dim[z->format];
  if (sx) *sx = z->startx;
  if (sy) *sy = z->starty;
}

static void zwo_pulse (void *cam, int dir, int n)
{
  //fprintf(stderr, "PULSE %d %d\n", dir, n);
  pulseGuide(dir, n);
}

static const char *home;

static const char *get_home(void)
{
  if (home)
    return home;

  home = getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    home = pw->pw_dir;
  }
  return home;
}

static void zwo_load(void *cam)
{
  struct zwo_cam *z = cam;
  char fname[MAX_PATH];
  FILE *f;
  int i;

  snprintf(fname, MAX_PATH, "%s/.ASICamera%d", get_home(), zwo_n);
  f = fopen(fname, "r");
  if (f) {
    int x,y,w,h,m,b,fx,fy;
    int reso = 0;
    if (fscanf(f, "%d %d\n%d %d\n%d %d\n%d %d\n", &x, &y, &w, &h, &fx, &fy, &m, &b) != 8)
      goto load_err;
    c[1].def = fx;
    zwo_set(z, 1, fx, 0);
    c[2].def = fy;
    zwo_set(z, 2, fy, 0);
    SetMisc(fx, fy);
    c[9].def = x;
    zwo_set(z, 9, x, 0);
    c[10].def = y;
    zwo_set(z, 10, y, 0);
    setStartPos(x, y);
    i = 0;
    while (resolutions[i]) {
      if (resolutions_x[i] == w && resolutions_y[i] == h && resolutions_bin[i] == b) {
	reso = i;
	break;
      }
      i++;
    }
    c[11].def = reso;
    zwo_set(z, 11, reso, 0);
    c[0].def = 0;
    for (i = 0; i < n_formats; i++) {
      if (format2[i] == m) {
	c[0].def = i;
	break;
      }
    }
    zwo_set(z, 0, c[0].def, 0);
    setImageFormat(w, h, b, m);
    for(i = 0; i < 7; i++) {
      int v, a;

      if (fscanf(f, "%d %d\n", &v, &a) != 2)
	goto load_err;
      c[12 + i].def = v;
      c[12 + i].def_auto = a;
      zwo_set(z, 12 + i, v, a);
      setValue(i, v, a);
    }
  load_err:
    fclose(f);
  }
}

static void zwo_save(void *cam)
{
  char fname[MAX_PATH];
  FILE *f;
  int i;

  snprintf(fname, MAX_PATH, "%s/.ASICamera%d", get_home(), zwo_n);
  f = fopen(fname, "w");
  if (f) {
    char fx, fy;

    GetMisc(&fx, &fy);
    fprintf(f,
	    "%d %d\n"
	    "%d %d\n"
	    "%d %d\n"
	    "%d %d\n",
	    getStartX(), getStartY(),
	    getWidth(), getHeight(),
	    fx != 0, fy != 0,
	    getImgType(), getBin());
    for(i = 0; i < 7; i++) {
      int v;
      char a;

      v = getValue(i, &a);
      fprintf(f, "%d %d\n", v, a != 0);
    }
    fclose(f);
  }
}

static int zwo_maxw(void *cam)
{
  return getMaxWidth();
}

static int zwo_maxh(void *cam)
{
  return getMaxHeight();
}

static int zwo_isbin(void *cam, int res)
{
  return resolutions_bin[res];
}

uint8_t *zwo_get_buffer(void *cam, int done)
{
  return NULL;
}

int zwo_save_path(void *cam, const char *path)
{
  return -1;
}

struct yaaca_cam_s ZWO_CAM = {
  "ZWO Asi Camera",
  zwo_cam_init,
  zwo_close,
  zwo_set,
  zwo_get,
  zwo_get_str,
  zwo_run,
  zwo_get_pars,
  zwo_pulse,
  zwo_save,
  zwo_load,
  zwo_maxw,
  zwo_maxh,
  zwo_isbin,
  zwo_get_buffer,
  zwo_save_path,
};
