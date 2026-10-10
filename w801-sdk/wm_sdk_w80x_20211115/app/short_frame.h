#ifndef SHORT_FRAME_H
#define SHORT_FRAME_H
#include <stdint.h>
#include <string.h>
/* Full MAC + random boot session + sequence: 14 bytes -> 19 URL-safe chars. */
static inline void short_id(char out[20],const unsigned char mac[6],uint32_t session,uint32_t seq) {
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
 unsigned char bytes[14];unsigned i,j=0,bits=0;uint32_t value=0;
 memcpy(bytes,mac,6);
 for(i=0;i<4;i++){bytes[6+i]=(unsigned char)(session>>(24-8*i));bytes[10+i]=(unsigned char)(seq>>(24-8*i));}
 for(i=0;i<14;i++){value=(value<<8)|bytes[i];bits+=8;while(bits>=6){bits-=6;out[j++]=alphabet[(value>>bits)&63];}}
 if(bits)out[j++]=alphabet[(value<<(6-bits))&63];
 out[j]=0;
}
static inline int short_valid_id(const char *id) {
 unsigned i;if(strlen(id)!=19)return 0;
 for(i=0;i<19;i++)if(!((id[i]>='a'&&id[i]<='z')||(id[i]>='A'&&id[i]<='Z')||(id[i]>='0'&&id[i]<='9')||id[i]=='-'||id[i]=='_'))return 0;
 return 1;
}
static inline int short_control(const char *frame,char kind) {
 return frame[0]==kind&&frame[1]>='a'&&frame[1]<='j'&&short_valid_id(frame+2);
}
#endif
