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

#define ASILL_PCLK_24MHZ (0)
#define ASILL_PCLK_40MHZ (1)
#define ASILL_PCLK_48MHZ (2)
#define ASILL_PCLK_96MHZ (3)
#define ASILL_PCLK_8MHZ  (4)
#define ASILL_PCLK_2MHZ  (5)
int asill_sel_pclk(struct asill_s *A, int pclk);
int asill_get_pclk(struct asill_s *A);

int asill_set_wh(struct asill_s *A, uint16_t w, uint16_t h, int bin);
uint16_t asill_get_w(struct asill_s *A);
uint16_t asill_get_h(struct asill_s *A);
int asill_get_bit(struct asill_s *A);
uint16_t asill_get_maxw(struct asill_s *A);
uint16_t asill_get_maxh(struct asill_s *A);
int asill_set_xy(struct asill_s *A, uint16_t x, uint16_t y);

int asill_set_analog_gain(struct asill_s *A, int gain);
int asill_set_digital_gain(struct asill_s *A, int gain, int gainR, int gainG1, int gainG2, int gainB);

int asill_set_exp_us(struct asill_s *A, uint32_t exp);
uint32_t asill_get_exp_us(struct asill_s *A);
uint32_t asill_get_min_exp_us(struct asill_s *A);
uint32_t asill_get_max_exp_us(struct asill_s *A);

int asill_set_bias_sub(struct asill_s *A, int on);
int asill_set_row_denoise(struct asill_s *A, int on);
int asill_set_col_denoise(struct asill_s *A, int on);

float asill_get_temp(struct asill_s *A);
int asill_is_color(struct asill_s *A);

uint8_t *asill_get_buffer(struct asill_s *A);
void asill_done_buffer(struct asill_s *A);
int asill_set_save(struct asill_s *A, const char *path);

#endif
