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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <arpa/inet.h>
#include <libusb.h>

#include "asill.h"
#include "registers.h"

#define pr_debug(x...) if (do_debug) fprintf(stderr, x)

#ifndef MAX_PATH
#define MAX_PATH 255
#endif

#define MAX_CMDS 200
#define CMD_SLEEP_MS 0
#define CMD_SET_REG 1
#define CMD_SEND 2

#define H_EXCESS 15

struct cmd_s {
  int cmd;
  int p1;
  int p2;
};

struct asill_s {
  libusb_device_handle *h;
  pthread_t th;
  volatile int running;
  uint8_t *d;
  int model, n;

  struct cmd_s *cmds;
  size_t max_cmds;
  size_t n_cmds;
  pthread_mutex_t cmd_lock;
  uint16_t shadow[0x1000];
  int is_color;
  int pars[ASILL_PAR_N];

  uint8_t *data;
  volatile int data_ready;
  asill_new_frame_f cb;
  float fps;
  int fps_n;
  struct timeval last_fps_comp;

  uint16_t max_width;
  uint16_t max_height;
  uint16_t width;
  uint16_t height;
  uint16_t start_x;
  uint16_t start_y;
  int bin, fmt;

  uint32_t pclk;
  uint32_t exposure_us;
  uint32_t exposure_real_us;
  uint32_t exposure_min_us;
  uint32_t exposure_max_us;

  char save_path[MAX_PATH];

  float Tk, T0, T;
};

static int do_debug;

static libusb_context *ctx;
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER; 

/*                              25, 24   40  48  96   8   2  */
const uint16_t M_PLL_mul[] =  { 25, 32,  40, 40, 48, 32, 32};
const uint16_t N_pre_div[] =  {  3,  4,   6,  5,  6,  4,  4};
const uint16_t P1_sys_div[] = {  8,  4,   2,  2,  1, 12, 24};
const uint16_t P2_clk_div[] = {  2,  4,   4,  4,  4,  4,  8};

static struct cmd_s *scmd(struct asill_s *A)
{
  if (A->n_cmds >= A->max_cmds) {
    A->max_cmds += 100;
    A->cmds = realloc(A->cmds, A->max_cmds * sizeof(struct cmd_s));
  }
  return &A->cmds[A->n_cmds++];
}

static void sleep_ms(struct asill_s *A, int ms)
{
  struct cmd_s *c;

  c = scmd(A);
  c->cmd = CMD_SLEEP_MS;
  c->p1 = ms;
}

static void send_ctrl(struct asill_s *A, int v)
{
  struct cmd_s *c;

  pr_debug("%s 0x%04x\n", __FUNCTION__, v);
  c = scmd(A);
  c->cmd = CMD_SEND;
  c->p1 = v;
  c->p2 = 0;
}

static void send_ctrl_val(struct asill_s *A, int v, int v1)
{
  struct cmd_s *c;

  pr_debug("%s 0x%04x 0x%04d\n", __FUNCTION__, v, v1);
  c = scmd(A);
  c->cmd = CMD_SEND;
  c->p1 = v;
  c->p2 = v1;
}

static void set_reg(struct asill_s *A, int r, int v)
{
  struct cmd_s *c;

  pr_debug("%s 0x%04x=0x%04x\n", __FUNCTION__, r, v);
  c = scmd(A);
  c->cmd = CMD_SET_REG;
  c->p1 = r;
  c->p2 = v;
  if (r >= 0x3000 && r < 0x4000)
    A->shadow[r - 0x3000] = v;
}

static void run_q(struct asill_s *A)
{
  int i;

  pthread_mutex_lock(&A->cmd_lock);
  for(i = 0; i < A->n_cmds; i++) {
    struct cmd_s *c = &A->cmds[i];
    int ret = 0;
    
    switch (c->cmd) {
    case CMD_SLEEP_MS:
      usleep(c->p1 * 1000);
      break;
    case CMD_SET_REG:
      ret = libusb_control_transfer(A->h, 0x40, 0xa6, c->p1, c->p2, NULL, 0, 1000);
      break;
    case CMD_SEND:
      ret = libusb_control_transfer(A->h, 0x40, c->p1 & 0xff, c->p2, 0, NULL, 0, 1000);
      break;
    default:
      assert(0);
    }
    if (ret) {
      fprintf(stderr, "control transfer failed: (%d)\n", ret);
    }
  }
  A->n_cmds = 0;
  pthread_mutex_unlock(&A->cmd_lock);
}

