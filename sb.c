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

#define SB 40

static struct libusb_transfer *saved_transfer[SB];
static struct libusb_transfer *my_transfer[SB];
static volatile int status;
#define IDLE 0
#define LOADING 1
#define SUBMITTING 2
static int total, total_i, run;
static int current, current_i;
static int debug;
static int chunk = 1024*1024;
static int inflight = 14;
static int wait_finish = 0;
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

static void signal_all(enum libusb_transfer_status st) {
  int i;

  for(i = 0; i < current_i; i++) {
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
}

static void submit_cb(struct libusb_transfer *transfer) {
  long i = (long) transfer->user_data;

  if (debug) {
    fprintf(stderr, "cb %ld = %d, status %d\n", i, transfer->status, status);
  }
  if (status != SUBMITTING && transfer->status != LIBUSB_TRANSFER_CANCELLED) {
    fprintf(stderr, "Spurious cb\n");
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
	return;
      }
      run++;
    } else if (i == total_i - 1) {
      signal_all(LIBUSB_TRANSFER_COMPLETED);
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
    signal_all(transfer->status);
    status = IDLE;
  }
}

static int my_libusb_submit_transfer(struct libusb_transfer *transfer) {
  if (status == SUBMITTING) {
    fprintf(stderr, "New transfers while submitting.\n");
    cancel_trans();
    status = IDLE;
  }
  if (status == IDLE) {
    if (debug) {
      fprintf(stderr, "First submit %p: %p %d tout %d\n", transfer, transfer->buffer, transfer->length, transfer->timeout);
    }
    data = transfer->buffer;
    current = transfer->length;
    current_i = 1;
    saved_transfer[0] = transfer;
  }
  if (status == LOADING) {
    if (debug) {
      fprintf(stderr, "Submit %p: %p %d tout %d\n", transfer, transfer->buffer, transfer->length, transfer->timeout);
    }
    current += transfer->length;
    saved_transfer[current_i] = transfer;
    current_i++;
  }
  if (current > 0)
    status = LOADING;
  if (current >= total) {
    int n;

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
      if (wait_finish)
	tout = 1000;
      libusb_fill_bulk_transfer(my_transfer[total_i],
				saved_transfer[0]->dev_handle, 130,
				&data[n], len, submit_cb,
				(void *) (long) total_i, tout);
    }
    run = 0;
    while (run < inflight && run < total_i) {
      int r;

      if (debug) {
	fprintf(stderr, "submitting %d\n", run);
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
    if (wait_finish) {
      if (debug) {
	fprintf(stderr, "start wait\n");
      }
      while (status != IDLE)
	sched_yield();
      if (debug) {
	fprintf(stderr, "end wait\n");
      }
    }
  }
  return 0;
}

static int my_libusb_cancel_transfer(struct libusb_transfer *transfer) {
  if (debug) {
    fprintf(stderr, "got cancel %d %p\n", run, transfer);
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
  if (getenv("SB_CHUNK"))
    chunk = strtoul(getenv("SB_CHUNK"), NULL, 0);
  if (getenv("SB_INFLIGHT"))
    inflight = strtoul(getenv("SB_INFLIGHT"), NULL, 0);  
  if (getenv("SB_WAIT_FINISH"))
    wait_finish = strtoul(getenv("SB_WAIT_FINISH"), NULL, 0);
  total = bsize;
  for (i=0; i < SB; i++) {
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
