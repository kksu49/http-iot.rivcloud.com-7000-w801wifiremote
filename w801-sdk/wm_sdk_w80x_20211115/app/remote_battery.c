#include "wm_include.h"
#include "wm_adc.h"
#include "wm_gpio_afsel.h"
#include "remote_board.h"
#include "remote_battery.h"
#include "battery_level.h"
#if REMOTE_BATTERY_ENABLED
static const enum tls_io_name leds[]=REMOTE_BATTERY_LEDS;
static const int thresholds[]=BATTERY_THRESHOLDS;
static OS_STK stack[512];
static tls_os_task_t task;
static volatile unsigned request;
static int enabled;
/* Unlike the legacy convenience API, require four completed conversions and
   a real tick timeout; never display stale ADC data as a valid measurement. */
static int conversion(int channel,int *value) {
    unsigned start,count=0;
    tls_adc_init(0,0);
    tls_adc_reference_sel(ADC_REFERENCE_INTERNAL);
    tls_adc_set_clk(0x28);
    /* Match SDK unity PGA setup without relying on its undeclared function. */
    tls_reg_write32(HR_SD_ADC_PGA_CTRL,GAIN_CTRL_PGA_VAL(1)|CLK_CHOP_SEL_PGA_VAL(1)|PGA_CHOP_ENP_VAL(1)|PGA_EN_VAL(1));
    tls_irq_disable(ADC_IRQn);
    tls_adc_clear_irq(ADC_INT_TYPE_ADC);
    tls_adc_start_with_cpu(channel);
    start=tls_os_get_time();
    while(count<4 && (unsigned)(tls_os_get_time()-start)<(HZ/10?HZ/10:1)) {
        if(tls_reg_read32(HR_SD_ADC_INT_STATUS)&ADC_INT_MASK){tls_adc_clear_irq(ADC_INT_TYPE_ADC);count++;}
        else tls_os_time_delay(1);
    }
    if(count==4){*value=(int)tls_read_adc_result();signedToUnsignedData(value);}
    tls_adc_stop(0);
    tls_irq_enable(ADC_IRQn);
    return count==4;
}
static int measure(void) {
    int offset,raw,i,sum=0,mv;
    if(!conversion(CONFIG_ADC_CHL_OFFSET,&offset))return -1;
    for(i=0;i<4;i++){
        if(!conversion(0,&raw))return -1;
        /* Same internal-reference conversion as the shipped SDK. */
        mv=1196+(int)((long long)(raw-offset)*126363/4000000LL);
        if(mv<10||mv>2200)return -1;
        sum+=mv;
    }
    mv=battery_mv((sum+2)/4,BATTERY_DIVIDER_TOP_OHMS,BATTERY_DIVIDER_BOTTOM_OHMS,BATTERY_CALIBRATION_PERMILLE);
    return mv>=2500&&mv<=4350?mv:-1;
}
static void show(int bars,int blink) {
    int i;
    for(i=0;i<4;i++)tls_gpio_write(leds[i],!((bars>i)||(i==0&&blink)));
}
static void entry(void *arg) {
    unsigned seen=0,stamp=0;int bars=0,visible=0,low=0,i;
    for(i=0;i<4;i++){tls_gpio_write(leds[i],1);tls_gpio_cfg(leds[i],WM_GPIO_DIR_OUTPUT,WM_GPIO_ATTR_FLOATING);}
    wm_adc_config(0);
    for(;;){
        unsigned now=tls_os_get_time();
        if(seen!=request){
            int mv;seen=request;mv=measure();stamp=tls_os_get_time();
            visible=mv>=0;bars=visible?battery_bars(mv,thresholds):0;low=visible&&bars==0;
            printf("BATTERY mv=%d estimate_percent=%d bars=%d low=%d valid=%d\n",mv,bars*25,bars,low,visible);
            now=stamp;
        }
        if(visible&&(unsigned)(now-stamp)>=(unsigned)((BATTERY_DISPLAY_MS*(unsigned long long)HZ+999)/1000))visible=0;
        show(visible?bars:0,visible&&low&&((unsigned)(now-stamp)/(HZ/4?HZ/4:1))%2==0);
        tls_os_time_delay(HZ/20?HZ/20:1);
    }
}
#endif
void remote_battery_start(int wifi_role) {
#if REMOTE_BATTERY_ENABLED
    if(!wifi_role)return;
    enabled=tls_os_task_create(&task,"battery",entry,0,(u8*)stack,sizeof(stack),35,0)==WM_SUCCESS;
    if(!enabled)printf("BATTERY_TASK_FAILED\n");
#endif
}
void remote_battery_request(void) {
#if REMOTE_BATTERY_ENABLED
    if(enabled)request++;
#endif
}
