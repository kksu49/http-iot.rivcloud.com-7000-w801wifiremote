#include "provision_form.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 char out[33],password[65];
 assert(form_value("ssid=TP-LINK_Huang&password=abc12345","ssid",out,sizeof(out))&&!strcmp(out,"TP-LINK_Huang"));
 assert(form_value("ssid=hello+world&password=a%26b%2Bc%3Dd","password",password,sizeof(password))&&!strcmp(password,"a&b+c=d"));
 assert(form_value("ssid=%E6%B5%8B%E8%AF%95&password=","ssid",out,sizeof(out))&&!strcmp(out,"测试"));
 assert(form_value("ssid=a&password=","password",password,sizeof(password))&&!password[0]);
 assert(!form_value("ssid=%00","ssid",out,sizeof(out)));
 assert(!form_value("ssid=%GG","ssid",out,sizeof(out)));
 assert(!form_value("ssid=%2","ssid",out,sizeof(out)));
 assert(!form_value("ssid=%0A","ssid",out,sizeof(out)));
 assert(!form_value("other=a","ssid",out,sizeof(out)));
 assert(!form_value("ssid=123456","ssid",out,5));
 puts("10 provisioning form tests passed");return 0;
}
