#ifndef BATTERY_LEVEL_H
#define BATTERY_LEVEL_H
/* Thresholds are deployment estimates, not a battery fuel gauge. */
static int battery_bars(int mv,const int thresholds[4]) {
    int bars=0,i;
    for(i=0;i<4;i++)if(mv>=thresholds[i])bars++;
    return bars;
}
static int battery_mv(int adc,int top,int bottom,int correction) {
    return (int)(((long long)adc*(top+bottom)*correction+bottom*500LL)/(bottom*1000LL));
}
#endif
