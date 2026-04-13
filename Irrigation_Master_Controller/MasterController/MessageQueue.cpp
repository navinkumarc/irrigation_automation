// MessageQueue.cpp
#include "MessageQueue.h"

MessageQueue::MessageQueue() : head(0), tail(0) {}

bool MessageQueue::enqueue(const String &msg) {
  int next = (tail + 1) % INCOMING_QUEUE_SIZE;
  if (next == head) {
    // Queue full — overwrite oldest message (advance head)
    head = (head + 1) % INCOMING_QUEUE_SIZE;
  }
  queue[tail] = msg;
  tail = next;
  return true;
}

bool MessageQueue::dequeue(String &msg) {
  if (head == tail) return false;
  msg = queue[head];
  head = (head + 1) % INCOMING_QUEUE_SIZE;
  return true;
}

int MessageQueue::size() {
  if (tail >= head) return tail - head;
  return INCOMING_QUEUE_SIZE - head + tail;
}

bool MessageQueue::isEmpty() {
  return head == tail;
}

bool MessageQueue::isFull() {
  return ((tail + 1) % INCOMING_QUEUE_SIZE) == head;
}

void MessageQueue::clear() {
  head = 0;
  tail = 0;
}
