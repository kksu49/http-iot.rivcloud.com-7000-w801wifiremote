# W801 Wi-Fi 遥控与 DTU 接收执行

当前分支：2026-10-10 双向扩展；正向稳定版本为 2026-10-09，支持自动认证续期、串口心跳和 1 秒 LED 输出。

## 双向控制扩展（2026-10-10）

正向稳定源码已保留在 GitHub 标签 `stable-forward-20261010`；本机 `backups/forward-20261010` 保存改动前固件、私有配置和源码 ZIP。以下扩展需现场双向验证，不能视为已经完成端到端验收。

两块板使用同一固件，通过 `board.transport_select_gpio`（默认 PA11）选择按钮发送方式，不再按 MAC 固定角色。上电时 PA11 连续 5 次采样为低电平（接 GND）才选择 DTU UART；悬空由内部上拉选择 WiFi HTTP，采样不一致时也选择 WiFi。选择后本次启动不再改变；换接线后需复位，避免旧队列跨发送方式执行。两种方式均保留串口控制解析与 LED 输出；WiFi 方式同时读取平台反向 REV 上行。更换板子无需修改 MAC 或编译两份固件。仅插拔 PA12/PA13 无法可靠判断通信角色，必须配套 PA11 选择线。

PA11 选择 DTU 后，PA0～PA9 按钮仅走 UART5，上传不等待 WiFi；WiFi 配网功能仍存在。接收纯串口指令不要求 WiFi。PA10 接地 5 秒进入热点配网，PA11 是发送方式选择，两者不可混用。此版本已编译、通过主机测试；硬件烧录及两种接线的角色回读需按现场接线完成。

反向路径：DTU 板按键 → PA12 UART5 输出 `REV,<唯一ID>,<a-j>\n` → DTU 将串口载荷上传平台 → WiFi 板每约 2 秒查询目标 DTU 的上行记录 → 转成内部控制帧交给原有接收核心 → 对应 LED 点亮 1 秒。GPIO 映射与下表一致；a～g 为板载 LED，h～j 为外接输出。串口上传不能等待 WiFi 或平台 Token，按键不直接点亮本机 LED。

首次成功查询和 WiFi 重连后的首次查询仅建立记录 ID 基线，不执行历史命令；仅消费新增 uplink 中的 REV 帧，忽略 ACK、HB 和 downlink。平台查询与原正向按键发送分属独立任务；执行队列由原串口接收任务独占，避免并发访问接收核心。队列满、排队超过 5 秒的命令丢弃并记录日志，不补发。当前每次读取最近 20 条上行，离线期间命令不补执行；HTTP 延迟、上行频繁或网络故障可能导致延迟/遗漏，不保证实时或百分之百送达。

日志：DTU 板 `BUTTON_TRANSPORT DTU_UART`、`KEY x SERIAL_QUEUED`、`REVERSE_UART_TX QUEUED`；WiFi 板 `BUTTON_TRANSPORT WIFI_HTTP`、`REVERSE_BASELINE_READY`、`REVERSE_COMMAND`、`REVERSE_PLATFORM_RX`、`RX_GPIO`。UART QUEUED 仅表示驱动接收了发送请求，必须看到平台新增 REV 上行才能证明 DTU 上传成功。现有 01/02 调试口是否支持此业务上传尚需实测；若不上行，应使用厂家支持的业务串口并添加正确电平转换，不能只靠修改 W801 代码解决。

测试：`python test_reverse_rows.py` 验证真实轮询函数的历史基线、重复过滤、方向过滤、心跳/回执忽略、重连行为；原有接收核心测试验证 1 秒复位及串口协议。`python monitor_bidirectional.py --ports COM4 COM5 --seconds 60` 只读取 USB 调试日志，不向板发送控制字节。需安装 pyserial。

本次实测：两块板烧录完成；COM5 多次 a～i 正向动作经过平台、DTU 到 COM4，板载输出按极性切换并约 1 秒复位。COM4 的 f/e 按键已生成唯一 REV 帧并提交 UART5，但随后查询最近 50 条 uplink 得到 REV_COUNT=0，因此反向完整链路未通过。手册 `DLA80-485DL` 明确 01/02 为 DEBUG-RX/TX，透传接口模式仅列 RS485/RS232；当前调试口连接未证明能够上传业务帧。下一步须验证业务串口接线、电平转换、两端波特率以及 REV 上行，保留稳定标签以便回退。

