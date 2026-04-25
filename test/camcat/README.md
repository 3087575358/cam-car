# CamCat - ESP32 Camera Car Control

## 项目简介

这是一个基于 ESP32 的智能小车项目，集成了摄像头实时视频流传输和网页远程控制功能。本项目通过 HTTP 服务器实现视频流传输和小车控制的 Web 界面。

## 代码逻辑详解

### 一、整体架构

```
┌─────────────────────────────────────────────────────────┐
│                    ESP32 主控                          │
├──────────────────┬────────────────────────────────────┤
│   摄像头模块     │          小车控制模块              │
│                 │                                  │
│  - 图像采集     │  - L298N 电机驱动                │
│  - 格式转换     │  - PWM 速度控制                  │
│  - 面部识别*    │  - 方向控制                      │
│  - 视频流传输   │  - HTTP 命令处理                 │
│                 │                                  │
└────────┬────────┴────────┬──────────────────────────┘
         │                 │
    HTTP 服务器           │
         │                 │
         └────────┬────────┘
                  │
         ┌────────▼────────┐
         │   Web 浏览器     │
         │  - 视频显示      │
         │  - 控制面板      │
         └─────────────────┘
```

* 面部识别功能需要 PSRAM 支持

### 二、文件结构说明

#### 1. app_httpd.cpp - 核心功能实现

##### 2.1.1 头文件和宏定义 (第 1-38 行)

```cpp
#include "esp_http_server.h"      // HTTP 服务器核心库
#include "esp_timer.h"           // 高精度定时器
#include "esp_camera.h"          // 摄像头驱动
#include "img_converters.h"       // 图像格式转换
#include "fb_gfx.h"              // 图形绘制（如人脸框）
#include "esp32-hal-ledc.h"      // LED PWM 控制
#include "camera_index.h"         // 压缩的 HTML 文件
```

**PSRAM 和面部识别条件编译 (第 40-53 行)**

代码根据以下条件决定是否启用面部识别：
- `BOARD_HAS_PSRAM` - 是否有外部 PSRAM
- `CONFIG_IDF_TARGET_ESP32S3` - 目标芯片是否为 ESP32S3

```cpp
#ifdef BOARD_HAS_PSRAM
    #define CONFIG_ESP_FACE_DETECT_ENABLED 1
    // ESP32S3 芯片速度快，可以支持面部识别
    // 其他芯片识别速度太慢（15秒/帧），默认关闭
    #if CONFIG_IDF_TARGET_ESP32S3
        #define CONFIG_ESP_FACE_RECOGNITION_ENABLED 1
    #else
        #define CONFIG_ESP_FACE_RECOGNITION_ENABLED 0
    #endif
#else
    // 无 PSRAM 则关闭所有 AI 功能
    #define CONFIG_ESP_FACE_DETECT_ENABLED 0
    #define CONFIG_ESP_FACE_RECOGNITION_ENABLED 0
#endif
```

**面部识别神经网络模型 (第 57-88 行)**

```cpp
#include "human_face_detect_msr01.hpp"  // 人脸检测模型 MSN01
#include "human_face_detect_mnp01.hpp"  // 人脸检测模型 MNP01
#include "face_recognition_tool.hpp"     // 面部识别工具
#include "face_recognition_112_v1_s16.hpp"  // S16 量化模型
#include "face_recognition_112_v1_s8.hpp"   // S8 量化模型
```

- `TWO_STAGE = 1`: 两阶段检测（准确但慢，带关键点）
- `TWO_STAGE = 0`: 单阶段检测（快速但较简单，无关键点）
- `QUANT_TYPE = 0`: 使用 S8 模型（固件小、速度快）
- `QUANT_TYPE = 1`: 使用 S16 模型（固件大、速度慢）

**LED 补光灯设置 (第 92-100 行)**

```cpp
#define CONFIG_LED_ILLUMINATOR_ENABLED 1  // 启用 LED
#define LED_LEDC_CHANNEL 2               // LEDC 通道2
#define CONFIG_LED_MAX_INTENSITY 255      // 最大亮度 255 级
int led_duty = 0;                      // 当前亮度
bool isStreaming = false;               // 是否正在流传输
```

**滑动平均滤波器 (第 138-189 行)**

用于平滑帧率计算，避免帧率突变的噪声影响：

```cpp
typedef struct {
    size_t size;      // 滤波窗口大小
    size_t index;     // 当前索引
    size_t count;     // 实际数据个数
    int sum;          // 累加和
    int *values;      // 数据数组
} ra_filter_t;
```