static int get_reg_r(struct asill_s *A, int r)
{
  unsigned char data[2] = {0,0};

  libusb_control_transfer(A->h, 0xc0, 0xa7, r, 0, data, 2, 1000);
  return data[0] * 256 + data[1];
}

static int get_reg(struct asill_s *A, int r)
{
  if (r >= 0x3000 && r < 0x4000)
    return A->shadow[r - 0x3000];
  return 0;
}

static void set_reg_mask(struct asill_s *A, int r, int mask, int v)
{
  int nv = get_reg(A, r) & ~mask;

  nv |= v;
  set_reg(A, r, nv);
}

static void calc_min_max_exp(struct asill_s *A)
{
#if 0
  uint16_t tot_w = 1600;
  uint16_t tot_h = A->height + H_EXCESS;
  double pclk = 48000000.0 * M_PLL_mul[A->pclk] / (N_pre_div[A->pclk] * P1_sys_div[A->pclk] * P2_clk_div[A->pclk]);


  A->exposure_min_us = 1000000.0 * tot_w * tot_h / pclk;
  A->exposure_max_us = 0x8000 * (65535.0 / pclk) * 1000000.0;
  pr_debug("%s exp_us min %u max %u\n", __FUNCTION__, A->exposure_min_us, A->exposure_max_us);
#endif
}

static void restore_rnc(struct asill_s *A)
{
  return;			/* doesn't work */
  set_reg(A, MT9M034_RESET_REGISTER, 0x10d8);
  set_reg(A, MT9M034_COLUMN_CORRECTION, 0x0000);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10dc);
#if 1				/* from DS wait 1 frame for column cor */
  sleep_ms(A, 10 + 2 * A->exposure_us / 1000);
#else
  sleep_ms(A, 51);
#endif
  set_reg(A, MT9M034_RESET_REGISTER, 0x10d8);
  set_reg(A, MT9M034_COLUMN_CORRECTION, 0xe007);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10dc);
}

static int setup_frame(struct asill_s *A)
{
#define MAX_COARSE 0x2000
  uint16_t tot_w = 1600;
  uint16_t tot_h = A->height + H_EXCESS;
  uint16_t coarse, fine;
  double pclk = 48000000.0 * M_PLL_mul[A->pclk] / (N_pre_div[A->pclk] * P1_sys_div[A->pclk] * P2_clk_div[A->pclk]);
  double line_us = (tot_w / pclk) * 1000000.0;
  double fine_real;

  pr_debug("%s pclk %f exp %u line_us %f\n", __FUNCTION__, pclk, A->exposure_us, line_us);
  pr_debug("%s %dx%d:%d\n", __FUNCTION__, A->width, A->height, A->bin);

#if 1
  if (A->exposure_us > 100000) {
    tot_w = 0x2fff;
    line_us = (tot_w / pclk) * 1000000.0;
  }
  coarse = A->exposure_us / line_us;
#else
  while( (coarse = A->exposure_us / line_us) > MAX_COARSE) {
    tot_w *= 2;
    line_us = (tot_w / pclk) * 1000000.0;
    pr_debug("%s tot_w %d coarse %d line_us %f\n", __FUNCTION__, tot_w, coarse, line_us);
  }
#endif
  pr_debug("%s finale: tot_w %d coarse %d line_us %f\n", __FUNCTION__, tot_w, coarse, line_us);
  fine_real = A->exposure_us *pclk / 1000000.0 - tot_w * coarse;
  pr_debug("%s fine: %f\n", __FUNCTION__, fine_real);
  // note: if fine_real is 0 we have similar brightness (if we set the same gain of course!) to ZWO's libASICamera
  fine_real = 0;
  fine = fine_real;
  A->exposure_real_us = coarse * line_us + fine_real / (pclk / 1000000.0);
  pr_debug("%s real exp us: %u (fine %d)\n", __FUNCTION__, A->exposure_real_us, fine);
  
  set_reg(A, MT9M034_FINE_INT_TIME, fine);
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, coarse);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10da);
  sleep_ms(A, 101);
  set_reg(A, MT9M034_VT_PIX_CLK_DIV, P2_clk_div[A->pclk]);
  set_reg(A, MT9M034_VT_SYS_CLK_DIV, P1_sys_div[A->pclk]);
  set_reg(A, MT9M034_PRE_PLL_CLK_DIV, N_pre_div[A->pclk]);
  set_reg(A, MT9M034_PLL_MULTIPLIER, M_PLL_mul[A->pclk]);
  sleep_ms(A, 11);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10dc);
  sleep_ms(A, 201);
  send_ctrl(A, A->fmt == ASILL_FMT_RAW8 ? 0xab : 0xac);
  set_reg(A, MT9M034_DIGITAL_BINNING, A->bin == 2 ? 0x0022 : 0x0000);
  set_reg(A, MT9M034_Y_ADDR_START, 0x0002 + A->start_y);
  set_reg(A, MT9M034_X_ADDR_START, A->start_x);
  set_reg(A, MT9M034_FRAME_LENGTH_LINES, tot_h);
  set_reg(A, MT9M034_Y_ADDR_END, 0x0002 + A->start_y + A->height - 1);
  set_reg(A, MT9M034_X_ADDR_END, A->start_x + A->width - 1);
  set_reg(A, MT9M034_DIGITAL_BINNING, A->bin == 2 ? 0x0022 : 0x0000);
  set_reg(A, 0x306e, 0x9200);
  //set_reg(A, 0x306e, 0x9200 | (A->is_color ? 0x10 : 0));
  set_reg(A, MT9M034_LINE_LENGTH_PCK, tot_w);
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, coarse);
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, coarse);
#if 0
  restore_rnc(A);
