// MessageQueue.h - Incoming message queue (ring buffer)
#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <Arduino.h>
#include "Config.h"

class MessageQueue {
private:
  String queue[INCOMING_QUEUE_SIZE];
  int head;
  int tail;

public:
  MessageQueue();
  bool enqueue(const String &msg);
  bool dequeue(String &msg);
  int size();
  bool isEmpty();
  bool isFull();
  void clear();
};

// Global instance
extern MessageQueue incomingQueue;

#endif // MESSAGE_QUEUE_H