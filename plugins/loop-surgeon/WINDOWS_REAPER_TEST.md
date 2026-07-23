# Loop Surgeon 0.3 Alpha：Windows / REAPER 快速测试

这是 Windows x64 的 VST3 效果器测试版。它不发声，必须加载或接收一段音频素材，再从中寻找并生成无缝循环。

## 1. 安装

1. 关闭 REAPER。
2. 解压测试包。将完整的 `Loop Surgeon.vst3` 文件夹复制到：

   ```text
   C:\Program Files\Common Files\VST3\
   ```

3. 打开 64 位 REAPER，进入 **Options > Preferences > Plug-ins > VST**。
4. 点击 **Clear cache/re-scan**。
5. 在 FX 浏览器搜索 `Loop Surgeon`，把它插入一条音频轨道。

如果扫描不到，请先确认使用的是 64 位 REAPER；仍然失败时，安装最新的 Microsoft Visual C++ 2015–2022 x64 Redistributable 后重新扫描。

## 2. 五分钟操作

1. 把 WAV、AIFF、FLAC 或 OGG 文件拖进插件，或点击 **Import Audio**。
2. 拖动蓝色 **Source In / Out**，圈定“允许插件取样和搜索”的素材范围。
3. 先设定 **Seam repair**，再点击 **Analyze Selection**。绿色 **Loop In / Out** 是自动找到的循环范围。
4. 切换候选结果 1–3，用 **Original / Loop** 对比原素材和连续循环。
5. 如需微调，可拖动绿色 **Loop In / Out**；重点听连接处有没有咔哒声、音量鼓包、相位变薄或节奏跳变。
6. 满意后点击 **Export Loop WAV**，导出 24-bit WAV，再拖回 REAPER 连续铺排检查。
7. 保存 REAPER 工程、关闭并重开；完成的 loop 应随工程恢复。

> 当前 Alpha 的已知问题：分析完成后再改变 **Seam repair**，可能导致绿色 Loop Out 与实际渲染长度不完全一致。测试时请先设 Seam repair 再分析；若之后改变它，请重新点击 **Analyze Selection**。

## 3. 建议测试素材

- 单声道和立体声各一份；
- 44.1、48、96 kHz 各一份；
- 环境声、风雨、机械声、持续音、鼓或节奏素材；
- 一份故意难以循环的语音或单次冲击声，用来确认插件会给出低置信度，而不是假装成功。

## 4. 反馈时请记录

- REAPER 版本、Windows 版本、声卡采样率；
- 素材类型和采样率；
- Source In / Out、所选候选、Seam repair 数值；
- 问题是扫描失败、界面卡住、连接点可闻、工程恢复失败，还是导出结果不一致。

