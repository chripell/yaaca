
#include "yaaca_server.h"

#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include "ASICamera2.h"
#include "jsmn.h"
#include "ser.h"
#ifdef SELF_BULK
#include "sb.h"
#endif

#define MAX_IMGS 8
#define MAX_CONTROL ASI_ANTI_DEW_HEATER

struct control_s {
  long value;
  ASI_BOOL is_auto;
};

struct status_capture_s {
  int run_capture;
  struct control_s control[MAX_CONTROL + 1];
  int width;
  int height;
  int bin;
  int type;
  int start_x;
  int start_y;
  int mode;
#define MODE_LONG 1
  ASI_EXPOSURE_STATUS mode_status;
  int cam_dropped;
  int capture_error_status;
  int capture_generation;
};

struct status_save_s {
  int run_save;
  int recording;
  char dest[FILENAME_MAX];
  int auto_debayer;
  int save_error_status;
  int save_generation;
};

struct zwo_s {
  ASI_CAMERA_INFO prop;
  int Offset_HighestDR;
  int Offset_UnityGain;
  int Gain_LowestRN;
  int Offset_LowestRN;
  int ncap;
  ASI_CONTROL_CAPS *cap;
  /* lock_next */
  pthread_mutex_t lock_next;
  struct status_capture_s next_capture;
  struct status_save_s next_save;
  /* lock_next */
  /* lock_current */
  pthread_mutex_t lock_current;
  struct status_capture_s current_capture;
  struct status_save_s current_save;
  /* lock_current */
  /* lock_ubuf */
  pthread_mutex_t lock_ubuf;
  unsigned char *ubuf;
  int ubuf_width;
  int ubuf_height;
  int ubuf_type;
  int ubuf_temp;
  int ubuf_pending;
  /* lock_ubuf */
  int nbuf;
  unsigned char *buf[MAX_IMGS];
  /* lock_buf */
  pthread_mutex_t lock_buf;
  pthread_cond_t new_img;
  int buf_width[MAX_IMGS];
  int buf_height[MAX_IMGS];
  int buf_type[MAX_IMGS];
  int head;
  int tail;
  int dropped;
  int captured;
  int failed;
  int ucaptured;
  /* lock_buf */
  pthread_t capture_th;
  pthread_t save_th;
};

static struct zwo_s *zwo;
static int nzwo = -1;

static int zwo_init(void) {
  int i, r;
  
  nzwo = ASIGetNumOfConnectedCameras();
  zwo = calloc(nzwo, sizeof(struct zwo_s));
  for(i = 0; i < nzwo; i++) {
    r = ASIGetCameraProperty(&zwo[i].prop, i);
    if (r != 0) {
      nzwo = -1;
      return -1000-r;
    }
  }
  return 0;
}

static int zwo_id(int idx) {
  if (idx < 0 || idx >= nzwo) return -6;
  return zwo[idx].prop.CameraID;
}

static void zwo_free(struct zwo_s *z) {
  int i;
  
  if (z->cap) {
    free(z->cap);
    z->cap = NULL;
  }
  z->ncap = 0;
  if (z->ubuf) {
    free(z->ubuf);
    z->ubuf = NULL;
  }
  for(i = 0; i < z->nbuf; i++) {
    if (z->buf[i]) {
      free(z->buf[i]);
      z->buf[i] = NULL;
    }
  }
  z->nbuf = 0;
}

static int zwo_read(int id, struct zwo_s *z, struct status_capture_s *sc) {
  int i,r;
    
  for(i = 0; i < z->ncap; i++) {
    r = ASIGetControlValue(id, z->cap[i].ControlType,
			   &sc->control[i].value, &sc->control[i].is_auto);
    if (r != 0) {
      fprintf(stderr, "ASI error on get control %d set: %d\n", i, r);
      return -1000-r;
    }
  }
  r = ASIGetROIFormat(id, &sc->width, &sc->height, &sc->bin, &sc->type);
  if (r != 0) {
    fprintf(stderr, "ASI error on get ROI: %d\n", r);
    return -1000-r;
  }
  r = ASIGetStartPos(id, &sc->start_x, &sc->start_y);
  if (r != 0) {
    fprintf(stderr, "ASI error on get start pos: %d\n", r);
    return -1000-r;
  }
  r = ASIGetDroppedFrames(id, &sc->cam_dropped); 
  if (r != 0) {
    fprintf(stderr, "ASI error on get Dropped frames: %d\n", r);
    return -1000-r;
  }
  return 0;
}

