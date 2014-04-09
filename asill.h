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

#ifndef _ASILL_H_
#define _ASILL_H_ 1

#include <stdint.h>

#define ASILL_ASI120MM 0x120a
#define ASILL_ASI120MC 0x120b

struct asill_s;

typedef void (*asill_new_frame_f)(unsigned char *data, int width, int height);

struct asill_s *asill_new(uint16_t model, int n, int has_buffer, asill_new_frame_f cb);

#define ASILL_PCLK_25MHZ (0)
#define ASILL_PCLK_24MHZ (1)
#define ASILL_PCLK_40MHZ (2)
#define ASILL_PCLK_48MHZ (3)
#define ASILL_PCLK_96MHZ (4)
#define ASILL_PCLK_8MHZ  (5)
#define ASILL_PCLK_2MHZ  (6)
int asill_sel_pclk(struct asill_s *A, int pclk);
int asill_get_pclk(struct asill_s *A);

#define ASILL_FMT_RAW8 0
#define ASILL_FMT_RAW16 2
int asill_set_wh(struct asill_s *A, uint16_t w, uint16_t h, int bin, int fmt);
uint16_t asill_get_w(struct asill_s *A);
uint16_t asill_get_h(struct asill_s *A);
int asill_get_bin(struct asill_s *A);
uint16_t asill_get_maxw(struct asill_s *A);
uint16_t asill_get_maxh(struct asill_s *A);
int asill_set_xy(struct asill_s *A, uint16_t x, uint16_t y);
uint16_t asill_get_x(struct asill_s *A);
uint16_t asill_get_y(struct asill_s *A);
int asill_get_format(struct asill_s *A);

#define ASILL_PAR_ANALOG_GAIN 0
#define ASILL_PAR_DIGITAL_GAIN 1
#define ASILL_PAR_DIGITAL_GAIN_R 2
#define ASILL_PAR_DIGITAL_GAIN_G1 3
#define ASILL_PAR_DIGITAL_GAIN_G2 4
#define ASILL_PAR_DIGITAL_GAIN_B 5
#define ASILL_PAR_BIAS_SUB 6
#define ASILL_PAR_ROW_DENOISE 7
#define ASILL_PAR_COL_DENOISE 8
#define ASILL_PAR_FLIP_X 9
#define ASILL_PAR_FLIP_Y 10
#define ASILL_PAR_N 11
int asill_set_int_par(struct asill_s *A, int par, int gain);
int asill_get_int_par(struct asill_s *A, int par);

int asill_set_exp_us(struct asill_s *A, uint32_t exp);
uint32_t asill_get_exp_us(struct asill_s *A);
uint32_t asill_get_min_exp_us(struct asill_s *A);
uint32_t asill_get_max_exp_us(struct asill_s *A);

float asill_get_temp(struct asill_s *A);
int asill_is_color(struct asill_s *A);

uint8_t *asill_get_buffer(struct asill_s *A);
void asill_done_buffer(struct asill_s *A);
int asill_buffer2float(struct asill_s *A, float *fb);
int asill_buffer2buffer(struct asill_s *A, void *b);
int asill_set_save(struct asill_s *A, const char *path);
float asill_get_fps(struct asill_s *A);

int asill_save_pars(struct asill_s *A);
int asill_load_pars(struct asill_s *A);

#define ASILL_N 0
#define ASILL_S 1
#define ASILL_E 2
#define ASILL_W 3
void asill_pulse(struct asill_s *A, int dir, int ms);

#endif
