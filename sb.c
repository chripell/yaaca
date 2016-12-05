#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <sched.h>

#include "libusb.h"

extern int (*hack_libusb_submit_transfer)(struct libusb_transfer *transfer);
extern int (*hack_libusb_cancel_transfer)(struct libusb_transfer *transfer);

#define SBI 33
#define SBO 33

static struct libusb_transfer *saved_transfer[SBI];
static struct libusb_transfer *my_transfer[SBO];
static volatile int status;
#define IDLE 0
#define LOADING 1
#define SUBMITTING 2
static int total, total_i, run;
static int current, current_i;
static int debug;
static int big_chunk = 8*1024*1024 - 256*1024;
static int big_inflight = 2;
static int small_chunk = 1024*1024;
static int small_inflight = 15;
static unsigned char *data;

static void cancel_trans() {
  int i;
  
  if (debug) {
    fprintf(stderr, "cancelling %d\n", run);
  }
  for(i = 0; i < run; i++) {
    my_transfer[i]->endpoint = 130;
    libusb_cancel_transfer(my_transfer[i]);
    sched_yield();
  }
}

static void signal_one(enum libusb_transfer_status st, int i) {
  saved_transfer[i]->status = st;
  saved_transfer[i]->endpoint = 129;
  if (st == 0)
    saved_transfer[i]->actual_length = saved_transfer[i]->length;
  else
    saved_transfer[i]->actual_length = 0;
  if (debug) {
    fprintf(stderr, "signaling %d/%d to %d\n", st, saved_transfer[i]->actual_length, i);
  }
  saved_transfer[i]->callback(saved_transfer[i]);
}

static void submit_cb_one(struct libusb_transfer *transfer) {
  long i = (long) transfer->user_data;

  if (debug) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    fprintf(stderr, "%ld.%06ld cb %ld = %d, status %d\n", tv.tv_sec, tv.tv_usec, i, transfer->status, status);
  }
  if (status != SUBMITTING && transfer->status != LIBUSB_TRANSFER_CANCELLED) {
    if (debug) {
      fprintf(stderr, "spurious cb\n");
    }
  }
  if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
    if (run < total_i) {
      int r;
      
      if (debug) {
	fprintf(stderr, "submitting %d\n", run);
      }
      r = libusb_submit_transfer(my_transfer[run]);
      if (r != 0) {
	fprintf(stderr, "submit failed: %d\n", r);
	cancel_trans();
	status = IDLE;
	signal_one(LIBUSB_TRANSFER_ERROR, 0);
	return;
      }
      run++;
    } else if (i == total_i - 1) {
      status = IDLE;
      signal_one(LIBUSB_TRANSFER_COMPLETED, 0);
    }
  }
  else if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
    if (debug) {
      fprintf(stderr, "cancelled %ld\n", i);
    }
  }
  else {
    cancel_trans();
    status = IDLE;
    signal_one(transfer->status, 0);
  }
}

static void submit_cb_stream(struct libusb_transfer *transfer) {
  long i = (long) transfer->user_data;

  if (debug) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    fprintf(stderr, "%ld.%06ld cb %ld = %d, status %d\n", tv.tv_sec, tv.tv_usec, i, transfer->status, status);
  }
  if (status != SUBMITTING && transfer->status != LIBUSB_TRANSFER_CANCELLED) {
    if (debug) {
      fprintf(stderr, "spurious cb\n");
    }
  }
  if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
    signal_one(LIBUSB_TRANSFER_COMPLETED, i);
    if (run < total_i) {
      int r;
      
      if (debug) {
	fprintf(stderr, "submitting %d\n", run);
      }
      r = libusb_submit_transfer(my_transfer[run]);
      if (r != 0) {
	fprintf(stderr, "submit failed: %d\n", r);
	cancel_trans();
	status = IDLE;
	signal_one(LIBUSB_TRANSFER_ERROR, run);
	return;
      }
      run++;
    } else if (i == total_i - 1) {
      if (debug) {
	fprintf(stderr, "all done\n");
      }
      status = IDLE;
    }
  }
  else if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
    if (debug) {
      fprintf(stderr, "cancelled %ld\n", i);
    }
  }
  else {
    cancel_trans();
    status = IDLE;
    signal_one(transfer->status, i);
  }
}

