# Loop Surgeon 0.7.0-pre：Windows / REAPER 简要测试说明

这是本机生成的 Windows x64 测试包。本地 Release 编译和确定性 DSP 测试已通过；尚未
完成完整素材库盲听、VST3 Validator 和多采样率 REAPER 验证，因此仍是预发布测试版。
本次没有使用 GitHub Actions。

## 安装

1. 关闭 REAPER。
2. 把完整的 `Loop Surgeon.vst3` 文件夹复制到：

   ```text
   C:\Program Files\Common Files\VST3\
   ```

3. 打开 64 位 REAPER，进入 **Options > Preferences > Plug-ins > VST**。
4. 点击 **Clear cache/re-scan**，在 FX 浏览器搜索 `Loop Surgeon`。

## 基本动线

1. 把音频拖进插件，或点击 **Choose Audio...**。
2. 拖动蓝色 **Source In/Out**，限定允许处理的范围。
3. 选择模式、设定最终长度，点击蓝色生成按钮。
4. 用 **Source / Generated** 选择对象，用 **Preview / Stop** 开始和停止。
5. 满意后拖动 **Drag Loop to DAW**，或使用 **Save WAV...**。

## Rotate & Repair

1. 使用一段本身已经接近成品、只缺少顺畅首尾连接的氛围或环境音。
2. `Final Length = Selection` 会保留完整选择区；输入秒数后，会在蓝色范围内寻找对应
   精确长度的最佳连续窗口。长度不能超过蓝色范围。
3. **Seam Repair** 先使用 25–80 ms，再点击 **Repair Selected Loop**。
4. 生成后拖动绿色 **Loop Start**，确认新起点不会改变内容或产生新断点。
5. 连听至少十圈，检查内部修复点和最终尾到头；不得出现点击、抽吸、音量凹陷、倒放或
   短片段复制感。

## Texture Loop

1. 使用带有一次性 ADSR、撞击、pass-by 或明显动态轨迹的素材。
2. 设定精确 **Output Length**。
3. **Stability** 越高，原始动态轨迹越平；**Rebuild** 越高，越依赖干净的材质重构，
   越少保留原素材的事件与波形。
4. 点击 **Generate Texture Loop**，再用 **New Variation** 比较不同候选。
5. 重点检查：
   - 是否仍残留原 one-shot 的 Impact、衰减或 pass-by；
   - 是否变成普通白/粉/棕噪声，或出现固定电子音；
   - 颗粒素材是否仍有材质颗粒，同时没有密集重复 Attack；
   - 是否存在短周期复制、倒放感、频段空洞或不自然立体声；
   - 完整 WAV 尾到头是否无明显接点。

## 反馈时请附上

- 插件、REAPER、Windows 版本，以及工程采样率/缓冲；
- 原素材、导出结果、Source In/Out 和所有参数；
- 问题出现在哪一秒，属于重复、接缝、频段、颗粒、响度、相位还是方位。