#endif

  calc_min_max_exp(A);
  return 0;
}

static void init(struct asill_s *A)
{
  float T55, T70;

  pthread_mutex_lock(&A->cmd_lock);

  send_ctrl(A, 0xa4);
  send_ctrl(A, 0xab);
  send_ctrl(A, 0xaa);
  set_reg(A, MT9M034_RESET_REGISTER, 0x0001);
  sleep_ms(A, 101);
  set_reg(A, MT9M034_SEQ_CTRL_PORT, 0x8000);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0225);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x5050);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2d26);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0828);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0d17);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0926);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0028);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0526);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0xa728);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0725);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x8080);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2917);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0525);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0040);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2702);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1616);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2706);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1736);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x26a6);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1703);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x26a4);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x171f);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2805);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2620);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2804);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2520);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2027);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0017);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1e25);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0020);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2117);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1028);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x051b);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1703);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2706);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1703);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1747);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2660);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x17ae);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2500);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x9027);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0026);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1828);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x002e);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2a28);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x081e);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0831);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1440);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x4014);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2020);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1410);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1034);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1400);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1014);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0020);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1400);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x4013);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1802);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1470);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x7004);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1470);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x7003);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1470);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x7017);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2002);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1400);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2002);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1400);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x5004);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1400);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2004);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x1400);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x5022);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0314);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0020);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0314);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x0050);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2c2c);
  set_reg(A, MT9M034_SEQ_DATA_PORT, 0x2c2c);
  set_reg(A, MT9M034_ERS_PROG_START_ADDR, 0x0000);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10d8);
  set_reg(A, MT9M034_MODE_CTRL, 0x0029);
  set_reg(A, MT9M034_DATA_PEDESTAL, 0x0000);
  set_reg(A, MT9M034_DAC_LD_14_15, 0x0f03);
  set_reg(A, MT9M034_DAC_LD_18_19, 0xc005);
  set_reg(A, MT9M034_DAC_LD_12_13, 0x09ef);
  set_reg(A, MT9M034_DAC_LD_22_23, 0xa46b);
  set_reg(A, MT9M034_DAC_LD_20_21, 0x047d);
  set_reg(A, MT9M034_DAC_LD_16_17, 0x0070);
  set_reg(A, MT9M034_DARK_CONTROL, 0x0404);
  set_reg(A, MT9M034_DAC_LD_26_27, 0x8303);
  // note: was set_reg(A, MT9M034_DAC_LD_24_25, 0xd308);
  // in driver, but DS says put low conversion gain for
  // column correction calibration.
  set_reg(A, MT9M034_DAC_LD_24_25, 0xd008);
  set_reg(A, MT9M034_DAC_LD_10_11, 0x00bd);
  set_reg(A, MT9M034_DAC_LD_26_27, 0x8303);
  set_reg(A, MT9M034_ADC_BITS_6_7, 0x6372);
  set_reg(A, MT9M034_ADC_BITS_4_5, 0x7253);
  set_reg(A, MT9M034_ADC_BITS_2_3, 0x5470);
  set_reg(A, MT9M034_ADC_CONFIG1, 0xc4cc);
  set_reg(A, MT9M034_ADC_CONFIG2, 0x8050);
  set_reg(A, MT9M034_DIGITAL_TEST, 0x5300);
  set_reg(A, MT9M034_COLUMN_CORRECTION, 0xe007);
  set_reg(A, MT9M034_DIGITAL_CTRL, 0x0008);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10dc);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10d8);
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, 0x0fff);
  set_reg(A, MT9M034_DIGITAL_TEST, 0x5300);
  sleep_ms(A, 101);
  set_reg(A, MT9M034_EMBEDDED_DATA_CTRL, 0x1802);
  set_reg(A, 0x30b4, 0x0011);
  set_reg(A, MT9M034_AE_CTRL_REG, 0x0000);
  set_reg(A, MT9M034_READ_MODE, 0x4000);
  set_reg(A, MT9M034_DIGITAL_TEST, 0x1330);
  set_reg(A, MT9M034_GLOBAL_GAIN, 0x0024);
  sleep_ms(A, 19);
  set_reg(A, MT9M034_RED_GAIN, 0x0022);
  set_reg(A, MT9M034_BLUE_GAIN, 0x003e);
  set_reg(A, MT9M034_DATA_PEDESTAL, 0x0000);
  set_reg(A, MT9M034_LINE_LENGTH_PCK, 0x056e);
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, 0x0473);