后续排查回读确认：`transUartMode=2`（RS232）、业务口 `baudrate=9600`，Modbus switch=0，TCP 服务正常；最近 100 条 uplink 仅有 DTU 自身心跳。W801 为 115200，与业务口不同。SDK 的 UART 写入函数会复制发送缓冲，因此当前局部帧缓冲生命周期不是已发现的故障。不能将看到调试口下行日志当成业务透传接口已接通。

业务接线修复：DTU 03 TXD → RS232/3.3V TTL 转换模块 → W801 PA13 RX；W801 PA12 TX → 转换模块 → DTU 04 RXD；共地。不能直接连接 RS232 与 GPIO。先执行 `python configure_dtu_business_uart.py` 仅读取；安装转换模块并完成业务接线后，再执行 `python configure_dtu_business_uart.py --apply`，保存原始参数并将 DTU 业务口波特率设成配置中 W801 的 115200，随后回读验证。脚本不更改透传模式，不恢复出厂，不重启 DTU。硬件未安装时尚未执行 apply，正向调试路径仍保持现状。

`python inspect_dtu_upload.py` 仅读取当前设备配置及最近上行；诊断 JSON 仅保留本机。以上平台脚本使用本机预置 Token；若该 Token 过期，需更新本机配置，不能据此判断 DTU 离线。验收必须包括：COM4 的 REV 实际出现在平台 uplink、COM5 的 REVERSE_PLATFORM_RX 与 GPIO 1 秒动作，以及 COM5→COM4 正向回归。

同一套固件包含发送与接收功能。发送端按钮接地后经 Wi-Fi 调用 IOT 平台下发接口；接收端经 DTU 串口接收完整控制帧，驱动对应 GPIO，定时结束后通过串口输出回执。按键本身不直接驱动本机执行输出；同一块板若同时连接 DTU，也可能收到自己经平台返回的指令。

## GPIO 接线

按钮上拉，接地触发；需先松开再按，含消抖。GPIO 名称指芯片丝印名称，不是排针序号。

| 按钮 | 命令 | 接收输出 | 动作 |
| --- | --- | --- | --- |
| PA0 | a | PB5 | 低电平点亮板载 LED |
| PA1 | b | PB25 | 低电平点亮板载 LED |
| PA2 | c | PB26 | 低电平点亮板载 LED |
| PA3 | d | PB18 | 低电平点亮板载 LED |
| PA4 | e | PB17 | 低电平点亮板载 LED |
| PA5 | f | PB16 | 低电平点亮板载 LED |
| PA6 | g | PB11 | 低电平点亮板载 LED |
| PA7 | h | PB0 | 外接输出，高电平有效 |
| PA8 | i | PB1 | 外接输出，高电平有效 |
| PA9 | j | PB2 | 外接输出，高电平有效 |

默认持续 1000ms，然后恢复无效电平。板载 LED 上电默认熄灭。当前版本为定时脉冲模式，早期提出的五路锁定开关模式尚未在当前接收核心实现。

- UART5：PA12 TX、PA13 RX，当前 115200、8N1；USB 调试日志与业务串口分开。
- DTU 01 RX ← PA12 TX，DTU 02 TX → PA13 RX，共地。这是调试口连接，仅用于现阶段测试；尚未验证长期无人值守稳定性。
- DTU 03/04 为 RS232 业务接口，必须经过 RS232↔3.3V TTL 转换模块，不能直连 GPIO。已读取业务口为 RS232/9600，改接前须统一波特率。
- PA10 接地 5 秒进入配网；USB 下载 UART 的 PB19/PB20 保留。
- PA11：发送方式选择。接 GND 为 DTU 串口，悬空为 WiFi，复位后生效；与 PA0～PA9 按钮和 PA10 配网不冲突。

## 报文与可靠性

控制帧：`CMD,<唯一ID>,<a-j>\n`。也兼容已实测的 DTU 调试日志前缀 `[RIV]# TCP Recv data : CMD,...`，逐段接收并按换行组帧；忽略非控制日志。

