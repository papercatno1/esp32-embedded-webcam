// 照相模式（从 firmware/webcam 合并进来的最小可用版）
// 只保留：摄像头初始化 + 8080 端口的 MJPEG 流 / 单帧取图 / 状态 JSON
#pragma once
#include <Arduino.h>

bool cam_begin();      // 初始化摄像头并开始监听 8080；失败返回 false
void cam_end();        // 停止监听并释放摄像头（省内存）
void cam_loop();       // 每轮 loop 调一次：接受新连接 + 推帧
bool cam_active();

float    cam_fps();
int      cam_viewers();
uint32_t cam_frames();
const char *cam_res();
