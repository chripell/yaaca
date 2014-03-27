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
int asill_get_pclk_mhz(struct asill_s *A);

int asill_set_wh(struct asill_s *A, uint16_t w, uint16_t h);
uint16_t asill_get_w(struct asill_s *A);
uint16_t asill_get_h(struct asill_s *A);

uint8_t *asill_get_buffer(struct asill_s *A);
void asill_done_buffer(struct asill_s *A);

#endif
