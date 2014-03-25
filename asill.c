
#include <assert.h>
#include <stdio.h>

#include "asill.h"
#include "registers.h"

#define MAX_CMDS 200
#define CMD_SLEEP_MS 0
#define CMD_SET_REG 1
#define CMD_SEND 2

struct cmd_s {
  int cmd;
  int p1;
  int p2;
};

struct asill_s {
  libusb_device_handle *h;
  pthread_t th;
  volatile int running;
  unsigned char *d;

  struct cmd_s *cmds;
  size_t max_cmds;
  size_t n_cmds;
  pthread_mutex_t cmd_lock;
  uint16_t shadow[0x200];

  unsigned char *data;
  volatile int data_ready;
  asill_new_frame_f cb;

  uint16_t max_width;
  uint16_t max_height;
  uint16_t width;
  uint16_t height;
  uint16_t start_x;
  uint16_t start_y;

  uint16_t digital_gain;
  uint16_t digital_gain_R;
  uint16_t digital_gain_G1;
  uint16_t digital_gain_G2;
  uint16_t digital_gain_B;

  uint32_t pclk;
  uint32_t exposure_us;
  uint16_t M_pll_mul;
  uint16_t N_pre_pll_div;
  uint16_t P1_sys_div;
  uint16_t P2_pix_div;
};

static libusb_context *ctx;
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER; 

const uint16_t M_PLL_mul[] =  { 32, 40, 40, 48, 32, 32, };
const uint16_t N_pre_div[] =  { 4,   3,  5,  6,  4,  4, };
const uint16_t P1_sys_div[] = { 4,   4,  2,  1, 12, 24, };
const uint16_t P2_clk_div[] = { 4,   4,  4,  4,  4,  8, };

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

  c = scmd(A);
  c->cmd = CMD_SEND;
  c->p1 = v;
}

static void set_reg(struct asill_s *A, int r, int v)
{
  struct cmd_s *c;

  assert(r >= 0x3000 && r < 0x3200);
  c = scmd(A);
  c->cmd = CMD_SET_REG;
  c->p1 = r;
  c->p2 = v;
  A->shadow[r - 0x3000] = v;
}

static void run_q(struct asill_s *A)
{
  int i;

  pthread_mutex_lock(&A->cmd_lock);
  for(i = 0; i < A->ncmds; i++) {
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
      ret = libusb_control_transfer(dh, 0x40, c->p1 & 0xff, 0, 0, NULL, 0, 1000);
      break;
    default:
      assert(0);
    }
    if (ret) {
      fprintf(stderr, "control transfer failed: %s(%d)\n", libusb_error_name(ret), ret);
    }
  }
  A->ncmds = 0;
  pthread_mutex_unlock(&A->cmd_lock);
}

static int get_reg(struct asill_s *A, int r)
{
  assert(r >= 0x3000 && r < 0x3200);
  return A->shadow[r - 0x3000];
}

static void setup_frame(struct asill_s *A)
{
#define MAX_COARSE 0x8000
  uint16_t tot_w = A->width + 460;
  uint16_t tot_h = A->height + 26;
  uint16_t coarse;
  double pclk = 48000000.0 * M_PLL_mul[A->pclk] / (N_pre_div[A->pclk] * P1_sys_div[A->pclk] * P2_clk_div[A->pclk]);
  double line_us = tot_w / pclk;

  while( (coarse = A->exp_us / line_us) > MAX_COARSE) {
    tot_w *= 2;
    line_us = tot_w / pclk;
  }
  
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
  send_ctrl(A, 0xac);
  set_reg(A, MT9M034_DIGITAL_BINNING, 0x0000);
  set_reg(A, MT9M034_Y_ADDR_START, 0x0002 + A->start_y);
  set_reg(A, MT9M034_X_ADDR_START, A->start_x);
  set_reg(A, MT9M034_FRAME_LENGTH_LINES, tot_h);
  set_reg(A, MT9M034_Y_ADDR_END, 0x0002 + A->start_y + A->height -1);
  set_reg(A, MT9M034_X_ADDR_END, A->start_x + A->width - 1);
  set_reg(A, MT9M034_DIGITAL_BINNING, 0x0000);
  set_reg(A, 0x306e, 0x9200);
  set_reg(A, MT9M034_LINE_LENGTH_PCK, tot_w);
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, coarse);
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, coarse);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10d8);
  set_reg(A, MT9M034_COLUMN_CORRECTION, 0x0000);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10dc);
  sleep_ms(A, 51);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10d8);
  set_reg(A, MT9M034_COLUMN_CORRECTION, 0xe007);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10dc);
}

