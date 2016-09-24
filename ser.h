#ifndef _ser_h_
#define _ser_h_ 1

#include <stdint.h>
#include <sys/types.h>

#define SER_MONO             0
#define SER_BAYER_RGGB       8
#define SER_BAYER_GRBG       9
#define SER_BAYER_GBRG       10
#define SER_BAYER_BGGR       11
#define SER_BAYER_CYYM       16
#define SER_BAYER_YCMY       17
#define SER_BAYER_YMCY       18
#define SER_BAYER_MYYC       19
#define SER_RGB              100
#define SER_BGR              101

#define SER_MAX_STRING_LEN   40

struct SERHeader_s {
  char      FileID[14];
  uint32_t  LuID;
  uint32_t  ColorID;
  uint32_t  LittleEndian;
  uint32_t  ImageWidth;
  uint32_t  ImageHeight;
  uint32_t  PixelDepth;
  uint32_t  FrameCount;
  char      Observer[SER_MAX_STRING_LEN];
  char      Instrument[SER_MAX_STRING_LEN];
  char      Telescope[SER_MAX_STRING_LEN];
  int64_t   DateTime;
  int64_t   DateTimeUTC;
} __attribute__((packed));

#endif
