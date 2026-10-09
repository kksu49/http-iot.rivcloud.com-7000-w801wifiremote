#ifndef RECEIVER_CORE_H
#define RECEIVER_CORE_H
#include <stdint.h>
#define RX_ID_SIZE 64
typedef struct {char id[RX_ID_SIZE];char action;unsigned char state;} rx_record;
typedef struct {
 char line[96];unsigned length;int discard;
 rx_record records[32];unsigned next;
 int active[10];uint32_t due[10];
 uint32_t pulse_ms;
 void (*output)(int,int);void (*ack)(const char*,char,const char*);
 void (*command)(const char*,char);
} receiver_core;
void receiver_init(receiver_core*,void(*)(int,int),void(*)(const char*,char,const char*));
void receiver_feed(receiver_core*,const unsigned char*,unsigned,uint32_t);
void receiver_tick(receiver_core*,uint32_t);
#endif
