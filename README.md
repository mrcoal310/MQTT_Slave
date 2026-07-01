# 基于 STM32F407 的物联网 MQTT 温湿度监测系统下位机

本项目为一个基于 STM32F407ZG 的物联网下位机程序，使用 ThreadX 进行多线程调度，通过 DHT11 采集温湿度，OLED 显示本地状态，并通过 ESP8266/ESP-AT 接入 Wi-Fi 与阿里云 MQTT。设备支持网页配网、Flash 保存 Wi-Fi 参数、周期上报、云端命令控制、定时开关、阈值自动控制和心跳/事件/回复消息

## 主要功能

- 温湿度采集：DHT11 接在 `PA7`，默认每 2 秒读取一次
- 本地显示：OLED 使用软件 I2C，显示温度、湿度、Wi-Fi/MQTT 状态和配网 AP 名称
- Wi-Fi 配网：无已保存 Wi-Fi 时自动进入 AP 配网模式，提供网页表单保存路由器 SSID/密码
- MQTT 通信：连接阿里云 IoT MQTT Broker，订阅命令主题并发布遥测、状态、事件、心跳和命令回复
- 本地按键：按键可触发 Wi-Fi 重连、MQTT 重连、清除配网和开始数据上报
- 运行控制：支持开关控制、延时定时、上报周期配置、温湿度阈值配置和软件复位命令
- 断线重试：Wi-Fi 连接失败后会先快速重试，再切换为慢速重试

## 工程环境

- MCU：`STM32F407ZG`
- IDE：Keil uVision / MDK-ARM
- 工程文件：`project.uvprojx`
- 输出目标：`project`
- 外设库：STM32F4 Standard Peripheral Library
- RTOS：ThreadX，源码位于 `THREADX/`
- 主要宏定义：`USE_STDPERIPH_DRIVER`、`STM32F40_41xxx`

## 目录结构

```text
CORE/       Cortex-M4 内核与启动文件
FWLIB/      STM32F4 标准外设库
HARDWARE/   OLED、DHT11、Wi-Fi/MQTT、cJSON、GPIO 封装
SYSTEM/     延时等系统辅助代码
THREADX/    ThreadX 内核源码与 Cortex-M4 AC5 移植
USER/       main、系统初始化、中断处理和 STM32 配置
Objects/    Keil 编译输出目录
Listings/   Keil 列表/map 输出目录
```

## 硬件连接

| 模块 | 引脚 | 说明 |
| --- | --- | --- |
| DHT11 | `PA7` | 单总线温湿度采集 |
| OLED SCL | `PA5` | 软件 I2C 时钟 |
| OLED SDA | `PA6` | 软件 I2C 数据 |
| ESP8266 RX | `PB10 / USART3_TX` | MCU 发送 AT 指令 |
| ESP8266 TX | `PB11 / USART3_RX` | MCU 接收 AT 返回 |
| LED0 | `PF9` | LED 线程周期翻转 |
| LED1 | `PF10` | LED 线程周期翻转 |
| KEY0 | `PE4`，上拉 | 触发 Wi-Fi 连接/重连 |
| KEY1 | `PE3`，上拉 | 触发 MQTT 连接/重连 |
| KEY2 | `PE2`，上拉 | 长按 3 秒清除 Wi-Fi 配置并重启 |
| WKUP | `PA0`，下拉 | 开始周期上报 |

ESP8266 串口参数为 `115200 8N1`，中断入口在 `USER/stm32f4xx_it.c` 的 `USART3_IRQHandler()`，实际接收处理在 `HARDWARE/wifi.c`。

> 注意：当前云端 `switch` 控制只维护软件状态，`wifi_load_set()` 没有驱动具体继电器或负载 GPIO。如需控制风扇、继电器等外设，应在 `HARDWARE/wifi.c` 的 `wifi_load_set()` 中补充实际 GPIO 输出逻辑。

## 线程设计

`USER/main.c` 中创建 4 个 ThreadX 线程：