**原理**：维护一个固定大小的数组，新数据覆盖旧数据，返回平均值。

---

##### 2.1.2 小车控制模块 (第 23-34 行, 1473-1599 行)

**引脚定义**

```cpp
#define ENA    5   // 右电机 PWM 使能/速度 (GPIO5)
#define ENB    4   // 左电机 PWM 使能/速度 (GPIO4)
#define IN_1   14  // 右电机方向1 (GPIO14)
#define IN_2   12  // 右电机方向2 (GPIO12)
#define IN_3   13  // 左电机方向1 (GPIO13)
#define IN_4   15  // 左电机方向2 (GPIO15)

int speedCar = 1023;      // PWM 占空比 0-1023
int speedInterval = 90;    // 速度档间隔
float speedTurn = 1.5;     // 转弯时内侧轮减速系数
```

**L298N 电机驱动原理**

```
IN1  IN2  |  右电机状态
-----|-----|-----------
 1    0   |  正转
 0    1   |  反转
 0    0   |  停止
 1    1   |  刹车（不建议）

IN3  IN4  |  左电机状态
-----|-----|-----------
 0    1   |  正转
 1    0   |  反转
 0    0   |  停止
 1    1   |  刹车（不建议）
```

**小车运动控制函数详解**

1. **前进** `carForward()` (1484-1492 行)
   ```cpp
   analogWrite(ENA, speedCar);   // 右电机满速
   analogWrite(ENB, speedCar);   // 左电机满速
   digitalWrite(IN_1, 1);        // 右电机正转
   digitalWrite(IN_2, 0);
   digitalWrite(IN_3, 0);        // 左电机正转
   digitalWrite(IN_4, 1);
   ```

2. **后退** `carBackward()` (1495-1503 行)
   ```cpp
   // 两电机反转
   digitalWrite(IN_1, 0);
   digitalWrite(IN_2, 1);
   digitalWrite(IN_3, 1);
   digitalWrite(IN_4, 0);
   ```

3. **左转** `carLeft()` (1506-1514 行)
   ```cpp
   // 两侧电机反向转动，原地旋转
   digitalWrite(IN_1, 1);  // 右电机正转
   digitalWrite(IN_2, 0);
   digitalWrite(IN_3, 1);  // 左电机反转
   digitalWrite(IN_4, 0);
   ```

4. **右转** `carRight()` (1517-1525 行)
   ```cpp
   // 两侧电机反向转动，原地旋转
   digitalWrite(IN_1, 0);  // 右电机反转
   digitalWrite(IN_2, 1);
   digitalWrite(IN_3, 0);  // 左电机正转
   digitalWrite(IN_4, 1);
   ```

5. **停止** `carStop()` (1528-1534 行)
   ```cpp
   // 所有方向引脚设为 0，电机断电停止
   digitalWrite(IN_1, 0);
   digitalWrite(IN_2, 0);
   digitalWrite(IN_3, 0);
   digitalWrite(IN_4, 0);
   ```

6. **斜向运动实现原理**

   以 `carLeftForward()` (1537-1545 行) 为例：
   ```cpp
   analogWrite(ENA, speedCar);          // 右电机满速
   analogWrite(ENB, speedCar/speedTurn); // 左电机减速
   digitalWrite(IN_1, 1);               // 右电机正转
   digitalWrite(IN_3, 0);               // 左电机停止
   ```

   **原理**：单侧电机减速或停止，使小车向该侧偏转前进。

   - `speedTurn = 1.5` 表示转弯时内侧轮速度为外侧轮的 1/1.5 ≈ 67%
   - 右轮满速前进 + 左轮减速前进 = 向左前斜向移动

7. **速度控制** `carSetSpeed(int level)` (1582-1592 行)
   ```cpp
   speedCar = 1023 - (9 - level) * speedInterval;
   ```
   计算：
   - 1档：1023 - 8*90 = 303
   - 5档：1023 - 4*90 = 663
   - 9档：1023 - 0*90 = 1023

**小车 HTTP 命令处理** `car_control_handler()` (1595-1650 行)

```cpp
// 解析 URL 参数: /car?cmd=forward
if (httpd_query_key_value(buf, "cmd", cmd, sizeof(cmd)) == ESP_OK) {
    if (strcmp(cmd, "forward") == 0) {
        carForward();
    } else if (strcmp(cmd, "speed_1") == 0) {
        carSetSpeed(1);
    }
    // ... 其他命令
}
```

