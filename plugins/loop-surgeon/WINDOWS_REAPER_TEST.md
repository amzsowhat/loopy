# Loop Surgeon 0.4 Alpha：Windows / REAPER 快速测试

这是 Windows x64 VST3 效果器测试版。它需要用户提供一段音频素材，再自动寻找、修复并导出无缝 loop。

## 1. 安装

1. 关闭 REAPER。
2. 解压测试包，将完整的 `Loop Surgeon.vst3` 文件夹复制到：

   ```text
   C:\Program Files\Common Files\VST3\
   ```

3. 打开 64 位 REAPER，进入 **Options > Preferences > Plug-ins > VST**。
4. 点击 **Clear cache/re-scan**。
5. 在 FX 浏览器搜索 `Loop Surgeon`，插入一条音频轨道。

如果扫描不到，请确认使用的是 64 位 REAPER；仍然失败时，安装最新的 Microsoft Visual C++ 2015–2022 x64 Redistributable 后重新扫描。

## 2. 推荐操作顺序

1. 将 WAV、AIFF、FLAC 或 OGG 拖入插件，或点击 **Choose Audio...**。
2. 在波形上半区拖动蓝色 **SEARCH IN / OUT**，圈定允许自动搜索的素材范围。
3. 点击 **Find Best Loop**。绿色 **LOOP IN / OUT** 是自动找到的最终循环范围。
4. 在候选菜单切换 1–3 个结果。
5. 点击 **Preview** 开始试听；同一个按钮会变为 **Stop**。用 **Original / Loop** 切换原始范围和循环结果。
6. 如需手动修改，在波形下半区拖动绿色 LOOP 标记。主按钮会变成 **Use Manual Loop**；点击后会保留绿色位置并重新评估接缝，不会跳回自动位置。
7. **Max Repair Window** 是允许算法使用的最大修复窗口，算法会自动测试更短的窗口；修改后要重新执行 Find Best Loop 或 Use Manual Loop。
8. 底部质量条显示 Quality、Repair、Spectrum、Phase、Stereo、Transient。低分不是装饰性警告，应切换候选或重新选择蓝色范围。
9. 满意后点击 **Export Loop WAV**，导出 24-bit WAV，再拖回 REAPER 连续铺排检查。
10. 保存 REAPER 工程、关闭并重开；已完成的 loop 应随工程恢复。

## 3. 建议测试素材

- 单声道和立体声各一份；
- 44.1、48、96 kHz 各一份；
- 环境声、风雨、机械声、持续音、鼓或节奏素材；
- 一份难以循环的语音或单次冲击声，确认插件会显示低置信度，而不是假装成功。

## 4. 当前 Alpha 限制

- 没有波形缩放、时间尺、零交叉/瞬态吸附和键盘逐样本微调；
- 没有 seam-only、10/30/100 次耐久试听；
- 工程会保存完成的 loop，但不会完整保存原素材和全部候选；
- 导出暂未写入 `cue`/`smpl` loop 元数据；
- 大文件解码和导出仍可能短暂阻塞界面。

反馈时请记录 REAPER/Windows 版本、采样率、素材类型、蓝色和绿色范围、候选编号、Max Repair Window，以及问题属于扫描、卡顿、接缝可闻、工程恢复还是导出不一致。