执行结束回执：`ACK,<唯一ID>,<命令>,TIMER,<通道>,DONE\n`。接收端保留近期 32 条记录用于去重；重复活动命令不再次执行，完成的重复命令可再次回执。不同 ID 的同通道新命令重新计时，旧命令回执 SUPERSEDED；去重记录不跨断电保留。

发送端队列最多 31 条，超过 5 秒的旧命令不发送，断网/认证失效清队列，不自动重发结果不明的动作。回执为可选的独立查询，不阻塞正常按键发送。DTU 调试口路径上尚未确认 ACK 上传到平台，DONE 日志不等于平台收到回执。

PA12 每 30 秒异步提交 `HB,W801\r\n`，USB 日志为 SERIAL_HEARTBEAT QUEUED/FAILED；QUEUED 表示已提交 UART 驱动，不能证明 DTU 已接收或被唤醒。DTU 本身的 30 秒业务心跳是另一项平台配置。

## 自动 Token 续期

有效 Token 直接复用，收到 HTTP 401 或 JSON code=401 后自动使用配置中账号和密码 MD5 重新登录。新 Token 在 RAM 中使用，重新验证设备后恢复发送。无需每次按键登录，也无需因正常认证过期重新烧录。

失败退避为 5、10、20、40、60 秒，之后最多每 60 秒重试；空闲时每分钟读设备信息检测认证状态。续期期间按键不会补发。账号/密码变更、平台不可达时仍可能失败，日志输出 AUTH_RENEW_FAILED 和 AUTH_RETRY_IN。配置内的 authentication 是模式记录，自动续期当前由源码固定启用。

已使用真正过期的 Token 在 COM5 实测 TOKEN_EXPIRED → AUTH_RENEW_START → AUTH_RENEW_OK → DEVICE_READY。COM4 的控制解析、1 秒输出复位及两块板 30 秒串口心跳也已验证。长时间运行、故障恢复及正式业务口闭环验收尚未完成。

## 配置、编译、烧录

仓库仅保存自研代码和脱敏模板；请从厂家准备 `wm_sdk_w80x_20211115` SDK，放到 `w801-sdk/wm_sdk_w80x_20211115`，保留本仓库 app 文件，并准备 C-SKY 工具链和脚本要求的 C:/MinGW 工具。厂商 SDK、交叉编译器与私有产品手册不随本仓库上传。

1. 将 `project_config.example.json` 复制为 `project_config.json`，填写真实 Wi-Fi、平台账号、密码 MD5、设备及预置 Token；配置文件只留本机。密码 MD5 仍是认证凭据，不是可公开信息。
2. PowerShell 执行 `./build-w801.ps1`，生成三个部署头文件并编译。
3. 生成固件为 `w801-sdk/wm_sdk_w80x_20211115/bin/w800/w800.fls`，不要将含凭据的固件提交到 GitHub。
4. 关闭对应端口的串口监视器，执行厂家 wm_tool（示例端口按当前实际设备修改）：

```powershell
./w801-sdk/wm_sdk_w80x_20211115/tools/w800/wm_tool.exe -c COM5 -rs rts -ds 921600 -dl ./w801-sdk/wm_sdk_w80x_20211115/bin/w800/w800.fls
```

USB COM 编号会变化。发送板与接收板按实际用途区分，不能依赖端口号永久固定。当前历史连接为 COM5 发送、COM4 接收。

## 手机配网

初次没有可用 Wi-Fi 时等待约 45 秒，或 PA10 接地 5 秒，进入热点配网。连接 W801-Setup-<MAC尾号>，网页地址为 `http://192.168.4.1:8080`。热点密码来自私有配置，手机提交 Wi-Fi 后保存并重启。热点配网完整现场流程尚未验证。硬件 RESET 长按无法由固件正常计时，本项目使用 PA10。

## GitHub 备份范围

上传自研源码、构建/配置生成脚本、脱敏配置模板和核心测试。实际 project_config.json、remote_config.h、remote_session.h、固件、日志、PDF、工具链和本地 V1/V2 ZIP 均不上传。V2 是历史本机备份，不包含自动续期。

恢复源码需要另行准备厂家 SDK 与工具链；恢复当前设备部署还需本机私有配置。GitHub 源码备份不等于芯片 Flash 全量备份。
