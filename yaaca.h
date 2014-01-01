#ifndef _YAACA_H_
#define _YAACA_H_ 1

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
  int def_auto;
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
  void (*pulse) (int dir, int n);
};

#define YAACA_FMT_RAW8 0
#define YAACA_FMT_RGB24 1
#define YAACA_FMT_RAW16 2
#define YAACA_FMT_Y8 3

int yaac_new_image(unsigned char *data, int w, int h, int format, int bpp);


#endif