static int zwo_write(int id, struct zwo_s *z, struct status_capture_s *new,
		     struct status_capture_s *old) {
  int i,r;

  for(i = 0; i < z->ncap; i++)
    if (z->cap[i].IsWritable &&
	(new->control[i].value != old->control[i].value ||
	 new->control[i].is_auto != old->control[i].is_auto)) {
      r = ASISetControlValue(id, z->cap[i].ControlType,
			     new->control[i].value, new->control[i].is_auto);
      if (r != 0) {
	fprintf(stderr, "ASI error on set control %d to %ld, auto %d (from %ld,%d): %d\n",
		i, new->control[i].value, new->control[i].is_auto,
		old->control[i].value, old->control[i].is_auto, r);
	return -1000-r;
      }
    }
  if (new->width != old->width || new->height != old->height ||
      new->bin != old->bin || new->type != old->type) {
    r = ASISetROIFormat(id, new->width, new->height, new->bin, new->type);
    if (r != 0) {
      fprintf(stderr, "ASI error on set ROI: %d\n", r);
      return -1000-r;
    }
  }
  if (new->start_x != old->start_x || new->start_y != old->start_y) {
    r = ASISetStartPos(id, new->start_x, new->start_y);
    if (r != 0) {
      fprintf(stderr, "ASI error on set start pos: %d\n", r);
      return -1000-r;
    }
  }
  return 0;
}

static int zwo_open(int idx) {
  int r, i, id;
  int bsize;
  struct zwo_s *z;
  struct status_capture_s *sc;
  struct status_save_s *ss;
  
  id = zwo_id(idx);
  if (id < 0) return id;
  r = ASIOpenCamera(id);
  if (r != 0) {
    fprintf(stderr, "ASIOpenCamera failed: %d\n", r);
    return -1000-r;
  }
  r = ASIInitCamera(id);
  if (r != 0) {
    fprintf(stderr, "ASIInitCamera failed: %d\n", r);
    return -1000-r;
  }
  z = &zwo[idx];
  r = ASIGetGainOffset(id, &z->Offset_HighestDR, &z->Offset_UnityGain,
		       &z->Gain_LowestRN, &z->Offset_LowestRN);
  if (r != 0) return -1000-r;
  r = ASIGetNumOfControls(id, &z->ncap);
  if (r != 0) return -1000-r;
  r = ASIDisableDarkSubtract(id);
  if (r != 0) return -1000-r;
  z->cap = calloc(z->ncap, sizeof(ASI_CONTROL_CAPS));
  for(i = 0; i < z->ncap; i++) {
    r = ASIGetControlCaps(id, i, &z->cap[i]);
    if (r != 0) {
      zwo_free(z);
      return -1000-r;
    }
  }
  pthread_mutex_init(&z->lock_next, NULL);
  pthread_mutex_init(&z->lock_current, NULL);
  pthread_mutex_init(&z->lock_ubuf, NULL);
  pthread_mutex_init(&z->lock_buf, NULL);
  pthread_cond_init(&z->new_img, NULL);
  z->nbuf = 4;
  bsize = z->prop.MaxHeight * z->prop.MaxWidth * 6;
  z->ubuf = malloc(bsize);
  assert(z->ubuf);
  for(i = 0; i < z->nbuf; i++) {
    z->buf[i] = malloc(bsize);
    assert(z->buf[i]);
  }
  sc = &z->next_capture;
  memset(sc, 0, sizeof(struct status_capture_s));
  ss = &z->next_save;
  memset(ss, 0, sizeof(struct status_save_s));
  r = zwo_read(id, z, sc);
  if (r != 0) return r;
  sc->capture_generation = 0;
  ss->run_save = 1;
  ss->recording = 0;
  snprintf(ss->dest, FILENAME_MAX, "%s/yaaca-", getenv("HOME"));
  ss->auto_debayer = 0;
  ss->save_generation = 0;
  z->current_capture = *sc;
  z->current_save = *ss;
  z->dropped = 0;

  return 0;
}

static int zwo_close(int idx) {
  int r, id;
  
  id = zwo_id(idx);
  if (id < 0) return id;
  r = ASICloseCamera(id);
  if (r != 0) return -1000-r;
  zwo_free(&zwo[idx]);
  return 0;
}

int zwo_imlen(struct zwo_s *z) {
  int l = z->current_capture.width * z->current_capture.height;

  switch (z->current_capture.type) {
  case ASI_IMG_RGB24:
    return l * 3;
  case ASI_IMG_RAW16:
    return l * 2;
  }
  return l;
}

int zwo_uimlen(struct zwo_s *z) {
  int l = z->ubuf_width * z->ubuf_height;

  if (z->prop.IsColorCam && z->current_save.auto_debayer) {
    switch (z->ubuf_type) {
    case ASI_IMG_RAW8:
      return l * 3;
    case ASI_IMG_RAW16:
      return l * 6;
    }
  }
  switch (z->ubuf_type) {
  case ASI_IMG_RGB24:
    return l * 3;
  case ASI_IMG_RAW16:
    return l * 2;
  }
  return l;
}

#define MODB(n) ((n) % z->nbuf)