#if 0
  /* should be gain = 50 */
  set_reg(A, MT9M034_DAC_LD_24_25, 0xd308);
  set_reg(A, MT9M034_GLOBAL_GAIN, 0x0024);
  set_reg(A, MT9M034_DIGITAL_TEST, 0x1330);
#else
  /* unity gain digital, minum analog*/
  set_reg(A, MT9M034_DIGITAL_TEST, 0x1300);
  set_reg(A, MT9M034_DAC_LD_24_25, 0xd008);
  set_reg(A, MT9M034_RED_GAIN, 0x0020);
  set_reg(A, MT9M034_BLUE_GAIN, 0x0020);
  set_reg(A, MT9M034_GREEN1_GAIN, 0x0020);
  set_reg(A, MT9M034_GREEN2_GAIN, 0x0020);
  set_reg(A, MT9M034_GLOBAL_GAIN, 0x0020);
#endif

  /* read temperature coefficents */
  T70 = get_reg_r(A, 0x30c6) & 0x7ff;
  T55 = get_reg_r(A, 0x30c8) & 0x7ff;
  A->Tk = (70.0 - 55.0) / (T70 - T55);
  A->T0 = 55.0 - A->Tk * T55;

  /* default flip for compatibility with yaaca zwo.c */
  set_reg(A, MT9M034_READ_MODE, 0x0000);

  setup_frame(A);

  /* start capture */
  send_ctrl(A, 0xaa);
  send_ctrl(A, 0xaf);
  sleep_ms(A, 100);
  send_ctrl(A, 0xa9);

  pthread_mutex_unlock(&A->cmd_lock);
}

static void stop(struct asill_s *A)
{
  pthread_mutex_lock(&A->cmd_lock);
  send_ctrl(A, 0xaa);
  pthread_mutex_unlock(&A->cmd_lock);  
}

static int diff_us(struct timeval from, struct timeval to)
{
  return 1000000 * (to.tv_sec - from.tv_sec) + (to.tv_usec - from.tv_usec);
}

