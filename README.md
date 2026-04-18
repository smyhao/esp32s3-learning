# ESP32-S3 Learning

基于 ESP-IDF 框架的 ESP32-S3 学习项目。

## 硬件

- **芯片**: ESP32-S3
- **Flash**: 16MB

## 分区表

| 分区      | 类型   | 大小   | 说明           |
| --------- | ------ | ------ | -------------- |
| nvs       | data   | 24 KB  | 非易失性存储   |
| phy_init  | data   | 4 KB   | PHY 校准数据   |
| factory   | app    | 1.9 MB | 应用程序       |
| vfs       | data   | 10 MB  | FAT 文件系统   |
| storage   | data   | 4 MB   | SPIFFS 存储    |

## 开发环境

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v5.x
- VS Code + ESP-IDF 插件 / DevContainer

## 构建与烧录

```bash
idf.py build
idf.py -p COMx flash monitor
```