static void * zwo_capture(void *z_) {
  struct zwo_s *z = z_;
  struct status_capture_s s;
  int r, next, i;
  int id = z->prop.CameraID;
  int quit = 0;
  
  pthread_mutex_lock(&z->lock_next);
  while (z->next_capture.run_capture && !quit) {
    unsigned char *im;
    int tout = 0;
    int mode = z->next_capture.mode;
    pthread_mutex_unlock(&z->lock_next);

    s.capture_error_status = 0;
    
    im = z->buf[z->head];
    if (mode & MODE_LONG) {
      struct timespec ms100 = {0, 100*1000*1000};
      
      r = ASIGetExpStatus(id, &s.mode_status);
      if (r == ASI_SUCCESS) {
	switch (s.mode_status) {
	case ASI_EXP_IDLE:
	case ASI_EXP_FAILED:
	  if (s.mode_status == ASI_EXP_FAILED) {
	    z->failed++;
	    fprintf(stderr, "Exposure failed!\n");
	  }
	  r = ASIStartExposure(id, ASI_FALSE);
	  if (r == ASI_SUCCESS) {
	    nanosleep(&ms100, NULL);
	    r = -1;
	  }
	  break;
	case ASI_EXP_WORKING:
	  nanosleep(&ms100, NULL);
	  r = -1;
	  break;
	case ASI_EXP_SUCCESS:
	  r = ASIGetDataAfterExp(id, im, zwo_imlen(z));
	  if (r == ASI_SUCCESS) {
	    r = ASIStartExposure(id, ASI_FALSE);
	  }
	  break;
	}
      }
    }
    else {
      r = ASIGetVideoData(id, im, zwo_imlen(z), 100);
    }
    switch(r) {
    case -1:
      break;
    case ASI_SUCCESS:
      pthread_mutex_lock(&z->lock_buf);
      z->buf_width[z->head] = z->current_capture.width;
      z->buf_height[z->head] = z->current_capture.height;
      z->buf_type[z->head] = z->current_capture.type;
      next = MODB(z->head + 1);
      if (next == z->tail) {
	z->dropped++;
      }
      else {
	z->captured++;
	z->head = next;
	pthread_cond_signal(&z->new_img);
      }
      pthread_mutex_unlock(&z->lock_buf);
      break;
    case ASI_ERROR_TIMEOUT:
      tout = 1;
      break;
    default:
      fprintf(stderr, "ASI error on capture: %d\n", r);
      s.capture_error_status = -1000 - r;
      quit = 1;
    }

    if (!quit && !tout) {
      pthread_mutex_lock(&z->lock_next);
      s = z->next_capture;
      pthread_mutex_unlock(&z->lock_next);
      r = zwo_write(id, z, &s, &z->current_capture);
      if (r != 0) {
	fprintf(stderr, "ASI error on control set: %d\n", r);
	s.capture_error_status = r;
	quit = 1;
      }
      
      for(i = 0; i < z->ncap; i++) {
	if (s.control[i].is_auto || !z->cap[i].IsWritable) {
	  r = ASIGetControlValue(id, z->cap[i].ControlType,
				 &s.control[i].value, &s.control[i].is_auto);
	  if (r != 0) {
	    fprintf(stderr, "ASI error on control %d read: %d\n", i, r);
	    s.capture_error_status = -1000 - r;
	    quit = 1;
	  }
	  if (z->cap[i].ControlType == ASI_TEMPERATURE)
	    z->ubuf_temp = s.control[i].value;
	}
      }
      pthread_mutex_lock(&z->lock_current);
      z->current_capture = s;
      pthread_mutex_unlock(&z->lock_current);
    }
    
    pthread_mutex_lock(&z->lock_next);
  }
  pthread_mutex_unlock(&z->lock_next);

  pthread_mutex_lock(&z->lock_current);
  z->current_capture.run_capture = 0;
  pthread_mutex_unlock(&z->lock_current);

  return NULL;
}

static void write_hdr_info(struct SERHeader_s *hdr, struct zwo_s *z) {
  if (z->prop.IsColorCam) {
    switch (z->prop.BayerPattern) {
    case ASI_BAYER_RG:
      hdr->ColorID = SER_BAYER_RGGB;
      break;
    case ASI_BAYER_BG:
      hdr->ColorID = SER_BAYER_BGGR;
      break;
    case ASI_BAYER_GR:
      hdr->ColorID = SER_BAYER_GRBG;
      break;
    case ASI_BAYER_GB:
      hdr->ColorID = SER_BAYER_GBRG;
      break;
    }
  }
  else {
    hdr->ColorID = SER_MONO;
  }
}

static void flush_hdr(FILE *f, struct SERHeader_s *hdr, int start_temp, int end_temp, time_t start_t, time_t end_t) {
  fseek(f, 0, SEEK_SET);
  snprintf(hdr->Telescope, SER_MAX_STRING_LEN, "T%.1f(%lx)-%.1f(%lx)", start_temp/10.0, start_t, end_temp/10.0, end_t);
  hdr->FrameCount = hdr->FrameCount;
  fwrite(hdr, sizeof(*hdr), 1, f);
  fclose(f);
}

