#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <sys/time.h>
#include <arpa/inet.h>

#ifdef SELF_BULK
#include "sb.h"
#endif

#include "ASICamera2.h"
#include "ser.h"

#define CHECK1(X) do {						\
    int r=(X);							\
    if (r != ASI_SUCCESS) {					\
      fprintf(stderr, "ASI Error on '%s': %d\n", #X, r);	\
      exit(1);							\
    }								\
  } while(0)

#define CHECK(X) do {						\
    fprintf(stderr, "%s\n", #X);				\
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
  unsigned int exposure, gain, mode, nzwo, bin, lm;
  ASI_CAMERA_INFO prop;
  unsigned char *buff;
  int bsize, n=0, max = 0;
  float sum = 0.0;
  int width, height;
  
  if (argc != 9) {
    printf("Usage: %s [camera idx] [mode] [bin] [exposure in ms] [gain] [long mode] [w if >0] [h if>0]\n", argv[0]);
    exit(1);
  }
  cidx = strtoul(argv[1], NULL, 0);
  mode = strtoul(argv[2], NULL, 0);
  bin = strtoul(argv[3], NULL, 0);
  exposure = strtoul(argv[4], NULL, 0);
  gain = strtoul(argv[5], NULL, 0);
  lm = strtoul(argv[6], NULL, 0);
  width = strtol(argv[7], NULL, 0);
  height = strtol(argv[8], NULL, 0);
  nzwo = ASIGetNumOfConnectedCameras();
  if (cidx >= nzwo) {
    fprintf(stderr, "Camera out of range %d/%d\n", cidx, nzwo);
    exit(1);
  }
  CHECK(ASIGetCameraProperty(&prop, cidx));
  CHECK(ASIOpenCamera(cidx));
  CHECK(ASIInitCamera(cidx));
  if (width <= 0)
    width = prop.MaxWidth/bin;
  if (height <= 0)
    height = prop.MaxHeight/bin;
  fprintf(stderr, "%d x %d\n", width, height);
  bsize = width * height;
  if (mode == 1)
    bsize *= 3;
  else if (mode == 2)
    bsize *= 2;
  buff = malloc(bsize);
  CHECK(ASISetROIFormat(cidx, width, height, bin, mode));
  set_cap(ASI_EXPOSURE, exposure * 1000);
  set_cap(ASI_GAIN, gain);
  set_cap(ASI_HARDWARE_BIN, ASI_TRUE);
#ifdef SELF_BULK
  sb_init(bsize);
#endif
  if (lm == 0)
    CHECK(ASIStartVideoCapture(cidx));
  while (1) {
    int r;
    struct timespec start, stop;
    long delta;

    clock_gettime(CLOCK_MONOTONIC, &start);
    if (lm == 1) {
      ASI_EXPOSURE_STATUS s;
	
      CHECK1(ASIGetExpStatus(cidx, &s));
      if (s != ASI_EXP_IDLE && s != ASI_EXP_FAILED) {
	fprintf(stderr, "Not idle or failed: %d\n", s);
	exit(1);
      }
      CHECK(ASIStartExposure(cidx, 0));
      fprintf(stderr, "START\n");
      r = 99;			/* timeout */
      do {
	usleep(10*1000);
	clock_gettime(CLOCK_MONOTONIC, &stop);
	delta = (stop.tv_sec - start.tv_sec) * 1000 +
	  (stop.tv_nsec - start.tv_nsec) / 1000000;
	CHECK1(ASIGetExpStatus(cidx, &s));
	if (s == ASI_EXP_IDLE) {
	  fprintf(stderr, "start\n");
	  CHECK(ASIStartExposure(cidx, 0));
	}
	else if (s == ASI_EXP_SUCCESS) {
	  r = 0;
	  fprintf(stderr, "FINISHED\n");
	  CHECK(ASIGetDataAfterExp(cidx, buff, bsize));
	  break;
	} else if (s == ASI_EXP_FAILED) {
	  r = 98;
	  break;
	}
	else if (s != ASI_EXP_WORKING) {
	  fprintf(stderr, "Unexpected status\n");
	  exit(1);
	}
      } while (delta < 20 * exposure + 500);
    }
    else {
      fprintf(stderr, "ASIGetVideoData\n");
      r = ASIGetVideoData(cidx, buff, bsize, 20 * exposure + 500);
      clock_gettime(CLOCK_MONOTONIC, &stop);
      delta = (stop.tv_sec - start.tv_sec) * 1000 +
	(stop.tv_nsec - start.tv_nsec) / 1000000;
    }
    n++;
    if (delta > max)
      max = delta;
    sum += delta;
    fprintf(stderr, ":%2d %6.0f(%6d) %ld\n",
	    r, sum / n, max, delta);
  }
  return 0;
}
