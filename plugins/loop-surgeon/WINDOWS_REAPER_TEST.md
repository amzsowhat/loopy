# Loop Surgeon 0.6.0：Windows / REAPER 测试说明

当前 0.6.0 只有源码，尚未构建测试包。GitHub Actions 自动触发已关闭，避免继续消耗已
用完的额度；旧 0.5.2 VST3 不能用于判断这里的新算法。

## 有新测试包后的安装

1. 关闭 REAPER。
2. 将完整的 `Loop Surgeon.vst3` 文件夹复制到：

   ```text
   C:\Program Files\Common Files\VST3\
   ```

3. 打开 64 位 REAPER，进入 **Options > Preferences > Plug-ins > VST**。
4. 点击 **Clear cache/re-scan**，在 FX 浏览器搜索 `Loop Surgeon`。

## 模式一：Rotate & Repair

1. 使用一段已经接近成品、整体平稳但原首尾接不上的十几秒氛围或环境音。
2. 选择 **Rotate & Repair**，拖入素材，用蓝色 **Source In/Out** 保留整段目标内容。
3. **Seam Repair** 先用 25–80 ms，点击 **Repair Selected Loop**。
4. 生成结果的长度应接近整段蓝色范围，不能退化成一个很短的重复片段。
5. 绿色 **Loop Start** 是成品从哪里开始；拖动它后再次试听。完整循环的尾到头应是原
   素材中本来相邻的采样，旧首尾的交叉修复位于循环内部。
6. 连听至少十次循环，检查内部修复点和最终尾到头两个位置；记录抽吸、相位摆动、音量
   凹陷、点击或内容顺序错误。

## 模式二：Texture Loop

1. 使用带一次性 ADSR、pass-by、rise/fall 或明显动态起伏的风声等素材。
2. 选择 **Texture Loop**，用蓝色 Source In/Out 限定允许建模的材料。
3. 设定精确 **Output Length**；**Flatten** 控制动态/运动被压平多少，**Source Match** 控制
   响度、左右方位、声道相关性和相位关系匹配深度。
4. 点击 **Generate Texture Loop**，比较两个候选；**New Variation** 再生成两个。
5. 用 **Source / Generated** 选择试听对象，**Preview / Stop** 控制开始和停止。
6. 检查真实频谱叠图、Phase、Correlation 和 Position，再重点听：
   - 原 one-shot 的攻击、衰减和 pass-by 是否消失；
   - 是否出现固定电音、窄带啸叫、倒放感或颗粒式多次 attack；
   - 是否一耳朵听出短周期复制粘贴；
   - 原音色、响度、左右位置和声道关系是否仍合理；
   - 完整 WAV 尾到头是否无明显接点。

## 交付与召回

- 拖 **Drag Loop to DAW** 到 REAPER 时间线，或使用 **Save WAV...**。
- 两条路径必须产生与插件试听一致的 24-bit WAV。
- 保存并重开工程后，Active Result、源音频、Source In/Out、Loop Start 和参数都应恢复；
  未选中的另一个候选无需恢复。

## 每次反馈记录

- 插件版本、REAPER/Windows 版本、工程采样率和缓冲；
- 素材类型、长度、采样率、单/双声道；
- 模式与全部模式参数；
- 问题发生在内部修复点、整段尾到头、长时重复、频段、响度、相位还是方位；
- 导出/拖放、工程保存和重开是否一致。