支持的命令列表：
- 基础运动：`forward`, `backward`, `left`, `right`, `stop`
- 斜向运动：`left_forward`, `right_forward`, `left_backward`, `right_backward`
- 速度控制：`speed_1` 到 `speed_9`

---

##### 2.1.3 HTTP 服务器模块

**HTTP 请求处理流程**

```
HTTP 请求
    ↓
index_handler() → 根据摄像头型号返回对应的 HTML
cmd_handler()   → 处理摄像头参数设置
capture_handler() → 捕获单帧图片
stream_handler() → 传输视频流
car_control_handler() → 处理小车控制命令
```

**关键数据结构** `jpg_chunking_t` (92-96 行)
```cpp
typedef struct {
    httpd_req_t *req;  // HTTP 请求对象
    size_t len;         // 图片数据长度
} jpg_chunking_t;
```

用于视频流分块传输。

**MJPEG 视频流协议** (99-102 行)

```cpp
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY =
    "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";
```

**MJPEG 数据包格式**：
```
--123456789000000000000987654321
Content-Type: image/jpeg
Content-Length: 12345
X-Timestamp: 1234567890.123456

<JPEG二进制数据>
--123456789000000000000987654321
Content-Type: image/jpeg
...
```

**流传输处理** `stream_handler()` (670-952 行)

核心逻辑：
```cpp
while (true) {  // 无限循环，持续传输
    // 1. 获取摄像头帧
    fb = esp_camera_fb_get();

    // 2. 如果需要，执行人脸检测和识别
    if (detection_enabled) {
        // 检测人脸 → 识别人脸 → 绘制框和ID
    }

    // 3. 转换为 JPEG 格式
    frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);

    // 4. 发送边界标记
    httpd_resp_send_chunk(req, _STREAM_BOUNDARY, ...);

    // 5. 发送 JPEG 数据
    httpd_resp_send_chunk(req, _jpg_buf, _jpg_buf_len);

    // 6. 返回帧缓冲区
    esp_camera_fb_return(fb);
}
```

**帧率平滑计算** (第 171-189 行)

使用滑动平均滤波器计算实际帧率，避免瞬时波动。

---

##### 2.1.4 摄像头控制模块

**捕获处理** `capture_handler()` (412-666 行)

流程：
1. 可选：开启 LED 补光灯（延迟 150ms 让灯光稳定）
2. 获取摄像头帧 `esp_camera_fb_get()`
3. 如果启用人脸检测：
   - 将帧转换为 RGB888 格式
   - 运行神经网络检测人脸
   - 在图像上绘制人脸框和ID
   - 转换回 JPEG 格式
4. 直接发送 JPEG 数据

**命令处理** `cmd_handler()` (980-1116 行)

支持的参数（通过 `/control?var=xxx&val=yyy` 访问）：
- `framesize`: 分辨率
- `quality`: JPEG 质量
- `brightness`: 亮度
- `contrast`: 对比度
- `saturation`: 饱和度
- `awb`: 自动白平衡
- `aec`: 自动曝光
- `agc`: 自动增益
- `led_intensity`: LED 亮度
- `face_detect`: 人脸检测开关
- `face_recognize`: 人脸识别开关

**状态查询** `status_handler()` (1138-1257 行)

返回 JSON 格式的摄像头状态：
```json
{
  "xclk": 20,
  "pixformat": 3,
  "framesize": 10,
  "quality": 10,
  "brightness": 0,
  "contrast": 0,
  "saturation": 0,
  "awb": 1,
  "aec": 1,
  "led_intensity": 255,
  "face_detect": 1,
  "face_recognize": 0
}
```

---

##### 2.1.5 服务器初始化 (第 1824-1903 行)

**startCameraServer() 函数流程**

```cpp
void startCameraServer() {
    // 1. 配置 HTTP 服务器
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    // 2. 定义 URI 处理器
    httpd_uri_t index_uri = {"/", HTTP_GET, index_handler, NULL};
    httpd_uri_t cmd_uri = {"/control", HTTP_GET, cmd_handler, NULL};
    httpd_uri_t stream_uri = {"/stream", HTTP_GET, stream_handler, NULL};
    httpd_uri_t car_uri = {"/car", HTTP_GET, car_control_handler, NULL};
    // ... 更多 URI

    // 3. 启动主服务器 (端口 80)
    httpd_start(&camera_httpd, &config);
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    // ... 注册所有 URI

    // 4. 启动流服务器 (端口 81)
    config.server_port += 1;  // 改为 81
    httpd_start(&stream_httpd, &config);
    httpd_register_uri_handler(stream_httpd, &stream_uri);
}
```