static void * zwo_save(void *z_) {
  struct zwo_s *z = z_;
  struct timespec tick;
  int csaving = 0;
  int cerror = 0;
  char cdest[FILENAME_MAX];
  FILE *f = NULL;
  int64_t offset_gmt;
  int64_t epochTicks = 621355968000000000LL;
  int64_t ticksPerSecond = 10000000;
  int64_t ticksPerMicrosecond = 10;
  struct tm *tmp;
  time_t now = time(NULL);
  struct SERHeader_s hdr;
  int start_temp = 0;
  time_t start_t;
  struct timeval tv;
  
  tmp = localtime(&now);
  offset_gmt = tmp->tm_gmtoff * ticksPerSecond;
    
  pthread_mutex_lock(&z->lock_next);
  while (z->next_save.run_save) {
    int nsaving = z->next_save.recording;
    int auto_debayer = z->next_save.auto_debayer;
    
    if (csaving == 0 && nsaving == 1) {
      cerror = 0;
      strncpy(cdest, z->next_save.dest, FILENAME_MAX);
      cdest[FILENAME_MAX - 1] = '\0';
    }
    pthread_mutex_unlock(&z->lock_next);

    pthread_mutex_lock(&z->lock_buf);
    if (z->head != z->tail) {
      unsigned char *im = z->buf[z->tail];
      int width = z->buf_width[z->tail];
      int height = z->buf_height[z->tail];
      int type = z->buf_type[z->tail];

      z->ucaptured++;
      pthread_mutex_unlock(&z->lock_buf);

#define R 0
#define G 1
#define B 2
#define OUT(O) (p[3*(i + j * width) + O])
#define IN(DX, DY) ((int) s[i + (DX) + width * (j + (DY))])
#define DEBAYER() do {							\
	int i, j;							\
	j = 0;								\
	for(i = 0; i < width; i++) {					\
	  OUT(R) = 0;							\
	  OUT(G) = 0;							\
	  OUT(B) = 0;							\
	}								\
	j = height - 1;							\
	for(i = 0; i < width; i++) {					\
	  OUT(R) = 0;							\
	  OUT(G) = 0;							\
	  OUT(B) = 0;							\
	}								\
	switch (z->prop.BayerPattern) {					\
	case ASI_BAYER_GR:						\
	  for(j = 1; j < height - 1; j++) {				\
	    i = 0;							\
	    OUT(R) = 0;							\
	    OUT(G) = 0;							\
	    OUT(B) = 0;							\
	    for(i = 1; i < width - 1; i++) {				\
	      if (i&1) {						\
		if (j&1) {						\
		  OUT(R) = (IN(-1, 0) + IN(+1, 0)) / 2;			\
		  OUT(G) = IN(0, 0);					\
		  OUT(B) = (IN(0, -1) + IN(0, +1)) / 2;			\
		}							\
		else {							\
		  OUT(R) = (IN(-1, -1) + IN(+1, +1) + IN(-1, +1) + IN(+1, -1)) / 4; \
		  OUT(G) = (IN(-1, 0) + IN(+1, 0) + IN(0, -1) + IN(0, +1)) / 4; \
		  OUT(B) = IN(0, 0);					\
		}							\
	      }								\
	      else {							\
		if (j&1) {						\
		  OUT(R) = IN(0, 0);					\
		  OUT(G) = (IN(-1, 0) + IN(+1, 0) + IN(0, -1) + IN(0, +1)) / 4; \
		  OUT(B) = (IN(-1, -1) + IN(+1, +1) + IN(-1, +1) + IN(+1, -1)) / 4; \
		}							\
		else {							\
		  OUT(R) = (IN(0, -1) + IN(0, +1)) / 2;			\
		  OUT(G) = IN(0, 0);					\
		  OUT(B) = (IN(-1, 0) + IN(+1, 0)) / 2;			\
		}							\
	      }								\
	    }								\
	    i++;							\
	    OUT(R) = 0;							\
	    OUT(G) = 0;							\
	    OUT(B) = 0;							\
	  }								\
	  break;							\
	case ASI_BAYER_RG:						\
	case ASI_BAYER_BG:						\
	case ASI_BAYER_GB:						\
	  fprintf(stderr, "TODO: finish debayer\n");			\
	}								\
      } while(0)
      
#define OUT2(O) (p[3*(i2 + j2 * width2) + O])
#define FDEBAYER()      do {					\
	int i,j, i2, j2;					\
	int width2 = width /2;					\
	int height2 = height /2;				\
	switch (z->prop.BayerPattern) {				\
	case ASI_BAYER_GR:					\
	  for(j = 0, j2 = 0; j < height; j += 2, j2++) {	\
	    for(i = 0, i2 = 0; i < width ; i += 2, i2++) {	\
	      OUT2(B) = IN(1, 0);				\
	      OUT2(G) = (IN(0, 0) + IN(1, 1)) / 2;		\
	      OUT2(R) = IN(0, 1);				\
	    }							\
	  }							\
	  width = width2;					\
	  height = height2;					\
	  break;						\
	case ASI_BAYER_RG:					\
	case ASI_BAYER_BG:					\
	case ASI_BAYER_GB:					\
	  fprintf(stderr, "TODO: finish debayer\n");		\
	}							\
      } while(0)
	
      pthread_mutex_lock(&z->lock_ubuf);
      if (!z->ubuf_pending) {
	if (type == ASI_IMG_RAW16 && auto_debayer) {
	  unsigned short *p = (unsigned short *) z->ubuf;
	  unsigned short *s = (unsigned short *) im;

	  if (auto_debayer == 1) {
	    DEBAYER();
	  }
	  else {
	    FDEBAYER();
	  }
	}
	else if (type == ASI_IMG_RAW8 && auto_debayer) {
	  unsigned char *p = z->ubuf;
	  unsigned char *s = im;
	
	  if (auto_debayer == 1) {
	    DEBAYER();
	  }
	  else {
	    FDEBAYER();
	  }
	}
	else {
	  memcpy(z->ubuf, im, zwo_imlen(z));
	}
	z->ubuf_width = width;
	z->ubuf_height = height;
	z->ubuf_type = type;
	z->ubuf_pending = 1;
      }
      pthread_mutex_unlock(&z->lock_ubuf);

      /* TODO: save image to file, based on current.recording. */
      if (!cerror) {
	if (csaving == 0 && nsaving == 1) {
	  char fname[FILENAME_MAX];
	  time_t now = time(NULL);

	  snprintf(fname, FILENAME_MAX, "%s%ld.ser", cdest, now);
	  f = fopen(fname, "w");
	  if (!f) {
	    cerror = errno;
	  }
	  else {
	    FILE *ftxt;
	    
	    snprintf(fname, FILENAME_MAX, "%s%ld.txt", cdest, now);
	    ftxt = fopen(fname, "w");
	    if (ftxt) {
	      int i;
	      
	      fprintf(ftxt, "width: %d\nheight: %d\nbin: %d\ntype: %d\n"
		      "start_x: %d\nstart_y: %d\nmode: %d\ncontrols: %d\n",
		      z->current_capture.width,
		      z->current_capture.height,
		      z->current_capture.bin,
		      z->current_capture.type,
		      z->current_capture.start_x,
		      z->current_capture.start_y,
		      z->current_capture.mode,
		      z->ncap);
	      for (i = 0; i < z->ncap; i++) {
		fprintf(ftxt, "%s: %ld, %d\n", z->cap[i].Name, z->current_capture.control[i].value,
			z->current_capture.control[i].is_auto);
	      }
	      fclose(ftxt);
	    }
	    start_temp = z->ubuf_temp;
	    time(&start_t);
	    memset(&hdr, 0, sizeof(hdr));
	    memcpy(hdr.FileID, "LUCAM-RECORDER", sizeof(hdr.FileID));
	    hdr.LittleEndian = 0;
	    switch (type) {
	    case ASI_IMG_RAW8:
	      hdr.PixelDepth = 8;
	      write_hdr_info(&hdr, z);
	      break;
	    case ASI_IMG_RGB24:
	      hdr.PixelDepth = 8;
	      hdr.ColorID = SER_BGR;
	      break;
	    case ASI_IMG_RAW16:
	      hdr.PixelDepth = 16;
	      write_hdr_info(&hdr, z);
	      break;
	    case ASI_IMG_Y8:
	      hdr.PixelDepth = 8;
	      hdr.ColorID = SER_MONO;
	      break;
	    }
	  }
	  hdr.ImageWidth = z->current_capture.width;
	  hdr.ImageHeight = z->current_capture.height;
	  strcpy(hdr.Observer, "YAACA");
	  strncpy(hdr.Instrument, z->prop.Name, SER_MAX_STRING_LEN);
	  hdr.Instrument[SER_MAX_STRING_LEN - 1] = '\0';
	  gettimeofday(&tv, NULL);
	  hdr.DateTimeUTC = tv.tv_sec * ticksPerSecond + tv.tv_usec * ticksPerMicrosecond + epochTicks;
	  hdr.DateTime = hdr.DateTimeUTC + offset_gmt;
	  if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) {
	    cerror = errno;
	    csaving = 0;
	  }
	  else {
	    csaving = 1;
	  }
	}
	if (csaving == 1 && nsaving == 1) {
	  if (!f || fwrite(im, zwo_imlen(z), 1, f) != 1) {
	    if (!f) {
	      fprintf(stderr, "Error saving, inconsistency\n");
	    }
	    else {
	      fprintf(stderr, "Error saving: %s\n", strerror(errno));
	    }
	    cerror = errno;
	    csaving = 0;
	  }
	  else {
	    hdr.FrameCount++;
	  }
	}
	if (csaving == 1 && nsaving == 0) {
	  flush_hdr(f, &hdr, start_temp, z->ubuf_temp, start_t, time(NULL));
	  csaving = 0;
	}
      }

      pthread_mutex_lock(&z->lock_current);
      if (csaving != z->current_save.recording) {
	strncpy(z->current_save.dest, cdest, FILENAME_MAX);
	z->next_save.dest[FILENAME_MAX - 1] = '\0';
      }
      z->current_save.auto_debayer = auto_debayer;
      z->current_save.recording = csaving;
      z->current_save.save_error_status = cerror;
      pthread_mutex_unlock(&z->lock_current);
      
      pthread_mutex_lock(&z->lock_buf);
      z->tail = MODB(z->tail + 1);
    }
    pthread_mutex_unlock(&z->lock_buf);

    clock_gettime(CLOCK_REALTIME, &tick);
    /* Note, we shall use CLOCK_MONOTONIC, but support of
       pthread_condattr_setclock is very sketchy.  */
    tick.tv_nsec += 100000000;
    if (tick.tv_nsec > 1000000000) {
      tick.tv_nsec -= 1000000000;
      tick.tv_sec++;
    }
    
    pthread_mutex_lock(&z->lock_buf);
    if (z->head == z->tail)
      pthread_cond_timedwait(&z->new_img, &z->lock_buf, &tick);
    pthread_mutex_unlock(&z->lock_buf);
    
    pthread_mutex_lock(&z->lock_next);
  }
  pthread_mutex_unlock(&z->lock_next);

  if (csaving) {
    flush_hdr(f, &hdr, start_temp, z->ubuf_temp, start_t, time(NULL));
  }
  
  pthread_mutex_lock(&z->lock_current);
  z->current_save.run_save = 0;
  pthread_mutex_unlock(&z->lock_current);
  return NULL;
}

