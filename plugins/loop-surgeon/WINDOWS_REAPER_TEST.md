# Loop Surgeon 0.5.1 Alpha：Windows / REAPER 快速测试

## 安装

1. 关闭 REAPER。
2. 解压测试包，将完整的 `Loop Surgeon.vst3` 文件夹复制到：

   ```text
   C:\Program Files\Common Files\VST3\
   ```

3. 打开 64 位 REAPER，进入 **Options > Preferences > Plug-ins > VST**。
4. 点击 **Clear cache/re-scan**，在 FX 浏览器搜索 `Loop Surgeon`。

## 风声 one-shot 推荐测试

1. 把素材拖进插件。
2. 用蓝色 **Source In/Out** 圈出允许取材的范围。
3. Mode 选择 **Evolving Texture**。
4. Length 先设 20–30 秒，Variation 先用 65–80%。
5. 点击 **Generate Texture**。
6. 点击 **Source** 会立即播放所选原素材；点击 **Generated** 会立即播放生成结果。
   **Preview/Stop** 控制播放和停止。
7. 在左侧下拉框比较三个版本；选择后会从头播放。**New Variation** 会重新生成三版。
8. 点击 **Export WAV**，把 WAV 拖回 REAPER，连续播放并重点检查：
   - 是否还能明显听出同一小段逐遍重复；
   - 原素材只有一次渐强/衰减时，结果是否错误地产生一串周期性鼓包或重新起音；
   - 结果是否主要保持原素材的音色颜色，同时像持续噪声一样自然微动；
   - 内部颗粒转换是否有抽吸、双影、突变或立体声漂移；
   - 完整 WAV 从尾部回到头部时是否可闻接缝。

## 其他模式

- **Seam Loop**：适合已经有明显周期的节奏、机械声、持续音；输出仍是传统短 loop。
- **Auto**：强周期、高置信度素材使用 Seam Loop，其余使用 Evolving Texture。判断错误时请
  手动选模式，并在反馈里注明素材类型。

## 工程召回

保存并重开 REAPER 工程。已选中的生成音频和参数应恢复，不需要原素材在线；Source
试听和另外两个未选版本不会完整写入工程。

## 反馈需要记录

- REAPER/Windows 版本与工程采样率；
- 素材类型、长度、单声道/立体声；
- Mode、Length、Variation、版本编号；
- 问题发生在内部转换还是整段首尾；
- 是否属于明显重复、点击、抽吸、相位/立体声变化、生成卡顿、导出或工程恢复。

本版本仍是 Alpha：没有波形缩放/吸附/逐样本编辑、完整频谱相位图、WAV loop 元数据，
也尚未完成大规模盲听验证。
