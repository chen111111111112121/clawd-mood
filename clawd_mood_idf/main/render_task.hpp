#pragma once

// 起渲染 task：以 ~30fps（RIG_TICK_MS 周期）非阻塞推进并重绘眼睛。
// 调用前需先 display::init() 和 eyes::init()。app_main 调一次即可。
void render_task_start();