static void init(struct asill_s *A)
{
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
  set_reg(A, MT9M034_DAC_LD_24_25, 0xd308);
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

#if 1
  A->width = 1280;
  A->height = 960;
  A->pclk = ASILL_PCLK_25MHZ;
  A->exposure_us = 10000;
  A->start_x = 0;
  A->start_y = 0;
  setup_frame(A);
#else
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, 0x01bc);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10da);
  sleep_ms(A, 101);
  set_reg(A, MT9M034_VT_PIX_CLK_DIV, 0x0002);
  set_reg(A, MT9M034_VT_SYS_CLK_DIV, 0x0008);
  set_reg(A, MT9M034_PRE_PLL_CLK_DIV, 0x0003);
  set_reg(A, MT9M034_PLL_MULTIPLIER, 0x0019);
  sleep_ms(A, 11);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10dc);
  sleep_ms(A, 201);
  send_ctrl(A, 0xac);
  set_reg(A, MT9M034_DIGITAL_BINNING, 0x0000);
  set_reg(A, MT9M034_Y_ADDR_START, 0x0002);
  set_reg(A, MT9M034_X_ADDR_START, 0x0000);
  set_reg(A, MT9M034_FRAME_LENGTH_LINES, 0x03da);
  set_reg(A, MT9M034_Y_ADDR_END, 0x03c1);
  set_reg(A, MT9M034_X_ADDR_END, 0x04ff);
  set_reg(A, MT9M034_DIGITAL_BINNING, 0x0000);
  set_reg(A, 0x306e, 0x9200);
  set_reg(A, MT9M034_LINE_LENGTH_PCK, 0x073e);
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, 0x01bc);
  set_reg(A, MT9M034_COARSE_INTEGRATION_TIME, 0x01bc);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10d8);
  set_reg(A, MT9M034_COLUMN_CORRECTION, 0x0000);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10dc);
  sleep_ms(A, 51);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10d8);
  set_reg(A, MT9M034_COLUMN_CORRECTION, 0xe007);
  set_reg(A, MT9M034_RESET_REGISTER, 0x10dc);
#endif

  /* unity gain */
  set_reg(MT9M034_RED_GAIN, 0x0020);
  set_reg(MT9M034_BLUE_GAIN, 0x0020);
  set_reg(MT9M034_GREEN1_GAIN, 0x0020);
  set_reg(MT9M034_GREEN2_GAIN, 0x0020);
  set_reg(MT9M034_GLOBAL_GAIN, 0x0020);

  /* start capture */
  send_ctrl(0xaa);
  send_ctrl(0xaf);
  sleep_ms(100);
  send_ctrl(0xa9);

  pthread_mutex_unlock(&A->cmd_lock);
}

static void *worker(void *A_)
{
  struct asill_s *A = (struct asill_s *) A_;

  while (A->running) {
    int transfered, ret;

    run_q(A);
    if ((ret = libusb_bulk_transfer(dh, 0x82, A->d, A->width * A->height * 2, &transfered, 1000)) == 0) {
      if (A->data && !A->data_ready) {
	memcpy(A->data, A->d, A->width * A->height * 2);
	A->data_ready = 1;
      }
      if (A->cb) {
	A->cb(A->d, A->width, A->height);
      }
    }
    else {
      fprintf(stderr, "bulk transfer failed: %d\n", ret);
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

  pthread_mutex_lock(&lk);
  if (!ctx) {
    if (!(ret = libusb_init(&ctx))) {
      ctx = NULL;
      fprintf(stderr, "libusb_init failed: %s(%d)\n", libusb_error_name(ret), ret);
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
    
    ret = libusb_get_device_descriptor( dev, &desc );
    if (!ret) {
      if (desc.idVendor == 0x03c3 && desc.idProduct == model) {
	if (n == j) {
	  A = calloc(1, sizeof(*A));
	  ret = libusb_open(device, &A.h);
	  if (ret) {
	    fprintf(stderr, "libusb_open failed: %s(%d)\n", libusb_error_name(ret), ret);
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
    init(A);
    A->max_width = 1280;
    A->max_height = 960;
    A->width = A->max_width;
    A->height = A->max_height;
    A->digital_gain = 0x20;
    A->digital_gain_R = 0x20;
    A->digital_gain_G1 = 0x20;
    A->digital_gain_G2 = 0x20;
    A->digital_gain_B = 0x20;
    A->cb = cb;
    if (has_buffer) {
      A->data = malloc(A->max_width * A->max_height * 2);
    }
    A->d = malloc(A->max_width * A->max_height * 2);
    run_q(A);
    A->running = 1;
    assert(pthread_create(&A->th, NULL, worker, A) == 0);
  }

 asil_new_exit:
  pthread_mutex_unlock(&lk);  
  return A;
}


