
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/types.h>

#include "yaaca.h"

#include "asill.h"

static struct asill_s *A;
static struct yaaca_ctrl *c;

/* 
   n is 0-9 refered to ASI120MM
   n is 10-11 refered to ASI120MC

 */
static void *zwoll_cam_init(int n, struct yaaca_ctrl **ctrls, int *n_ctrls, int *maxw, int *maxh)
{
  uint16_t model = (n >= 10) ? ASILL_ASI120MC : ASILL_ASI120MM;

  if (n >= 10)
    n -= 10;
  A = asill_new(model, n, 1, NULL);
  if (!A) {
    fprintf(stderr, "asill_new failed\n");
    return NULL;
  }

  *maxw = 1280;
  *maxh = 960;

  c = calloc(30, sizeof(struct yaaca_ctrl));
  assert(c);
  *ctrls = c;
  *n_ctrls = 0;
  return A;
}

static int zwoll_set(void * cam, int ctrl, double val, int autov)
{
  return 0;
}

static double zwoll_get(void * cam, int ctrl)
{
  return 0.0;
}

static char * zwoll_get_str(void *cam, int ctrl)
{
  return NULL;
}

static void zwoll_close(void *cam_)
{
}

static void zwoll_get_pars(void *cam, int *w, int *h, int *format, int *Bpp, int *sx, int *sy)
{
  *w = 1280;
  *h = 960;
  *format = YAACA_FMT_RAW16;
  *Bpp = 2;
  *sx = 0;
  *sy = 0;
}

static void zwoll_pulse (int dir, int n)
{
}

static void zwoll_load(void *cam)
{
}

static void zwoll_save(void *cam)
{
}

static int zwoll_maxw(void *cam)
{
  return 1280;
}

static int zwoll_maxh(void *cam)
{
  return 960;
}

static int zwoll_isbin(void *cam, int res)
{
  return 0;
}

uint8_t *zwoll_get_buffer (void *cam, int done)
{
  struct asill_s *A = (struct asill_s *) cam;

  if (done) {
    asill_done_buffer(A);
  }
  return asill_get_buffer(A);
}

static void zwoll_run(void * cam, int r)
{
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
};

