# Loop Surgeon 0.9.0-pre：Windows / REAPER 测试说明

这是本机生成的 Windows x64 预发布测试包。本地编译和确定性测试已通过；主观听感、VST3
Validator、多采样率与 Apple Silicon 尚未完成，当前版本不能视为可售成品。

## 安装

1. 关闭 REAPER。
2. 把完整的 `Loop Surgeon.vst3` 文件夹复制到：

   ```text
   C:\Program Files\Common Files\VST3\
   ```

3. 打开 REAPER，进入 **Options > Preferences > Plug-ins > VST**。
4. 点击 **Clear cache/re-scan**，在 FX 浏览器搜索 `Loop Surgeon`。

## 操作

1. 把音频拖进插件，或点击 **Choose Audio...**。
2. 拖动蓝色 **Source In/Out**，限定分析范围。
3. 选择 **Rotate & Repair** 或 **Texture Loop**，设置最终长度并点击 Generate。
4. 使用 **Source / Generated** 与 **Preview / Stop** 对比。
5. 满意后使用 **Drag Loop to DAW** 或 **Save WAV...**。

## Texture Loop

- **Organism**：共享运动与独立频区变化保持平衡。
- **Spectral Drift**：频区耦合更强，整体变化更平滑。
- **Fracture**：频区独立度更高，变化更激进。
- **Stability**：控制整段循环的宏观运动幅度。
- **Transform**：控制共振强化、谱域变异和源相位记忆。
- **New Variation**：改变闭合轨迹种子。

重点听以下问题：

- 是否听到完整事件或片段以固定间隔重新出现；
- 是否退化成普通白、粉、棕噪声或固定电子音；
- 是否仍保留可辨识的材质属性；
- 是否出现倒放感、颗粒火车、声像塌陷、破音或炸音；
- 连续播放十圈后，尾到头是否有明显接点；
- 三种 Style 是否真的给出不同且可用的声音答案。

反馈时请附上源文件、导出 WAV、Style、Output Length、Stability、Transform、Variation、
工程采样率，以及问题出现的准确时间。
