#ifndef BLE_COMMAND_H
#define BLE_COMMAND_H
#include <stdint.h>
#include <string.h>
static int ble_action(const unsigned char *data,unsigned n){
    if(n>1&&data[n-1]=='\n'){n--;if(n>1&&data[n-1]=='\r')n--;}
    return n==1&&data[0]>='a'&&data[0]<='j'?data[0]:-1;
}
/* No replay of arbitrary streams: one notification is one command. */
static int ble_name_match(const unsigned char *adv,unsigned size,const char *name){
    unsigned i=0,n,j;while(i<size){n=adv[i++];if(!n)break;if(n>size-i)return 0;
        if((adv[i]==8||adv[i]==9)&&n-1==strlen(name)){
            for(j=0;j<n-1;j++)if(adv[i+1+j]!=(unsigned char)name[j])break;
            if(j==n-1)return 1;
        }i+=n;
    }return 0;
}
#endif
