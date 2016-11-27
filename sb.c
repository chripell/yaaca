#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#include "libusb.h"

extern int (*hack_libusb_submit_transfer)(struct libusb_transfer *transfer);

#define SB 100
#define SB_CHUNK (1024*1024)

static struct libusb_transfer *sb_transfer[SB];
static libusb_transfer_cb_fn sb_cbs[SB];
static int sb_expected;
static int sb_last_submitted = -1;
static int sb_last_transfered;
static int sb_debug;

void sb_cb(struct libusb_transfer *transfer) {
  int i;

  if (sb_last_submitted == -1) {
    fprintf(stderr, "spurious cb -1\n");
    return;
  }
  
  for(i = 0; i < sb_last_submitted && sb_transfer[i] != transfer; i++);
  if (i == sb_last_submitted) {
    fprintf(stderr, "spurious cb %p\n", transfer);
  } else {
    transfer->callback = sb_cbs[i];
    sb_cbs[i](transfer);
    if (sb_debug)
      fprintf(stderr, "done %d t %p\n", i, transfer);
  }
  if (sb_last_submitted < sb_expected) {
    int r;

    sb_transfer[sb_last_submitted]->endpoint = 130;
    r = libusb_submit_transfer(sb_transfer[sb_last_submitted]);
    if (sb_debug)
      fprintf(stderr, "retry %d t %p r %d\n", sb_last_submitted, sb_transfer[sb_last_submitted], r);
    if (r == 0)
      sb_last_submitted++;
  }
}

int my_libusb_submit_transfer(struct libusb_transfer *transfer) {
  sb_last_submitted = -1;
  if (sb_debug)
    fprintf(stderr, "queuing %d t %p\n", sb_last_transfered, transfer);
  transfer->endpoint = 130;
  sb_cbs[sb_last_transfered] = transfer->callback;
  transfer->callback= sb_cb;
  sb_transfer[sb_last_transfered] = transfer;
  sb_last_transfered++;
  if (sb_last_transfered < sb_expected)
    return 0;
  sb_last_transfered = 0;
  sb_last_submitted = 0;
  while (sb_last_submitted < sb_expected) {
    int r;

    r = libusb_submit_transfer(sb_transfer[sb_last_submitted]);
    if (sb_debug)
      fprintf(stderr, "submit %d t %p r %d\n", sb_last_submitted, sb_transfer[sb_last_submitted], r);
    if (r != 0) {
      break;
    }
    sb_last_submitted++;
  }
  return 0;
}

void sb_init(int bsize) {
  if (getenv("SB_DEBUG"))
    sb_debug = 1;
  hack_libusb_submit_transfer = my_libusb_submit_transfer;
  sb_expected = bsize / SB_CHUNK;
  if (bsize % SB_CHUNK)
    sb_expected++;
  if (sb_debug)
    fprintf(stderr, "sb_expected %d\n", sb_expected);
}
