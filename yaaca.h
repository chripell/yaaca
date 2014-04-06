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

#ifndef _YAACA_H_
#define _YAACA_H_ 1

#include <stdint.h>

#define YAACA_MAX_NAME 100

#define YAACA_REAL 1
#define YAACA_ENUM 2
#define YAACA_STRING 2

#define YAACA_RO 1
#define YAACA_AUTO 2
#define YAACA_REFRESH 4
#define YAACA_OFF 8

#define YAACA_N 0
#define YAACA_S 1
#define YAACA_E 2
#define YAACA_W 3

struct yaaca_ctrl {
  char name[YAACA_MAX_NAME];
  int type;
  double min, max, def;
  char def_auto;
  char **text;
  int flags;
};

struct yaaca_cam_s {
  char *name;
  void * (*init)(int n, struct yaaca_ctrl **ctrls, int *n_ctrls, int *maxw, int *maxh);
  void (*close)(void *);
  int (*set)(void * cam, int ctrl, double val, int autov);
  double (*get)(void * cam, int ctrl);
  char * (*get_str)(void *cam, int ctrl);
  void (*run)(void *cam, int r);
  void (*get_pars) (void *cam, int *w, int *h, int *format, int *Bpp, int *sx, int *sy);
  void (*pulse) (void *cam, int dir, int n);
  void (*save) (void *cam);
  void (*load) (void *cam);
  int (*maxw) (void *cam);
  int (*maxh) (void *cam);
  int (*isbin) (void *cam, int res);
  uint8_t * (*get_buffer) (void *cam, int done);
  int (*save_path) (void *cam, const char *path);
};

#define YAACA_FMT_RAW8 0
#define YAACA_FMT_RGB24 1
#define YAACA_FMT_RAW16 2
#define YAACA_FMT_Y8 3

int yaac_new_image(unsigned char *data, int w, int h, int format, int bpp);


#endif