**两个服务器的作用**：
- 端口 80：处理静态请求和控制命令（单次请求-响应）
- 端口 81：处理视频流（长连接，持续传输）

---

#### 2. index_ov2640.html - 网页控制界面

##### 2.2.1 页面结构

```html
<body>
  <section class="main">
    <!-- 左侧设置面板 -->
    <div id="sidebar">
      <nav id="menu">
        <!-- 摄像头参数设置 -->
        <input id="framesize">  <!-- 分辨率 -->
        <input id="quality">   <!-- 质量 -->
        <input id="brightness"><!-- 亮度 -->
        <!-- ... 更多参数 -->

        <!-- 小车控制面板 -->
        <div class="car-control">
          <div class="car-buttons">
            <button data-cmd="forward">↑</button>
            <button data-cmd="stop">⏹</button>
            <!-- ... 9个方向按钮 -->
          </div>
          <div class="speed-control">
            <button data-speed="1">1</button>
            <!-- ... 9档速度 -->
          </div>
        </div>
      </nav>
    </div>

    <!-- 右侧视频显示 -->
    <figure>
      <div id="stream-container">
        <img id="stream" src="">  <!-- 视频流 -->
      </div>
    </figure>
  </section>
</body>
```

##### 2.2.2 JavaScript 核心逻辑

**1. 获取主机地址** (714-715 行)
```javascript
var baseHost = document.location.origin  // 如 http://192.168.1.100
var streamUrl = baseHost + ':81'        // 视频流端口
```

**2. HTTP 请求封装** (717-733 行)
```javascript
function fetchUrl(url, cb) {
    fetch(url)
        .then(response => {
            if (response.status !== 200) {
                cb(response.status, response.statusText);
            } else {
                response.text().then(data => cb(200, data));
            }
        })
        .catch(err => cb(-1, err));
}
```

**3. 参数更新** (950-974 行)
```javascript
function updateConfig(el) {
    let value;
    switch (el.type) {
        case 'checkbox': value = el.checked ? 1 : 0; break;
        case 'range': value = el.value; break;
        case 'select-one': value = el.value; break;
    }
    const query = `${baseHost}/control?var=${el.id}&val=${value}`;
    fetch(query);
}
```

**4. 视频流控制** (1015-1045 行)
```javascript
function startStream() {
    view.src = `${streamUrl}/stream`;  // 设置 MJPEG 流源
    show(viewContainer);
}

function stopStream() {
    window.stop();  // 停止加载
}

streamButton.onclick = () => {
    if (streamButton.innerHTML === 'Stop Stream') {
        stopStream();
    } else {
        startStream();
    }
};
```

**5. 小车控制** (1163-1176 行)
```javascript
function sendCarCommand(cmd) {
    const url = `${baseHost}/car?cmd=${cmd}`;
    fetch(url);
}

// 为每个按钮绑定点击事件
document.querySelectorAll('.car-buttons button').forEach(button => {
    button.addEventListener('click', () => {
        const cmd = button.getAttribute('data-cmd');
        sendCarCommand(cmd);
    });
});
```

---

#### 3. camera_index.h - 压缩 HTML 存储

**文件格式**

```cpp
//File: index_ov2640.html.gz, Size: 6787
#define index_ov2640_html_gz_len 6787
const uint8_t index_ov2640_html_gz[] = {
    0x1F, 0x8B, 0x08, 0x08,  // GZIP 文件头
    0x23, 0xFC, 0x69, 0x5E,  // 时间戳
    0x00, 0x03,               // 压缩方法 (deflate), 原始文件名
    0x69, 0x6E, 0x64, 0x65, 0x78, 0x5F, 0x6F, 0x76, 0x32, 0x36, 0x34, 0x30, 0x2E, 0x68, 0x74, 0x6D, 0x6C,  // "index_ov2640.html"
    // ... 压缩数据
};
```

**为什么使用 gzip 压缩？**
1. 节省 Flash 空间（压缩率约 85%）
2. HTTP 传输更快
3. 浏览器自动解压

---

#### 4. 工具脚本

**extract_gz_from_header.py**

功能：从 `camera_index.h` 提取并解压 HTML 文件