int zwo_start(int idx) {
  int r, id;
  struct zwo_s *z;
  struct status_capture_s *sc;
  struct status_save_s *ss;
  
  id = zwo_id(idx);
  if (id < 0) return id;
  z = &zwo[idx];
  
  sc = &z->current_capture;
  ss = &z->current_save;
  r = zwo_read(id, z, sc);
  if (r != 0) return r;
  sc->capture_error_status = 0;
  ss->save_error_status = 0;

  sc = &z->next_capture;
  ss = &z->next_save;
  sc->run_capture = 1;
  ss->run_save = 1;

  z->captured = 0;
  z->ucaptured = 0;
  z->failed = 0;
  
  z->head = 0;
  z->tail = 0;
  z->ubuf_width = 0;
  z->ubuf_height = 0;
  if (sc->mode & MODE_LONG)
    r = ASIStartExposure(id, ASI_FALSE);
  else
    r = ASIStartVideoCapture(id); 
  if (r != 0) return -1000-r;

  pthread_create(&z->capture_th, NULL, zwo_capture, z);
  pthread_create(&z->save_th, NULL, zwo_save, z);
  return 0;
}

int zwo_stop(int idx) {
  int id, r;
  struct zwo_s *z;
  
  id = zwo_id(idx);
  if (id < 0) return id;
  z = &zwo[idx];
  pthread_mutex_lock(&z->lock_next);
  z->next_capture.run_capture = 0;
  z->next_save.run_save = 0;
  pthread_mutex_unlock(&z->lock_next);
  pthread_join(z->capture_th, NULL);
  pthread_join(z->save_th, NULL);
  if (z->next_capture.mode & MODE_LONG)
    r = ASIStopExposure(id);
  else
    r = ASIStopVideoCapture(id); 
  if (r != 0) return -1000-r;
  return 0;
}