| 线程 | 优先级 | 栈大小 | 职责 |
| --- | --- | --- | --- |
| `led_thread` | 1 | 1024 B | `PF9/PF10` LED 交替闪烁，表示系统运行 |
| `oled_thread` | 2 | 1024 B | 初始化 OLED/DHT11，读取温湿度并刷新显示 |
| `mqtt_thread` | 3 | 8192 B | Wi-Fi 配网、MQTT 连接、收发 JSON、重试和定时逻辑 |
| `key_thread` | 4 | 1024 B | 扫描 KEY0/KEY1/KEY2/WKUP 并产生请求 |

SysTick 由 ThreadX 接管，`SysTick_Handler()` 调用 `_tx_timer_interrupt()`。

## 配网流程

1. 上电后 `Wifi_Init()` 会从 Flash `0x080E0000` 读取已保存的 Wi-Fi 配置。
2. 如果没有有效配置，设备进入 AP 配网模式：
   - AP 名称：`Sen_xxxx`，无法读取 MAC 后缀时为 `Sen_0000`
   - AP 密码：`12345678`
   - 端口：HTTP `80`
3. 手机或电脑连接该 AP，通常访问 `http://192.168.4.1`。
4. 在网页中填写路由器 Wi-Fi 名称和密码，提交后写入 Flash。
5. 保存成功后设备触发软件重启，之后以 STA 模式连接路由器并连接 MQTT。
6. 如需重新配网，长按 `KEY2` 约 3 秒清除 Flash 中的 Wi-Fi 配置。

## MQTT 配置

MQTT 连接参数在 `HARDWARE/wifi.c` 中定义：

| 参数 | 当前值 |
| --- | --- |
| ProductKey | `k0xzrztwuSU` |
| DeviceName | `zheng` |
| Host | `iot-060ab9rr.mqtt.iothub.aliyuncs.com` |
| Port | `1883` |
| ClientId / Username / Password | 固化在 `wifi.c` 中 |

如需更换设备或产品，应同步修改 `WIFI_PRODUCT_KEY`、`WIFI_DEVICE_NAME`、`clientId`、`username`、`passwd` 和 MQTT Host。

### 主题

| 方向 | Topic | QoS | 说明 |
| --- | --- | --- | --- |
| 订阅 | `/k0xzrztwuSU/zheng/user/cmd` | 1 | 云端下发控制命令 |
| 发布 | `/k0xzrztwuSU/zheng/user/DATA` | 0 | 温湿度和运行遥测 |
| 发布 | `/k0xzrztwuSU/zheng/user/status` | 1 | 在线状态与运行状态 |
| 发布 | `/k0xzrztwuSU/zheng/user/reply` | 1 | 命令执行回复 |
| 发布 | `/k0xzrztwuSU/zheng/user/event` | 1 | 事件消息 |
| 发布 | `/k0xzrztwuSU/zheng/user/heartbeat` | 0 | 心跳消息，默认 30 秒一次 |

## JSON 协议

设备发布和接收的消息使用统一外层结构：

```json
{
  "proto_ver": "1.0",
  "product_id": "envctrl_v1",
  "device_id": "iot_node_001",
  "msg_type": "telemetry",
  "req_id": "optional-request-id",
  "seq": 1,
  "timestamp": 12,
  "error": null,
  "data": {},
  "ext": {}
}
```

### 遥测数据示例

```json
{
  "msg_type": "telemetry",
  "data": {
    "report_period_s": 5,
    "sensor_ok": true,
    "timer_enable": false,
    "timer_remain_s": 0,
    "rssi": -60,
    "timer_action": "off",
    "temperature": 26,
    "humidity": 60,
    "switch": 1,
    "temp_high_limit": 30,
    "humidity_high_limit": 80,
    "auto_rule_enable": false,
    "auto_rule_active": false
  }
}
```

### 常用下行命令

打开或关闭开关：

```json
{
  "msg_type": "cmd",
  "req_id": "sw-001",
  "data": {
    "cmd": "switch_set",
    "switch": 1
  }
}
```

也可直接使用别名：

