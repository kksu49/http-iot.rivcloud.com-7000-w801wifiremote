#ifndef PLATFORM_HEALTH_H
#define PLATFORM_HEALTH_H
/* A real authorization response proves connectivity without valid credentials.
   A 200 login/captive HTML page does not prove platform access. */
static int platform_response_reachable(int status,int json_code){
    return (status==200&&json_code)||status==401||status==403||status==429||
           status==500||status==502||status==503||status==504;
}
static int platform_health_update(unsigned *failures,int reachable){
    if(reachable){*failures=0;return 0;}
    if(*failures<3)(*failures)++;
    return *failures>=3;
}
#endif