static int my_libusb_submit_transfer(struct libusb_transfer *transfer) {
  int one = 0;
  
  if (status == SUBMITTING) {
    fprintf(stderr, "New transfers while submitting.\n");
    cancel_trans();
    status = IDLE;
  }
  if (status == IDLE) {
    if (debug) {
      fprintf(stderr, "0 submit %p: %p %d tout %d flags 0x%x\n", transfer, transfer->buffer, transfer->length, transfer->timeout, transfer->flags);
    }
    data = transfer->buffer;
    current = transfer->length;
    current_i = 1;
    saved_transfer[0] = transfer;
    one = 1;
  }
  if (status == LOADING) {
    if (debug) {
      fprintf(stderr, "%d submit %p: %p %d tout %d flags 0x%x\n", current_i, transfer, transfer->buffer, transfer->length, transfer->timeout, transfer->flags);
    }
    current += transfer->length;
    saved_transfer[current_i] = transfer;
    current_i++;
    one = 0;
  }
  if (current > 0)
    status = LOADING;
  if (current >= total) {
    int n;
    int chunk = one ? big_chunk : small_chunk;
    int inflight = one ? big_inflight : small_inflight;

    status = SUBMITTING;
    if (current > total) {
      fprintf(stderr, "Too much data: %d vs %d.\n", current, total);
    }
    for(n = 0, total_i = 0; n < current; n+=chunk, total_i++) {
      int len = chunk;
      int tout = saved_transfer[0]->timeout;

      if (len > (current - n))
	len = current - n;
      if (debug) {
	fprintf(stderr, "filling %d: %d\n", total_i, len);
      }
      libusb_fill_bulk_transfer(my_transfer[total_i],
				saved_transfer[0]->dev_handle, 130,
				&data[n], len,
				one ? submit_cb_one : submit_cb_stream,
				(void *) (long) total_i, tout);
    }
    run = 0;
    while (run < inflight && run < total_i) {
      int r;

      if (debug) {
	struct timeval tv;

	gettimeofday(&tv, NULL);
	fprintf(stderr, "%ld.%06ld submitting %d\n", tv.tv_sec, tv.tv_usec, run);
      }
      r = libusb_submit_transfer(my_transfer[run]);
      if (r != 0) {
	fprintf(stderr, "submit failed: %d\n", r);
	cancel_trans();
	status = IDLE;
	return r;
      }
      run++;
    }
  }
  return 0;
}

static int my_libusb_cancel_transfer(struct libusb_transfer *transfer) {
  if (debug) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    fprintf(stderr, "%ld.%06ld got cancel %d %p\n", tv.tv_sec, tv.tv_usec, run, transfer);
  }
  cancel_trans();
  status = IDLE;
  run = 0;
  transfer->status = LIBUSB_TRANSFER_CANCELLED;
  transfer->actual_length = 0;
  transfer->endpoint = 129;
  transfer->callback(transfer);
  return 0;
}

void sb_init(int bsize) {
  int i;
  
  if (getenv("SB_DEBUG"))
    debug = 1;
  total = bsize;
  for (i=0; i < SBO; i++) {
    my_transfer[i] = libusb_alloc_transfer(0);
    if (!my_transfer[i]) {
      fprintf(stderr, "alloc transfer failed\n");
      exit(1);
    }
  }
  hack_libusb_submit_transfer = my_libusb_submit_transfer;
  hack_libusb_cancel_transfer = my_libusb_cancel_transfer;
  if (debug)
    fprintf(stderr, "SB total %d\n", total);
}