```json
{"msg_type":"cmd","req_id":"sw-002","data":{"cmd":"open_device"}}
{"msg_type":"cmd","req_id":"sw-003","data":{"cmd":"close_device"}}
```

设置延时动作，`delay_s` 范围为 `1` 到 `86400` 秒：

```json
{
  "msg_type": "cmd",
  "req_id": "timer-001",
  "data": {
    "cmd": "timer_set",
    "action": "off",
    "delay_s": 60
  }
}
```

取消定时：

```json
{"msg_type":"cmd","req_id":"timer-002","data":{"cmd":"timer_cancel"}}
```

修改运行配置：

```json
{
  "msg_type": "cmd",
  "req_id": "cfg-001",
  "data": {
    "cmd": "config_set",
    "report_period_s": 10,
    "temp_high_limit": 32,
    "humidity_high_limit": 85,
    "auto_rule_enable": true
  }
}
```

查询状态或重启：

```json
{"msg_type":"cmd","req_id":"query-001","data":{"cmd":"status_query"}}
{"msg_type":"cmd","req_id":"reboot-001","data":{"cmd":"restart"}}
```

支持的命令别名包括：

- 开关设置：`switch_set`、`switch`、`set_switch`
- 打开设备：`open_device`、`device_on`、`turn_on`、`start_device`、`switch_on`、`on`
- 关闭设备：`close_device`、`device_off`、`turn_off`、`stop_device`、`switch_off`、`off`
- 定时设置：`timer_set`、`delay_set`、`set_timer`、`set_delay`、`delay_on`、`delay_off`
- 定时取消：`timer_cancel`、`cancel_timer`、`cancel_delay`、`timer_clear`
- 配置修改：`config_set`、`set_config`、`config`、`update_config`
- 状态查询：`status_query`、`query_status`、`get_status`、`query_remain`、`remain_query`、`timer_query`、`query_timer`、`query`
- 软件重启：`restart`、`restart_device`、`reboot`、`device_restart`

## OLED 显示

OLED 共 4 行主要信息：

```text
Temperature:xx
Humidity:xx
WiFi:OK/ING/FAIL/AP
MQTT:OK/ING/FAIL 或 SSID:Sen_xxxx 或 DATA:ON
```

当设备处于配网模式时，第 4 行显示 AP 名称；开始周期上报后，第 4 行显示 `DATA:ON`。

## 编译与烧录

1. 使用 Keil uVision 打开 `project.uvprojx`。
2. 确认目标芯片为 `STM32F407ZG`。
3. 确认 Include Path 包含：
   - `.\CORE`
   - `.\FWLIB\inc`
   - `.\USER`
   - `.\HARDWARE`
   - `.\SYSTEM`
   - `.\THREADX\common\inc`
   - `.\THREADX\ports\cortex_m4\ac5\inc`
4. 编译工程，输出文件位于 `Objects/`，HEX 文件为 `Objects/project.hex`。
5. 使用 ST-LINK/J-LINK 下载到开发板。

## 调试建议

- ESP8266 需使用 MQTT 固件
- OLED 显示 `WiFi:AP` 时，说明设备正在等待网页配网
- OLED 显示 `WiFi:JOIN FAIL` 时，优先检查路由器 SSID/密码、2.4 GHz 网络和 ESP8266 信号
- OLED 显示 `MQTT:CONN FAIL` 时，检查阿里云设备三元组、密码签名、Broker 地址和网络是否可达
- 温湿度一直为 `--` 时，检查 DHT11 的 `PA7` 接线、上拉和供电
- 如设备循环进入旧网络，可长按 `KEY2` 清除保存的 Wi-Fi 配置后重新配网。

## 后续扩展

- 在 `wifi_load_set()` 中加入继电器、风扇或执行器 GPIO 控制。
- 将 MQTT 三元组移出源码，改为通过配网页面或 Flash 参数区配置。
- 为配网页面增加设备信息、信号强度和保存结果倒计时。
- 增加传感器异常事件上报和本地蜂鸣器/LED 告警。
