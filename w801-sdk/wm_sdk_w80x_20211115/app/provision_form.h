#ifndef PROVISION_FORM_H
#define PROVISION_FORM_H
#include <string.h>
/* Captive-portal probes and browser paths should all open the setup form.
   Compare only the method prefix: sizeof("GET / ") includes a trailing NUL. */
static int provision_page_request(const char *request){return !strncmp(request,"GET ",4);}
static int form_hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int form_value(const char *body,const char *name,char *out,unsigned capacity){
 unsigned len=strlen(name),n=0;const char *p=body;
 while(*p){const char *end=strchr(p,'&');if(!end)end=p+strlen(p);
  if((unsigned)(end-p)>len&&!strncmp(p,name,len)&&p[len]=='='){
   p+=len+1;while(p<end){unsigned char c=*p++;
    if(c=='+')c=' ';else if(c=='%'){int a,b;if(end-p<2)return 0;a=form_hex(p[0]);b=form_hex(p[1]);if(a<0||b<0)return 0;c=(a<<4)|b;p+=2;}
    if(!c||c<32||n+1>=capacity)return 0;
    out[n++]=c;
   }out[n]=0;return 1;
  }p=*end?end+1:end;
 }return 0;
}
#endif
