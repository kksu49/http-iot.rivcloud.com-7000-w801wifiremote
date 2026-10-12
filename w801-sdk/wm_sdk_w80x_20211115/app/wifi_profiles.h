#ifndef WIFI_PROFILES_H
#define WIFI_PROFILES_H
#include <stdint.h>
#include <string.h>
#define WIFI_PROFILE_MAX 5
typedef struct {char ssid[33],password[65];} wifi_profile;
typedef struct {uint32_t magic,version,generation,count;wifi_profile items[5];uint32_t crc;} wifi_record;
static uint32_t wifi_crc(const void *data,unsigned length){
    const unsigned char *p=data;uint32_t crc=~0u;unsigned i;
    while(length--){crc^=*p++;for(i=0;i<8;i++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}return ~crc;
}
static int wifi_record_valid(const wifi_record *r){
    unsigned i,j;
    if(r->magic!=0x57494635u||r->version!=1||r->count>5||r->crc!=wifi_crc(r,sizeof(*r)-sizeof(r->crc)))return 0;
    for(i=0;i<r->count;i++){
        unsigned n;
        if(!r->items[i].ssid[0]||!memchr(r->items[i].ssid,0,33)||!memchr(r->items[i].password,0,65))return 0;
        n=strlen(r->items[i].password);if(n&&n<8)return 0;
        for(j=0;j<i;j++)if(!strcmp(r->items[i].ssid,r->items[j].ssid))return 0;
    }return 1;
}
static int wifi_newer(uint32_t a,uint32_t b){return (int32_t)(a-b)>0;}
static int wifi_next(unsigned count,unsigned start,unsigned attempt){return count?(start+attempt)%count:-1;}
#endif