static int save_hdr(struct asill_s *A, const char *fname, time_t now, int usec)
{
  FILE *f;

  f = fopen(fname, "w");
  if (f) {
    fprintf(f, "0(local datetime):%s", asctime(localtime(&now)));
    fprintf(f, "1(utc datetime):%s", asctime(gmtime(&now)));
    fprintf(f, "2(width):%d\n", A->width);
    fprintf(f, "3(height):%d\n", A->height);
    fprintf(f, "4(start x):%d\n", A->start_x);
    fprintf(f, "5(start y):%d\n", A->start_y);
    fprintf(f, "6(bin):%d\n", A->bin);
    fprintf(f, "7(fmt):%d\n", A->fmt);
    fprintf(f, "8(pclk):%d\n", A->pclk);
    fprintf(f, "9(exposure us):%u\n", A->exposure_us);
    fprintf(f, "10(T):%f\n", A->T);
    fprintf(f, "11(analog gain):%d\n", A->pars[ASILL_PAR_ANALOG_GAIN]);
    fprintf(f, "12(digital gain):%d\n", A->pars[ASILL_PAR_DIGITAL_GAIN]);
    fprintf(f, "13(digital gain R):%d\n", A->pars[ASILL_PAR_DIGITAL_GAIN_R]);
    fprintf(f, "14(digital gain G1):%d\n", A->pars[ASILL_PAR_DIGITAL_GAIN_G1]);
    fprintf(f, "15(digital gain G2):%d\n", A->pars[ASILL_PAR_DIGITAL_GAIN_G2]);
    fprintf(f, "16(digital gain B):%d\n", A->pars[ASILL_PAR_DIGITAL_GAIN_B]);
    fprintf(f, "17(bias sub):%d\n", A->pars[ASILL_PAR_BIAS_SUB]);
    fprintf(f, "18(row denoise):%d\n", A->pars[ASILL_PAR_ROW_DENOISE]);
    fprintf(f, "19(col denoise):%d\n", A->pars[ASILL_PAR_COL_DENOISE]);
    fprintf(f, "20(flip x):%d\n", A->pars[ASILL_PAR_FLIP_X]);
    fprintf(f, "21(flip y):%d\n", A->pars[ASILL_PAR_FLIP_Y]);
    fprintf(f, "22(time s):%ld\n", now);
    fprintf(f, "23(time us):%u\n", usec);
    fclose(f);
  }
  else
    return -1;
  return 0;
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

int asill_save_pars(struct asill_s *A)
{
  char fname[MAX_PATH];

  snprintf(fname, MAX_PATH, "%s/.ASILL_%d_%d", get_home(), A->model, A->n);
  return save_hdr(A, fname, time(NULL), 0);
}

static char *get_p(const char *p)
{
  char *s = strchr(p, ':');

  if (!s)
    return s;
  return &s[1];
}

int asill_load_pars(struct asill_s *A)
{
  char fname[MAX_PATH];
  FILE *f;

  snprintf(fname, MAX_PATH, "%s/.ASILL_%d_%d", get_home(), A->model, A->n);
  f = fopen(fname, "r");
  if (f) {
    char b[200];

    while(!feof(f)) {
      if (fgets(b, 200, f)) {
	int p = atoi(b);
	const char *vs = get_p(b);

	if (vs) {
	  int v = atoi(vs);

	  if (p >= 11 && p <= 21) {
	    asill_set_int_par(A, p - 11, v);
	    if (p == 16 || p == 17) sleep(1); /* without this column denoise is killed if row and bias cor are saved off */
	  }
	  else {
	    switch(p) {
	    case 2:
	      A->width = v;
	      break;
	    case 3:
	      A->height = v;
	      break;
	    case 4:
	      A->start_x = v;
	      break;
	    case 5:
	      A->start_y = v;
	      break;
	    case 6:
	      A->bin = v;
	      break;
	    case 7:
	      A->fmt = v;
	      break;
	    case 8:
	      A->pclk = v;
	      break;
	    case 9:
	      A->exposure_us = v;
	      break;
	    }
	  }
	}
      }
    }
    fclose(f);
    setup_frame(A);
  }
  else
    return -1;
  return 0;
}

static void *worker(void *A_)
{
  struct asill_s *A = (struct asill_s *) A_;

  while (A->running) {
    int transfered, ret;
    
    run_q(A);
    A->T = (get_reg_r(A, 0x30b2) & 0x7ff) * A->Tk + A->T0;
    if ((ret = libusb_bulk_transfer(A->h, 0x82, A->d,
				    A->width * A->height * (A->fmt == ASILL_FMT_RAW16 ? 2 : 1) / (A->bin * A->bin),
				    &transfered, 1000 + (A->exposure_us / 1000))) == 0) {
      struct timeval now;

      gettimeofday(&now, NULL);
      if (A->data && !A->data_ready) {
	memcpy(A->data, A->d, A->width * A->height * (A->fmt == ASILL_FMT_RAW16 ? 2 : 1) / (A->bin * A->bin));
	A->data_ready = 1;
      }
      if (A->cb) {
	A->cb(A->d, A->width / A->bin, A->height / A->bin);
      }
      A->fps_n += 1;
      if (A->last_fps_comp.tv_sec == 0 && A->last_fps_comp.tv_usec == 0)
	gettimeofday(&A->last_fps_comp, NULL);
      else {
	int e;

	e = diff_us(A->last_fps_comp, now);
	if (e > 1000000) {
	  float xframe = ((float) e) / A->fps_n;

	  A->fps = 1000000.0 / xframe;
	  A->last_fps_comp = now;
	  A->fps_n = 0;
	}
      }
      if (A->save_path[0]) {
	struct timeval tv;
	char fname[MAX_PATH];
	FILE *f;

	gettimeofday(&tv, NULL);
	snprintf(fname, MAX_PATH, "%s/%010lu_%06lu.hdr", A->save_path, tv.tv_sec, tv.tv_usec);
	save_hdr(A, fname, tv.tv_sec, tv.tv_usec);

	snprintf(fname, MAX_PATH, "%s/%010lu_%06lu.pgm", A->save_path, tv.tv_sec, tv.tv_usec);
	f = fopen(fname, "w");
	if (f) {
	  fprintf(f, "P%d\n%d %d\n%d\n",
		  5,
		  A->width, A->height,
		  A->fmt == ASILL_FMT_RAW16 ? 65535 : 255);
	  fwrite(A->d, A->width * A->height * (A->fmt == ASILL_FMT_RAW16 ? 2 : 1), 1, f);
	  fclose(f);
	}
      }
    }
    else {
      fprintf(stderr, "bulk transfer failed: %d\n", ret);
      if (0) {
	stop(A);
	init(A);
      }
    }
  }
  return NULL;
}

struct asill_s *asill_new(uint16_t model, int n, int has_buffer, asill_new_frame_f cb)
{
  int ret, j;
  libusb_device **list;
  ssize_t i, cnt;
  struct asill_s *A = NULL;

  if (getenv("ASILL_DEBUG"))
    do_debug = 1;

  pthread_mutex_lock(&lk);
  if (!ctx) {
    if ((ret = libusb_init(&ctx))) {
      ctx = NULL;
      fprintf(stderr, "libusb_init failed: (%d)\n", ret);
      A = NULL;
      goto asill_new_exit;
    }
  }
  cnt = libusb_get_device_list(ctx, &list);
  if (cnt == 0) {
    A = NULL;
    goto asill_new_exit;
  }
  j = 0;
  for(i = 0; i < cnt; i++){
    libusb_device *device = list[i];
    struct libusb_device_descriptor desc;
    int ret;
    
    ret = libusb_get_device_descriptor(device, &desc);
    if (!ret) {
      if (desc.idVendor == 0x03c3 && desc.idProduct == model) {
	if (n == j) {
	  A = calloc(1, sizeof(*A));
	  ret = libusb_open(device, &A->h);
	  if (ret) {
	    fprintf(stderr, "libusb_open failed: (%d)\n", ret);
	    free(A);
	    A = NULL;
	    i = cnt;
	  }
	  else {
	     pthread_mutex_init(&A->cmd_lock, NULL); 
	  }
	  break;
	}
	j++;
      }
    }
  }
  libusb_free_device_list(list, 1);

  if (A) {
    A->model = model;
    A->n = n;
    A->fmt = ASILL_FMT_RAW16;
    A->max_width = 1280;
    A->max_height = 960;
    A->width = A->max_width;
    A->height = A->max_height;
    A->pars[ASILL_PAR_ANALOG_GAIN] = 8;
    A->pars[ASILL_PAR_DIGITAL_GAIN] = 0x20;
    A->pars[ASILL_PAR_DIGITAL_GAIN_R] = 0x20;
    A->pars[ASILL_PAR_DIGITAL_GAIN_G1] = 0x20;
    A->pars[ASILL_PAR_DIGITAL_GAIN_G2] = 0x20;
    A->pars[ASILL_PAR_DIGITAL_GAIN_B] = 0x20;
    A->pars[ASILL_PAR_BIAS_SUB] = 1;
    A->pars[ASILL_PAR_ROW_DENOISE] = 1;
    A->pars[ASILL_PAR_COL_DENOISE] = 1;
    A->pars[ASILL_PAR_FLIP_X] = 0;
    A->pars[ASILL_PAR_FLIP_Y] = 0;
    A->bin = 1;
    A->cb = cb;
    A->pclk = ASILL_PCLK_25MHZ;
    A->exposure_us = 10000;
    A->start_x = 0;
    A->start_y = 0;
    calc_min_max_exp(A);
    if (model == ASILL_ASI120MC)
      A->is_color = 1;
    init(A);
    if (has_buffer) {
      A->data = malloc(A->max_width * A->max_height * 2 / (A->bin * A->bin));
    }
    A->d = malloc(A->max_width * A->max_height * 2 / (A->bin * A->bin));
    run_q(A);
    A->running = 1;
    assert(pthread_create(&A->th, NULL, worker, A) == 0);
  }

 asill_new_exit:
  pthread_mutex_unlock(&lk);  
  return A;
}

uint8_t *asill_get_buffer(struct asill_s *A)
{
  if (!A->data_ready)
    return NULL;
  return A->data;
}

void asill_done_buffer(struct asill_s *A)
{
  A->data_ready = 0;
}

int asill_sel_pclk(struct asill_s *A, int pclk)
{
  A->pclk = pclk;
  return setup_frame(A);
}

int asill_get_pclk(struct asill_s *A)
{
  return A->pclk;
}

int asill_set_wh(struct asill_s *A, uint16_t w, uint16_t h, int bin, int fmt)
{
  pr_debug("%s: %dx%d bin %d fmt %d\n", __FUNCTION__, w, h, bin, fmt);
  A->width = w;
  A->height = h;
  A->bin = bin;
  A->fmt = fmt;
  pthread_mutex_lock(&A->cmd_lock);
  setup_frame(A);
  pthread_mutex_unlock(&A->cmd_lock);
  asill_set_xy(A, A->start_x, A->start_y);
  return 0;
}

uint16_t asill_get_w(struct asill_s *A)
{
  return A->width;
}

uint16_t asill_get_h(struct asill_s *A)
{
  return A->height;
}

int asill_get_bin(struct asill_s *A)
{
  return A->bin;
}

uint16_t asill_get_maxw(struct asill_s *A)
{
  return A->max_width;
}

uint16_t asill_get_maxh(struct asill_s *A)
{
  return A->max_height;
}

int asill_set_xy(struct asill_s *A, uint16_t x, uint16_t y)
{
  if (x + A->width > A->max_width) {
    x = A->max_width - A->width;
  }
  if (y + A->height > A->max_height) {
    y = A->max_height - A->height;
  }
  A->start_x = x;
  A->start_y = y;
  pthread_mutex_lock(&A->cmd_lock);
  set_reg(A, MT9M034_Y_ADDR_START, 0x0002 + A->start_y);
  set_reg(A, MT9M034_X_ADDR_START, A->start_x);
  set_reg(A, MT9M034_Y_ADDR_END, 0x0002 + A->start_y + A->height -1);
  set_reg(A, MT9M034_X_ADDR_END, A->start_x + A->width - 1);
  pthread_mutex_unlock(&A->cmd_lock);
  return 0;  
}

int asill_set_int_par(struct asill_s *A, int par, int gain)
{
  int ret = 0;
  int a,b;
  int ngain;

  pthread_mutex_lock(&A->cmd_lock);
  switch(par) {
  case ASILL_PAR_ANALOG_GAIN:
     /* gain from 1 to 16 */
    ngain = gain - 1;
    if (ngain < 0)
      ngain = 0;
    if (ngain > 15)
      ngain = 15;
    a = ngain >> 2;
    b = ngain & 3;
    set_reg_mask(A, 0x30b0, (3 << 4), (a << 4));  
    set_reg_mask(A, 0x3ee4, (3 << 8), (b << 8));  
    break;
  case ASILL_PAR_DIGITAL_GAIN:
    set_reg(A, MT9M034_GLOBAL_GAIN, gain);
    break;
  case ASILL_PAR_DIGITAL_GAIN_R:
    set_reg(A, MT9M034_RED_GAIN, gain);
    break;
  case ASILL_PAR_DIGITAL_GAIN_G1:
    set_reg(A, MT9M034_GREEN1_GAIN, gain);
    break;
  case ASILL_PAR_DIGITAL_GAIN_G2:
    set_reg(A, MT9M034_GREEN2_GAIN, gain);
    break;
  case ASILL_PAR_DIGITAL_GAIN_B:
    set_reg(A, MT9M034_BLUE_GAIN, gain);
    break;
  case ASILL_PAR_BIAS_SUB:
    set_reg_mask(A, 0x30ea, (1 << 15), gain ? 0 : (1 << 15));  
    break;
  case ASILL_PAR_ROW_DENOISE:
    set_reg_mask(A, 0x3044, (1 << 10), gain ? (1 << 10) : 0);  
    break;
  case ASILL_PAR_COL_DENOISE:
    set_reg_mask(A, 0x30d4, (1 << 15), gain ? (1 << 15) : 0);  
    break;
  case ASILL_PAR_FLIP_X:
    set_reg_mask(A, 0x3040, (1 << 14), gain ? (1 << 14) : 0);
    break;
  case ASILL_PAR_FLIP_Y:
    set_reg_mask(A, 0x3040, (1 << 15), gain ? (1 << 15) : 0);
    break;
  default:
    ret = -1;
  }
  pthread_mutex_unlock(&A->cmd_lock);
  if (ret == 0) {
    A->pars[par] = gain;
    if (par == ASILL_PAR_DIGITAL_GAIN) {
      A->pars[ASILL_PAR_DIGITAL_GAIN_R] = gain;
      A->pars[ASILL_PAR_DIGITAL_GAIN_G1] = gain;
      A->pars[ASILL_PAR_DIGITAL_GAIN_G2] = gain;
      A->pars[ASILL_PAR_DIGITAL_GAIN_B] = gain;
    }
    if (par == ASILL_PAR_FLIP_X || par == ASILL_PAR_FLIP_Y) {
      restore_rnc(A);
    }
  }
  return ret;
}

int asill_get_int_par(struct asill_s *A, int par)
{
  return A->pars[par];
}

int asill_set_exp_us(struct asill_s *A, uint32_t exp)
{
  if (exp < A->exposure_min_us || exp > A->exposure_max_us) {
    fprintf(stderr, "exposure out of limits, should be: %u <= %u <= %u\n",
	    A->exposure_min_us, exp, A->exposure_max_us);
  }
  A->exposure_us = exp;
  pthread_mutex_lock(&A->cmd_lock);
  setup_frame(A);
  pthread_mutex_unlock(&A->cmd_lock);
  return 0;
}

uint32_t asill_get_exp_us(struct asill_s *A)
{
  return A->exposure_us;
}

uint32_t asill_get_min_exp_us(struct asill_s *A)
{
  return A->exposure_min_us;  
}

uint32_t asill_get_max_exp_us(struct asill_s *A)
{
  return A->exposure_max_us;  
}

float asill_get_temp(struct asill_s *A)
{
  return A->T;
}

int asill_is_color(struct asill_s *A)
{
  return A->is_color;
}

int asill_set_save(struct asill_s *A, const char *path)
{
  if (path == NULL) {
    A->save_path[0] = '\0';
    return 0;
  }
  if (snprintf(A->save_path, MAX_PATH, "%s/", path) >= MAX_PATH) {
    A->save_path[0] = '\0';
    return -1;
  }
  return 0;
}

float asill_get_fps(struct asill_s *A)
{
  return A->fps;
}

uint16_t asill_get_x(struct asill_s *A)
{
  return A->start_x;
}

uint16_t asill_get_y(struct asill_s *A)
{
  return A->start_y;
}

int asill_get_format(struct asill_s *A)
{
  return A->fmt;
}

void asill_pulse(struct asill_s *A, int dir, int ms)
{
  pthread_mutex_lock(&A->cmd_lock);
  send_ctrl_val(A, 0xb0, dir);
  sleep_ms(A, ms);
  send_ctrl_val(A, 0xb1, dir);
  pthread_mutex_unlock(&A->cmd_lock);
}

int asill_buffer2float(struct asill_s *A, float *fb)
{
  int i;
  int n = A->width * A->height;
  
  if (!A->data_ready)
    return 0;
  if (A->fmt == ASILL_FMT_RAW16) {
    uint16_t *p = (uint16_t *) A->d;

    for(i = 0; i < n; i++)
      *fb++ = ntohs(*p++);
  }
  else {
    uint8_t *p = (uint8_t *) A->d;

    for(i = 0; i < n; i++)
      *fb++ = *p++;
  }
  A->data_ready = 0;
  return n;
}

int asill_buffer2buffer(struct asill_s *A, void *fb)
{
  int n = A->width * A->height;

  if (!A->data_ready)
    return 0;
  memcpy(fb, A->d, n * (A->fmt == ASILL_FMT_RAW16 ? 2 : 1));
  A->data_ready = 0;
  return n;
}