#define MAX_TOKEN 128
#define MAX_BUF 100

static const char *str;
static jsmntok_t tok[MAX_TOKEN];
static int ntok;

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
        if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
                        strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
                return 0;
        }
	return -1;
}

static int parse(const char *t) {
  jsmn_parser p;

  str = t;
  jsmn_init(&p);
  ntok = jsmn_parse(&p, str, strlen(str), tok, MAX_TOKEN);
  if (ntok < 0)
    return -2;
  if (ntok < 1 || tok[0].type != JSMN_OBJECT) {
    return -3;
  }
  return 0;
}

static int get_string(const char *s, char *b, int n) {
  int i;
  
  for (i = 0; i < ntok; i++)
    if (jsoneq(str, &tok[i], s) == 0) {
      snprintf(b, n, "%.*s", tok[i+1].end - tok[i+1].start, str + tok[i+1].start);
      return 0;
    }
  return -1;
}

static int get_int(const char *s, long *v) {
  int i;
  
  for (i = 0; i < ntok; i++)
    if (jsoneq(str, &tok[i], s) == 0) {
      *v = strtoul(str + tok[i+1].start, NULL, 0);
      return 0;
    }
  return -1;
}

static int get_idx() {
  long val;

  if (get_int("idx", &val) != 0)
    return -7;
  return val;
}

static int get_array(const char *s, long *a, int n, int is_bool) {
  int m, i, j;
  
  for (i = 0; i < ntok; i++)
    if (jsoneq(str, &tok[i], s) == 0) {
      if (tok[i+1].type != JSMN_ARRAY) {
	continue;
      }
      m = tok[i+1].size;
      if (n < m)
	m = n;
      for (j = 0; j < m; j++) {
	jsmntok_t *g = &tok[i+j+2];

	if (is_bool)
	  a[j] = *(str + g->start) == 't';
	else
	  a[j] = strtoul(str + g->start, NULL, 0);
      }
      return m;
    }
  return -1;
}

static char* format_bins(char* b, int *bins, int len, int end) {
  int i,j;

  j = 0;
  b[j++] = '[';
  for(i = 0; bins[i] != end && i < len; i++) {
    b[j++] = '0' + bins[i];
    b[j++] = ',';
  }
  b[j-1] = ']';
  b[j] = '\0';
  return b;
}

#define PRINTF(format, ...) do {				\
    int r = snprintf(resp, len, format, ##__VA_ARGS__ );	\
    if (r >= len) return -1;					\
    len -= r;							\
    resp += r;							\
  } while(0)

