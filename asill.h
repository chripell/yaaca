#ifndef _ASILL_H_
#define _ASILL_H_ 1

#include <stdint.h>

#define ASILL_ASI120MM 0x120a
#define ASILL_ASI120MC 0x120b

struct asill_s;

typedef void (*asill_new_frame_f)(unsigned char *data, int widt, int height);

struct asill_s *asill_new(uint16_t model, int n, int has_buffer, asill_new_frame_f cb);

#endif
