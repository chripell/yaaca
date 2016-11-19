#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include "ASICamera2.h"
#include "ser.h"

#define CHECK(X) do {						\
    int r=(X);							\
    if (r != ASI_SUCCESS) {					\
      fprintf(stderr, "ASI Error on '%s': %d\n", #X, r);	\
      exit(1);							\
    }								\
  } while(0)

static unsigned int cidx;

static void set_cap(int idx, int val) {
  CHECK(ASISetControlValue(cidx, idx, val, 0));
}

int main(int argc, char *argv[]) {
  unsigned int exposure, gain, mode, nzwo, bin;
  ASI_CAMERA_INFO prop;
  unsigned char *buff;
  int bsize, n=0, max = 0;
  float sum = 0.0;
  
  if (argc != 6) {
    printf("Usage: %s [camera idx] [mode] [bin] [exposure in ms] [gain]\n", argv[0]);
    exit(1);
  }
  cidx = strtoul(argv[1], NULL, 0);
  mode = strtoul(argv[2], NULL, 0);
  bin = strtoul(argv[3], NULL, 0);
  exposure = strtoul(argv[4], NULL, 0);
  gain = strtoul(argv[5], NULL, 0);
  nzwo = ASIGetNumOfConnectedCameras();
  if (cidx >= nzwo) {
    fprintf(stderr, "Camera out of range %d/%d\n", cidx, nzwo);
    exit(1);
  }
  CHECK(ASIGetCameraProperty(&prop, cidx));
  bsize = prop.MaxHeight * prop.MaxWidth * 6;
  buff = malloc(bsize);
  CHECK(ASIOpenCamera(cidx));
  CHECK(ASISetROIFormat(cidx, prop.MaxWidth/bin, prop.MaxHeight/bin, bin, mode));
  set_cap(ASI_EXPOSURE, exposure * 1000);
  set_cap(ASI_GAIN, gain);
  CHECK(ASIStartVideoCapture(cidx));
  while (1) {
    int r;
    struct timespec start, stop;
    long delta;

    clock_gettime(CLOCK_MONOTONIC, &start);
    r = ASIGetVideoData(cidx, buff, bsize, 2 * exposure + 500);
    n++;
    clock_gettime(CLOCK_MONOTONIC, &stop);
    delta = (stop.tv_sec - start.tv_sec) * 1000 +
      (stop.tv_nsec - start.tv_nsec) / 1000000;
    if (delta > max)
      max = delta;
    sum += delta;
    fprintf(stderr, "\r%2d %6.0f(%6d) %ld                            ",
	    r, sum / n, max, delta);
  }
  return 0;
}