#define BOOL(x) ((x) ? "true" : "false")

#define GET_INT(from, dest) do { \
    long v;			 \
    if (get_int(#from, &v) == 0) \
      dest = v;			 \
  } while(0)

int yaaca_cmd(const char *req, char *resp, int len) {
  int i, r;
  int ret = 0;
  char buf[MAX_BUF];

  if (nzwo < 0) {
    r = zwo_init();
    if (r != 0) return r;
  }
    
  r = parse(req);
  if (r < 0) return r;

  r = get_string("cmd", buf, MAX_BUF);
  if (r < 0) return -4;
  
  if (!strcmp(buf, "list")) {
    PRINTF("[");
    for(i = 0; i < nzwo; i++) {
      ASI_CAMERA_INFO *z = &zwo[i].prop;
      char bin_buf[50], vid_buf[50];
      
      PRINTF("{\"Name\":\"%s\","
	     "\"CameraID\":%d,"
	     "\"MaxHeight\":%ld,"
	     "\"MaxWidth\":%ld,"
	     "\"IsColorCam\":%s,"
	     "\"BayerPattern\":%d,"
	     "\"SupportedBins\":%s,"
	     "\"SupportedVideoFormat\":%s,"
	     "\"PixelSize\":%f,"
	     "\"MechanicalShutter\":%s,"
	     "\"ST4Port\":%s,"
	     "\"IsCoolerCam\":%s,"
	     "\"IsUSB3Host\":%s,"
	     "\"IsUSB3Camera\":%s,"
	     "\"ElecPerADU\":%f}",
	     z->Name,
	     z->CameraID,
	     z->MaxHeight,
	     z->MaxWidth,
	     BOOL(z->IsColorCam),
	     z->BayerPattern,
	     format_bins(bin_buf, z->SupportedBins, 16, 0),
	     format_bins(vid_buf, z->SupportedVideoFormat, 8, ASI_IMG_END),
	     z->PixelSize,
	     BOOL(z->MechanicalShutter),
	     BOOL(z->ST4Port),
	     BOOL(z->IsCoolerCam),
	     BOOL(z->IsUSB3Host),
	     BOOL(z->IsUSB3Camera),
	     z->ElecPerADU);
      if (i != nzwo - 1)
	PRINTF(",");
    }
    PRINTF("]");
  }
  else if(!strcmp(buf, "open")) {
    int idx = get_idx();

    if (idx < 0) return idx;
    ret = zwo_open(idx);
  }
  else if(!strcmp(buf, "close")) {
    int idx = get_idx();

    if (idx < 0) return idx;
    ret = zwo_close(idx);
  }
  else if(!strcmp(buf, "prop")) {
    int idx = get_idx();
    struct zwo_s *z;

    if (idx < 0) return idx;
    z = &zwo[idx];
    PRINTF("{");
    PRINTF("\"controls\":[");
    for(i = 0; i < z->ncap; i++) {
       ASI_CONTROL_CAPS *c = &z->cap[i];
      
      PRINTF("{\"Name\":\"%s\","
	     "\"Description\":\"%s\","
	     "\"MaxValue\":%ld,"
	     "\"MinValue\":%ld,"
	     "\"DefaultValue\":%ld,"
	     "\"IsAutoSupported\":%s,"
	     "\"IsWritable\":%s,"
	     "\"ControlType\":%d}",
	     c->Name,
	     c->Description,
	     c->MaxValue,
	     c->MinValue,
	     c->DefaultValue,
	     BOOL(c->IsAutoSupported),
	     BOOL(c->IsWritable),
	     c->ControlType);
      if (i != zwo[idx].ncap - 1)
	PRINTF(",");
    }
    PRINTF("],\"Offset_HighestDR\":%d,"
	   "\"Offset_UnityGain\":%d,"
	   "\"Gain_LowestRN\":%d,"
	   "\"Offset_LowestRN\":%d}",
	   z->Offset_HighestDR,
	   z->Offset_UnityGain,
	   z->Gain_LowestRN,
	   z->Offset_LowestRN);
  }
  else if(!strcmp(buf, "start")) {
    int idx = get_idx();

    if (idx < 0) return idx;
    ret = zwo_start(idx);
  }
  else if(!strcmp(buf, "stop")) {
    int idx = get_idx();

    if (idx < 0) return idx;
    ret = zwo_stop(idx);
  }
  else if(!strcmp(buf, "stat")) {
    int idx = get_idx();
    struct zwo_s *z;
    struct status_capture_s sc;
    struct status_save_s ss;
    int dropped, captured, ucaptured, failed;

    if (idx < 0) return idx;
    z = &zwo[idx];
    pthread_mutex_lock(&z->lock_current);
    sc = z->current_capture;
    ss = z->current_save;
    pthread_mutex_unlock(&z->lock_current);
    pthread_mutex_lock(&z->lock_buf);
    dropped = z->dropped;
    captured = z->captured;
    failed = z->failed;
    ucaptured = z->ucaptured;
    pthread_mutex_unlock(&z->lock_buf);
    PRINTF("{\"run_capture\":%d,\"vals\":["
	   ,sc.run_capture);
    for(i = 0; i < z->ncap; i++) {
      PRINTF("%ld", sc.control[i].value);
      if (i != z->ncap - 1)
	PRINTF(",");
    }
    PRINTF("],\"auto\":[");
    for(i = 0; i < z->ncap; i++) {
      PRINTF("%s", BOOL(sc.control[i].is_auto));
      if (i != z->ncap - 1)
	PRINTF(",");
    }
    PRINTF("],\"width\":%d,"
	   "\"height\":%d,"
	   "\"bin\":%d,"
	   "\"type\":%d,"
	   "\"start_x\":%d,"
	   "\"start_y\":%d,"
	   "\"mode\":%d,"
	   "\"mode_status\":%d,"
	   "\"cam_dropped\":%d,"
	   "\"capture_error_status\":%d,"
	   "\"capture_generation\":%d,"
	   "\"run_save\":%d,"
	   "\"recording\":%d,"
	   "\"dest\":\"%s\","
	   "\"auto_debayer\":%d,"
	   "\"save_error_status\":%d,"
	   "\"save_generation\":%d,"
	   "\"dropped\":%d,"
	   "\"ucaptured\":%d,"
	   "\"failed\":%d,"
	   "\"captured\":%d}",
	   sc.width,
	   sc.height,
	   sc.bin,
	   sc.type,
	   sc.start_x,
	   sc.start_y,
	   sc.mode,
	   sc.mode_status,
	   sc.cam_dropped,
	   sc.capture_error_status,
	   sc.capture_generation,
	   ss.run_save,
	   ss.recording,
	   ss.dest,
	   ss.auto_debayer,
	   ss.save_error_status,
	   ss.save_generation,
	   dropped,
	   ucaptured,
	   failed,
	   captured);
  }
  else if(!strcmp(buf, "set")) {
    int idx = get_idx();
    struct zwo_s *z;
    struct status_capture_s sc;
    struct status_save_s ss;
    int running, narr;
    long arr[MAX_CONTROL + 1];

    if (idx < 0) return idx;
    z = &zwo[idx];
    pthread_mutex_lock(&z->lock_current);
    running = z->current_capture.run_capture;
    pthread_mutex_unlock(&z->lock_current);
    if (running) {
      sc = z->next_capture;
      ss = z->next_save;
    }
    else {
      sc = z->current_capture;
      ss = z->current_save;
    }
    if ((narr  = get_array("vals", arr, MAX_CONTROL + 1, 0)) >  0) {
      for(i = 0; i < narr; i++) {
	sc.control[i].value = arr[i];
      }
    }
    if ((narr  = get_array("auto", arr, MAX_CONTROL + 1, 1)) >  0) {
      for(i = 0; i < narr; i++) {
	sc.control[i].is_auto = arr[i];
      }
    }
    GET_INT(width, sc.width);
    GET_INT(height, sc.height);
    GET_INT(bin, sc.bin);
    GET_INT(type, sc.type);
    GET_INT(start_x, sc.start_x);
    GET_INT(start_y, sc.start_y);
    GET_INT(mode, sc.mode);
    GET_INT(capture_generation, sc.capture_generation);
    GET_INT(recording, ss.recording);
    get_string("dest", ss.dest, FILENAME_MAX);
    GET_INT(auto_debayer, ss.auto_debayer);
    GET_INT(save_generation, ss.save_generation);
    if (running) {
      pthread_mutex_lock(&z->lock_next);
      z->next_capture = sc;
      z->next_save = ss;
      pthread_mutex_unlock(&z->lock_next);
    }
    else {
      r = zwo_write(z->prop.CameraID, z, &sc, &z->current_capture);
      if (r != 0) return r;
      z->current_capture = sc;
      z->next_capture = sc;
      z->current_save = ss;
      z->next_save = ss;
    }
  }
  else if(!strcmp(buf, "data")) {
    int idx = get_idx();
    struct zwo_s *z;
    int ret = 0;
    int imlen;
    
    if (idx < 0) return idx;
    z = &zwo[idx];
    pthread_mutex_lock(&z->lock_ubuf);
    imlen = zwo_uimlen(z);
    if (imlen <= len)
      memcpy(resp, z->ubuf, zwo_uimlen(z));
    else {
      fprintf(stderr, "Expected buffer %d, got %d\n", imlen, len);
      ret = -1;
    }
    z->ubuf_pending = 0;
    pthread_mutex_unlock(&z->lock_ubuf);
    return ret;
  }
  else if(!strcmp(buf, "pulse")) {
    int idx = get_idx();
    int ret = 0;
    ASI_GUIDE_DIRECTION dir = 0;
    int on = 0;
    
    if (idx < 0) return idx;
    GET_INT(dir, dir);
    GET_INT(on, on);
    if (on) {
      ret = ASIPulseGuideOn(idx, dir);
    }
    else {
      ret = ASIPulseGuideOff(idx, dir);
    }
    if (ret != ASI_SUCCESS)
      ret -= 1000;
    return ret;
  }
  else {
    return -5;
  }
  return ret;
}
