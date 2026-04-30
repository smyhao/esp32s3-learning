# ESP32 LED Locator — HTTP API 手册

## 概述

ESP32 LED Locator 固件在 WiFi 连接后启动 HTTP 服务器，提供以下 REST API 供上层服务（Flask）调用。

### 基础约定

| 项目 | 说明 |
|------|------|
| 协议 | HTTP over TCP/IP（局域网） |
| 数据格式 | JSON（UTF-8） |
| 请求体上限 | 4096 字节 |
| 默认端口 | 80（可通过 `POST /api/config` 修改） |
| 成功响应 | `{"status": "ok", ...}` |
| 错误响应 | `{"status": "error", "message": "..."}` |

### 端点总览

| 方法 | 路径 | 用途 |
|------|------|------|
| GET | `/api/health` | 健康检查 / 设备状态 |
| POST | `/api/led/set` | 设置 LED 颜色和模式 |
| POST | `/api/led/clear` | 关闭所有 LED |
| GET | `/api/config` | 查询灯带配置 |
| POST | `/api/config` | 修改灯带配置（热重载） |
| POST | `/api/config/strip` | 添加/更新单条灯带 |
| POST | `/api/config/strip/remove` | 删除单条灯带 |

---

## 1. GET /api/health

健康检查。用于测试设备连通性、获取当前灯带列表和运行时间。

### 请求

```
GET /api/health HTTP/1.1
```

无请求体。

### 成功响应 (200)

```json
{
  "status": "ok",
  "strips": [
    {"gpio": 8, "led_count": 30},
    {"gpio": 6, "led_count": 20}
  ],
  "uptime_s": 12345
}
```

### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | 固定 `"ok"` |
| `strips` | array | 当前所有灯带信息 |
| `strips[].gpio` | int | 灯带对应的 GPIO 编号 |
| `strips[].led_count` | int | 该灯带的 LED 数量 |
| `uptime_s` | int | 设备启动后的秒数 |

### 示例

```bash
curl http://192.168.1.50/api/health
```

---

## 2. POST /api/led/set

设置一个或多个 LED 的颜色和工作模式。支持批量操作。

### 请求

```
POST /api/led/set HTTP/1.1
Content-Type: application/json
```

```json
{
  "leds": [
    {"gpio": 8, "index": 0, "mode": "blink", "color": "#00ff00", "duration_ms": 10000},
    {"gpio": 8, "index": 3, "mode": "static", "color": "#ff0000", "duration_ms": 0},
    {"gpio": 6, "index": 1, "mode": "blink", "color": "#0000ff", "duration_ms": 5000}
  ]
}
```

### 字段说明

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `leds` | array | 是 | — | LED 指令数组 |
| `leds[].gpio` | int | 是 | — | 灯带 GPIO 编号 |
| `leds[].index` | int | 是 | — | LED 位置（0-based） |
| `leds[].mode` | string | 是 | — | `"static"` 常亮 或 `"blink"` 闪烁 |
| `leds[].color` | string | 是 | — | `"#RRGGBB"` 格式颜色 |
| `leds[].duration_ms` | int | 否 | 0 | 自动关闭时间（毫秒），0 = 永久 |

### 模式说明

| 模式 | 行为 |
|------|------|
| `static` | 立即常亮指定颜色。`duration_ms > 0` 时到时自动熄灭 |
| `blink` | 以 500ms 间隔闪烁。`duration_ms > 0` 时到时自动停止 |

### 特殊规则

- `leds` 为空数组 `[]` 时，等同于 clear —— 关闭所有灯带所有 LED
- `gpio` 对应的灯带不存在时，**静默忽略**该条目（不报错）
- `index` 超出范围 `[0, led_count)` 时，**静默忽略**该条目（不报错）
- 同一 `(gpio, index)` 多次出现时，使用**最后一个**
- `mode` 只接受 `"static"` 和 `"blink"`，其他值返回 400
- `color` 必须是 7 字符的 `"#RRGGBB"` 格式

### 成功响应 (200)

