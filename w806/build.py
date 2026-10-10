"""Build an independent W806 HAL application, sharing the tested wire parser."""
import json, subprocess
from pathlib import Path
root=Path(__file__).resolve().parent
workspace=root.parent
sdk=root/'vendor/WM_SDK_W806'
out=root/'build';out.mkdir(exist_ok=True)
cfg=json.loads((root/'config.json').read_text())
pins=cfg['outputs_pb'];levels=cfg['output_on_levels']
assert len(pins)==10 and len(set(pins))==10 and all(0<=p<=31 and p not in (19,20) for p in pins)
assert len(levels)==10 and all(v in (0,1) for v in levels)
assert 1<=cfg['pulse_ms']<=60000 and 9600<=cfg['uart_baud']<=115200
assert cfg['heartbeat_ms']==0 or 1000<=cfg['heartbeat_ms']<=3600000
identity=bytes.fromhex(cfg['board_id'].replace(':',''));assert len(identity)==6
(out/'board_config.h').write_text('\n'.join([
 '#define OUTPUT_PINS {'+','.join(map(str,pins))+'}',
 '#define OUTPUT_LEVELS {'+','.join(map(str,levels))+'}',
 '#define BOARD_ID {'+','.join(map(str,identity))+'}',
 '#define UART_BAUD '+str(cfg['uart_baud']), '#define PULSE_MS '+str(cfg['pulse_ms']),
 '#define HEARTBEAT_MS '+str(cfg['heartbeat_ms'])])+'\n')
gcc=next((workspace/'toolchains').rglob('csky-elfabiv2-gcc.exe'))
common=['-mcpu=ck804ef','-mhard-float','-O2','-ffunction-sections','-fdata-sections','-DTLS_CONFIG_CPU_XT804=1','-DGCC_COMPILE=1']
shared=workspace/'w801-sdk/wm_sdk_w80x_20211115/app'
includes=[sdk/'include',sdk/'include/arch/xt804',sdk/'include/arch/xt804/csi_core',sdk/'include/driver',out,shared]
common += ['-I'+str(p) for p in includes]
sources=[root/'main.c',shared/'receiver_core.c']
sources+=list((sdk/'platform/arch/xt804/bsp').glob('*.c'))+list((sdk/'platform/arch/xt804/bsp').glob('*.S'))
sources += [sdk/'platform/arch/xt804/libc/libc_port.c']
sources += [sdk/'platform/drivers'/f'wm_{name}.c' for name in ('gpio','uart','cpu','hal','internal_flash')]
objects=[]
def run(args):subprocess.run(list(map(str,args)),check=True)
for source in sources:
 obj=out/(source.stem+'.o');run([gcc,*common,'-c',source,'-o',obj]);objects.append(obj)
ld=(sdk/'ld/W806/gcc_csky.ld').read_text().replace('LENGTH = 0xEFC00','LENGTH = 0xEDC00')
(out/'w806.ld').write_text(ld)
elf=out/'w806.elf'
run([gcc,*common,'-nostartfiles','-Wl,--gc-sections','-Wl,-zmax-page-size=1024','-Wl,-Map='+str(out/'w806.map'),'-T',out/'w806.ld',*objects,'-o',elf])
run([gcc.with_name('csky-elfabiv2-objcopy.exe'),'-O','binary',elf,out/'w806.bin'])
tool=workspace/'w801-sdk/wm_sdk_w80x_20211115/tools/w800/wm_tool.exe'
run([tool,'-b',out/'w806.bin','-o',out/'w806','-it','1','-fc','0','-ra','8010400','-ih','8010000','-ua','8010000','-nh','0','-un','0'])
(out/'w806.fls').write_bytes((sdk/'tools/W806/W806_secboot.img').read_bytes()+(out/'w806.img').read_bytes())
print('W806 firmware:',out/'w806.fls',(out/'w806.fls').stat().st_size)
