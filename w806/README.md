# W806 独立 DTU 收发端 V1

本目录使用海凌科官方 `WM_SDK_W806`，复用 W801 V3 的 `receiver_core.c/h` 和 `short_frame.h`。W801 的源代码、配置、固件均不修改。W806 固定使用 DTU 串口，无 PA11 角色选择、WiFi、热点配网或平台登录；网络上传由 DTU 完成。

## 接线

- UART5：DTU TX → **PA13 RX**；DTU RX ← **PA12 TX**；两者 GND 共地。
- 串口：**115200、8N1、无流控**，DTU 业务串口也必须一致。
- PA0～PA9：各自通过按键接 GND，依次反向发送 a～j。内部上拉，30 ms 消抖，长按只发送一次，松开后可再次触发。
- PA11、PA10 不使用；PB19/PB20 保留 USB 下载与日志串口，COM10 日志波特率 115200。
- DTU 的 RS232 03/04 口必须经过 RS232↔3.3V TTL 转换；不得直接接 MCU。01/02 能否承载双向业务取决于 DTU 固件配置，W806 无法替代该配置。

## 接收输出

| 控制字符 | 输出 GPIO | 用途 | 执行电平 | 空闲电平 |
|---|---|---|---|---|
| a | PB0 | 板载 LED | 低 | 高 |
| b | PB1 | 板载 LED | 低 | 高 |
| c | PB2 | 板载 LED | 低 | 高 |
| d | PB18 | 外接输出 | 高 | 低 |
| e | PB17 | 外接输出 | 高 | 低 |
| f | PB16 | 外接输出 | 高 | 低 |
| g | PB11 | 外接输出 | 高 | 低 |
| h | PB5 | 外接输出 | 高 | 低 |
| i | PB25 | 外接输出 | 高 | 低 |
| j | PB26 | 外接输出 | 高 | 低 |

默认执行 1000 ms 后恢复空闲。三路板载 LED 按低电平点亮配置；若具体板型相反，在 `config.json` 修改相应 `output_on_levels`，重新编译。外接输出仍为高电平脉冲。

## 协议与反向控制

与当前 W801 V3 一致：`C<字符><19字符ID>\n` 接收控制，`R<字符><19字符ID>\n` 反向上传，`K<字符><ID>\n` 完成回执，`S<字符><ID>\n` 被新命令替代。每帧 22 字节。兼容 `CMD,<ID>,<a-j>\n`、CRLF、末尾空白、DTU `[RIV]# TCP Recv data : ` 前缀。

保持分片解析、32 条去重、同通道新命令重新计时，回执无需等待，不阻塞下一次按键。反向 R 帧必须先通过 DTU 真正上传到平台，再由已有 WiFi 控制端轮询并执行。串口写成功仅代表 MCU 已写入 UART，不能代替平台上传确认。

每 10 分钟发送 `HB,W806\r\n`，第一次在运行 10 分钟后；这不改变 DTU 自身业务心跳配置。COM10 输出 `RX_COMMAND`、`RX_GPIO`、`RX_ACK`、`KEY ... REVERSE_UART_TX` 和每 10 秒 `RX_ALIVE`。

`config.json` 不保存 WiFi 或平台密码。`board_id` 是协议身份而非真实 MAC，每块 W806 应设置不同的 6 字节值。使用 Flash 末尾两个保留扇区 `0xFE000/0xFF000` 交替保存启动序号，防止重启后反向消息 ID 重复。仅启动时写一次；读写校验失败时关闭反向按键发送，接收功能继续可用。编译链接区预留上述 8 KB，禁止其他代码或全片擦除工具复用此区域。

## 编译与烧录

官方 SDK 来源：https://www.hlktech.com/Goods-189.html （WM_SDK_W806）。实际下载镜像：https://h.hlktech.com/download/HLK-W806/3/WM_SDK_W806.rar。

将 SDK 解压至 `w806/vendor/WM_SDK_W806`。使用工作区现有 C-SKY 工具链和 W800 通用下载工具：

```powershell
python w806/build.py
& ./w801-sdk/wm_sdk_w80x_20211115/tools/w800/wm_tool.exe -c COM10 -rs rts -ds 921600 -dl ./w806/build/w806.fls
```

输出固件 `w806/build/w806.fls`。需要手动同步时按住 BOOT，短按 RESET，松开 BOOT；下载完成后正常复位运行。勿将 W801 的 `w800.fls` 烧录到 W806。