```json
{
  "status": "ok",
  "applied": 3
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | 固定 `"ok"` |
| `applied` | int | 成功应用的 LED 数量（不含静默忽略的） |

### 错误响应示例

| 场景 | HTTP 状态 | message |
|------|-----------|---------|
| body 不是合法 JSON | 400 | `"invalid JSON body"` |
| 缺少 `leds` 字段 | 400 | `"missing required field: leds"` |
| 某条目缺少 `gpio` | 400 | `"missing required field: gpio"` |
| 某条目缺少 `index` | 400 | `"missing required field: index"` |
| mode 值非法 | 400 | `"invalid mode: xxx, expected static or blink"` |
| color 格式非法 | 400 | `"invalid color format: xxx"` |
| body 超过 4096 字节 | 400 | `"request body too large"` |

### 示例

```bash
# 单颗 LED 闪烁（绿色，10秒后自动关闭）
curl -X POST http://192.168.1.50/api/led/set \
  -H "Content-Type: application/json" \
  -d '{"leds":[{"gpio":8,"index":0,"mode":"blink","color":"#00ff00","duration_ms":10000}]}'

# 同一灯带多颗 LED
curl -X POST http://192.168.1.50/api/led/set \
  -H "Content-Type: application/json" \
  -d '{"leds":[
    {"gpio":8,"index":0,"mode":"blink","color":"#00ff00","duration_ms":5000},
    {"gpio":8,"index":5,"mode":"static","color":"#ff0000","duration_ms":0}
  ]}'

# 跨灯带操作
curl -X POST http://192.168.1.50/api/led/set \
  -H "Content-Type: application/json" \
  -d '{"leds":[
    {"gpio":8,"index":0,"mode":"blink","color":"#00ff00","duration_ms":5000},
    {"gpio":6,"index":3,"mode":"static","color":"#ff0000","duration_ms":0}
  ]}'

# 空数组 = 清除所有（等同于 /api/led/clear）
curl -X POST http://192.168.1.50/api/led/set \
  -H "Content-Type: application/json" \
  -d '{"leds":[]}'
```

---

## 3. POST /api/led/clear

关闭所有灯带的所有 LED，取消所有闪烁定时器，重置所有 LED 状态为 OFF。

### 请求

```
POST /api/led/clear HTTP/1.1
```

请求体可为空或 `{}`。

### 成功响应 (200)

```json
{
  "status": "ok"
}
```

### 示例

```bash
curl -X POST http://192.168.1.50/api/led/clear -d '{}'
```

---

## 4. GET /api/config

查询当前灯带配置。返回设备上所有灯带的 GPIO、灯珠数以及 HTTP 端口。

### 请求

```
GET /api/config HTTP/1.1
```

无请求体。

### 成功响应 (200)

```json
{
  "status": "ok",
  "strips": [
    {"gpio": 8, "led_count": 30},
    {"gpio": 6, "led_count": 20}
  ],
  "http_port": 80
}
```

### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | 固定 `"ok"` |
| `strips` | array | 所有灯带信息 |
| `strips[].gpio` | int | GPIO 编号 |
| `strips[].led_count` | int | LED 数量 |
| `http_port` | int | HTTP 服务端口 |

### 示例

```bash
curl http://192.168.1.50/api/config
```

---

## 5. POST /api/config

修改灯带配置。新配置保存到 NVS 并**立即热重载**，无需重启设备。

### 请求

```
POST /api/config HTTP/1.1
Content-Type: application/json
```

```json
{
  "strips": [
    {"gpio": 8, "led_count": 30},
    {"gpio": 6, "led_count": 20},
    {"gpio": 4, "led_count": 15}
  ],
  "http_port": 80
}
```

### 字段说明

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `strips` | array | 是 | — | 灯带配置数组（1-8 条） |
| `strips[].gpio` | int | 是 | — | GPIO 编号（0-48） |
| `strips[].led_count` | int | 是 | — | LED 数量（1-256） |
| `http_port` | int | 否 | 80 | HTTP 服务端口 |

### 注意事项

- 调用后会**立即关闭所有 LED** 并用新配置重建驱动
- 配置持久化到 NVS，设备重启后自动加载新配置
- 该操作会短暂中断 LED 控制（通常 < 100ms）
- 最多支持 8 条灯带

### 成功响应 (200)

```json
{
  "status": "ok",
  "strips": [
    {"gpio": 8, "led_count": 30},
    {"gpio": 6, "led_count": 20},
    {"gpio": 4, "led_count": 15}
  ]
}
```

### 错误响应示例

| 场景 | HTTP 状态 | message |
|------|-----------|---------|
| 缺少 `strips` 字段 | 400 | `"missing required field: strips"` |
| strips 为空或超过 8 条 | 400 | `"strips count must be 1-8"` |
| gpio 超出 0-48 | 400 | `"gpio must be 0-48"` |
| led_count 超出 1-256 | 400 | `"led_count must be 1-256"` |
| 热重载失败（如 GPIO 占用） | 500 | `"failed to apply config: ..."` |

### 示例

```bash
# 查看当前配置
curl http://192.168.1.50/api/config

