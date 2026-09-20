#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <cplug.h> // cplug_atomic_*

// Single-producer / single-consumer ring of parameter events from the GUI
// thread to the audio thread. Lock-free; the audio thread never blocks.
typedef struct {
  uint32_t type;     // CPLUG_EVENT_PARAM_CHANGE_{BEGIN,UPDATE,END}
  uint32_t param_id;
  double value;      // plain value (UPDATE only)
  bool forward_to_host; // false when the host was already told on the UI thread
} ParamEvent;

#define PARAM_QUEUE_SIZE 256 // power of two
#define PARAM_QUEUE_MASK (PARAM_QUEUE_SIZE - 1)

typedef struct {
  ParamEvent items[PARAM_QUEUE_SIZE];
  cplug_atomic_i32 head; // next write slot, advanced by the producer
  cplug_atomic_i32 tail; // next read slot, advanced by the consumer
} ParamQueue;

static inline void ParamQueue_Init(ParamQueue *q) {
  cplug_atomic_exchange_i32(&q->head, 0);
  cplug_atomic_exchange_i32(&q->tail, 0);
}

// Producer side. Returns false (event dropped) when full.
static inline bool ParamQueue_Push(ParamQueue *q, const ParamEvent *ev) {
  int head = cplug_atomic_load_i32(&q->head);
  int tail = cplug_atomic_load_i32(&q->tail);
  if (((head + 1) & PARAM_QUEUE_MASK) == tail) return false;
  q->items[head] = *ev;
  cplug_atomic_exchange_i32(&q->head, (head + 1) & PARAM_QUEUE_MASK);
  return true;
}

// Consumer side. Returns false when empty.
static inline bool ParamQueue_Pop(ParamQueue *q, ParamEvent *ev) {
  int head = cplug_atomic_load_i32(&q->head);
  int tail = cplug_atomic_load_i32(&q->tail);
  if (tail == head) return false;
  *ev = q->items[tail];
  cplug_atomic_exchange_i32(&q->tail, (tail + 1) & PARAM_QUEUE_MASK);
  return true;
}