```python
def extract_compressed_data_from_header(header_file):
    # 1. 读取头文件
    with open(header_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # 2. 使用正则表达式查找压缩数据块
    pattern = r'#define\s+(\w+_len)\s+(\d+)\s*\n\s*const\s+uint8_t\s+(\w+)\[\]\s*=\s*\{(.*?)\};'
    matches = re.findall(pattern, content, re.DOTALL)

    # 3. 解析十六进制数据
    for len_define, length, array_name, hex_data in matches:
        hex_values = re.findall(r'0x[0-9A-Fa-f]{2}', hex_data)
        gz_data = bytes(int(h, 16) for h in hex_values)

        # 4. 解压并保存
        decompressed = gzip.decompress(gz_data)
        with open(output_file, 'wb') as f:
            f.write(decompressed)
```

---

## 文件说明

### 核心文件
- `app_httpd.cpp` - HTTP 服务器和摄像头流媒体处理代码，包含小车控制功能
- `camera_index.h` - 包含压缩的 HTML 网页文件（OV2640、OV3660、OV5640）
- `index_ov2640.html` - OV2640 摄像头的网页控制界面（已添加小车控制）

### 小车控制文件
- `esp8266_wificar.ino` - 原始 ESP8266 小车驱动程序（参考代码）

### 工具脚本
- `extract_gz_from_header.py` - 从 camera_index.h 提取并解压 HTML 文件的脚本
- `unzip_gz.py` - 通用 gzip 解压工具

---

## 硬件连接

### 摄像头连接
根据具体的摄像头模块（OV2640/OV3660/OV5640）连接到 ESP32 的摄像头接口

### L298N 电机驱动器连接
```
ESP32        L298N
-----        -----
GPIO 5  -->  ENA (右电机使能/速度)
GPIO 4  -->  ENB (左电机使能/速度)
GPIO 14 -->  IN1 (右电机控制1)
GPIO 12 -->  IN2 (右电机控制2)
GPIO 13 -->  IN3 (左电机控制1)
GPIO 15 -->  IN4 (左电机控制2)
```

### 电机连接
```
右电机: 连接到 L298N 的 OUT1 和 OUT2
左电机:  连接到 L298N 的 OUT3 和 OUT4
```

---

## 功能特性

### 摄像头功能
- 实时视频流传输
- 静态图片捕获
- 多种分辨率支持
- 图像参数调节（亮度、对比度、饱和度等）
- 可选的面部识别功能（需要 PSRAM）

### 小车控制功能
- **方向控制**：前进、后退、左转、右转
- **斜向控制**：左前、右前、左后、右后（单侧轮减速实现）
- **速度控制**：9 档速度可调
- **急停功能**：一键停止

---

## HTTP API 接口

### 小车控制接口
```
/car?cmd=forward           # 前进
/car?cmd=backward          # 后退
/car?cmd=left              # 左转
/car?cmd=right             # 右转
/car?cmd=stop              # 停止
/car?cmd=left_forward      # 左前
/car?cmd=right_forward     # 右前
/car?cmd=left_backward     # 左后
/car?cmd=right_backward    # 右后
/car?cmd=speed_1           # 速度1档
/car?cmd=speed_2           # 速度2档
...
/car?cmd=speed_9           # 速度9档
```

### 摄像头控制接口
```
/                    # 网页界面
/status              # 获取状态
/control             # 控制参数
/capture             # 拍摄照片
/stream              # 视频流
```

---

## 编译和使用

### 1. 准备开发环境
- 安装 PlatformIO 或 Arduino IDE
- 安装 ESP32 开发板支持

### 2. 配置引脚
在主程序的 `setup()` 中调用：
```cpp
setupCamera();          // 初始化摄像头
setupLedFlash(LED_PIN); // 如果有 LED 补光灯
setupCarControl();      // 初始化小车控制
```

### 3. 上传代码
将代码编译并上传到 ESP32

### 4. 访问网页
连接到 ESP32 的 WiFi，然后在浏览器中访问：
```
http://<ESP32_IP地址>
```

---

## 工具使用

### 提取 HTML 文件
如果需要修改网页界面：
```bash
python extract_gz_from_header.py
```
这会从 `camera_index.h` 提取并解压出三个 HTML 文件。

### 解压 gzip 文件
```bash
python unzip_gz.py
```

---

## 注意事项

1. **电源供应**：ESP32 和电机驱动器需要独立的电源供应，避免电机启动时电压波动影响 ESP32
2. **速度设置**：默认最高速度为 1023，可根据实际需要调整 `speedCar` 变量
3. **WiFi 设置**：根据需要修改 WiFi 连接配置
4. **引脚定义**：如需更改引脚，请同步修改 `app_httpd.cpp` 中的引脚定义

---

## 开发者

基于 ESP32 Camera Web Server 和 ESP8266 小车控制代码修改整合。

## 许可证

Apache License 2.0