# 添加 GPIO 6 灯带（保留原有 GPIO 8）
curl -X POST http://192.168.1.50/api/config \
  -H "Content-Type: application/json" \
  -d '{"strips":[{"gpio":8,"led_count":30},{"gpio":6,"led_count":20}]}'

# 只保留一条灯带
curl -X POST http://192.168.1.50/api/config \
  -H "Content-Type: application/json" \
  -d '{"strips":[{"gpio":8,"led_count":30}]}'

# 修改后验证
curl http://192.168.1.50/api/health
```

---

## 6. POST /api/config/strip

添加一条新灯带，或更新已有 GPIO 的灯珠数量。只需指定一条灯带，不影响其他灯带。

### 请求

```
POST /api/config/strip HTTP/1.1
Content-Type: application/json
```

```json
{
  "gpio": 6,
  "led_count": 20
}
```

### 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `gpio` | int | 是 | GPIO 编号（0-48） |
| `led_count` | int | 是 | LED 数量（1-256） |

### 行为

- GPIO 已存在：更新该灯带的 `led_count`，立即热重载
- GPIO 不存在：添加新灯带，立即热重载
- 最多 8 条灯带，超出返回错误

### 成功响应 (200)

```json
{
  "status": "ok",
  "gpio": 6,
  "led_count": 20,
  "total_strips": 2
}
```

### 错误响应示例

| 场景 | HTTP 状态 | message |
|------|-----------|---------|
| 缺少 gpio 或 led_count | 400 | `"missing required field: gpio"` 或 `"led_count"` |
| 参数超出范围 | 400 | `"gpio must be 0-48, led_count must be 1-256"` |
| 已达上限 8 条 | 507 | `"max 8 strips reached"` |
| 热重载失败 | 500 | `"failed to add strip: ..."` |

### 示例

```bash
# 添加 GPIO 6 灯带（20 颗灯珠）
curl -X POST http://192.168.1.50/api/config/strip \
  -H "Content-Type: application/json" \
  -d '{"gpio":6,"led_count":20}'

# 修改 GPIO 8 灯带的灯珠数量为 50
curl -X POST http://192.168.1.50/api/config/strip \
  -H "Content-Type: application/json" \
  -d '{"gpio":8,"led_count":50}'

# 添加后再验证
curl http://192.168.1.50/api/health
```

---

## 7. POST /api/config/strip/remove

删除指定 GPIO 的灯带。不影响其他灯带。

### 请求

```
POST /api/config/strip/remove HTTP/1.1
Content-Type: application/json
```

```json
{
  "gpio": 6
}
```

### 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `gpio` | int | 是 | 要删除的灯带 GPIO 编号 |

### 成功响应 (200)

```json
{
  "status": "ok",
  "removed_gpio": 6,
  "total_strips": 1
}
```

### 错误响应示例

| 场景 | HTTP 状态 | message |
|------|-----------|---------|
| 缺少 gpio | 400 | `"missing required field: gpio"` |
| GPIO 不存在 | 404 | `"gpio 6 not found in config"` |
| 热重载失败 | 500 | `"failed to remove strip: ..."` |

### 示例

```bash
# 删除 GPIO 6 灯带
curl -X POST http://192.168.1.50/api/config/strip/remove \
  -H "Content-Type: application/json" \
  -d '{"gpio":6}'
```

---

## 错误响应统一格式

所有端点出错时返回统一格式：

```json
{
  "status": "error",
  "message": "人类可读的错误描述"
}
```

HTTP 状态码：400（请求错误）或 500（内部错误）。

---

## 典型使用流程

```bash
# 1. 检查设备在线
curl http://192.168.1.50/api/health

# 2. 添加灯带（一条一条加）
curl -X POST http://192.168.1.50/api/config/strip \
  -H "Content-Type: application/json" \
  -d '{"gpio":6,"led_count":20}'

# 3. 控制 LED
curl -X POST http://192.168.1.50/api/led/set \
  -H "Content-Type: application/json" \
  -d '{"leds":[{"gpio":6,"index":0,"mode":"blink","color":"#00ff00","duration_ms":5000}]}'

# 4. 删除灯带
curl -X POST http://192.168.1.50/api/config/strip/remove \
  -H "Content-Type: application/json" \
  -d '{"gpio":6}'

# 5. 关闭所有 LED
curl -X POST http://192.168.1.50/api/led/clear -d '{}'
```
