# Loop Surgeon 0.8.0-pre：Windows / REAPER 简要测试说明

这是本机构建的 Windows x64 预发布测试包。本地编译和确定性 DSP 测试已通过；完整素材库盲听、VST3 Validator、多采样率 REAPER 验证仍未完成。本次没有使用 GitHub Actions。

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
3. 选择模式、设定最终长度，点击 Generate。
4. 用 **Source / Generated** 选择试听对象，用 **Preview / Stop** 开始和停止。
5. 满意后拖动 **Drag Loop to DAW**，或使用 **Save WAV...**。

## Rotate & Repair

1. 适合整体已经接近成品、只缺顺畅首尾连接的较长氛围或环境音。
2. `Final Length = Selection` 保留完整选择区；输入秒数后会在蓝色范围内寻找对应精确长度的最佳连续窗口，长度不能超过 Source In/Out。
3. **Seam Repair** 先使用 25–80 ms，再生成。
4. 生成后可拖动绿色 **Loop Start**；它只改变循环起点，不改变内容。
5. 连听至少十圈，检查内部修复点和最终尾到头，不应出现点击、抽吸、音量凹陷、倒放或短片段复制感。

## Texture Loop

1. **Material = Auto** 会按信号结构选路；判断不合适时手动选 **Continuous** 或 **Particles**。
2. 设定精确 **Output Length**。
3. **Stability** 控制宏观动态和轨迹的平稳程度；**Transform** 控制重组深度。
4. 点击 Generate，再用 **New Variation** 比较不同候选。
5. 重点检查：

   - 原 one-shot 的完整 ADSR、撞击或 pass-by 是否仍周期性重现；
   - 输出是否退化成普通白/粉/棕噪声或固定电子音；
   - 局部共振、颗粒、摩擦、气流等材质信息是否仍可辨识；
   - 是否出现短周期复制、倒放感、频段空洞或不自然立体声；
   - 完整 WAV 尾到头是否无明显接点。

## 反馈时请附上

- 插件、REAPER、Windows 版本和工程采样率/缓冲；
- 原素材、导出结果、Source In/Out、Material 路径和全部参数；
- 问题出现在哪一秒，属于重复、接缝、频段、瞬态、响度、相位、方位还是材质丢失。
