"""Check actual battery conversion and every display threshold boundary."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).parent
header = (root/'w801-sdk/wm_sdk_w80x_20211115/app/battery_level.h').as_posix()
source = '#include <assert.h>\n#include "'+header+'"\n'+r'''
int main(void) {
    int t[]={3500,3700,3850,4100},i;
    for(i=0;i<4;i++) {
        assert(battery_bars(t[i]-1,t)==i);
        assert(battery_bars(t[i],t)==i+1);
    }
    assert(battery_bars(4200,t)==4);
    assert(battery_mv(2100,100000,100000,1000)==4200);
    assert(battery_mv(1750,100000,100000,1000)==3500);
    assert(battery_mv(2000,100000,100000,1050)==4200);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)
    (p/'test.c').write_text(source)
    subprocess.run(['C:/MinGW/bin/gcc.exe','-std=c99',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
    subprocess.run([str(p/'test.exe')],check=True)
print('Battery tests passed: divider, calibration, all threshold boundaries')
