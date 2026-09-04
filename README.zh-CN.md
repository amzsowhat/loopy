# loopy

[English](README.md) | [简体中文](README.zh-CN.md)

**将录音素材制作为无缝、精确时长的循环。**

loopy 是一款面向声音设计师的 JUCE 音频插件，适用于环境声、机械声、天气、
房间底噪及其他连续录音。载入或录制音频，圈定有效素材并设置目标时长，即可生成
用于试听、WAV 导出或拖入 DAW 的循环。

## 两种模式

| | LOOP | TEXTURE |
| --- | --- | --- |
| 目标 | 自动完成声音设计师常规的 loop 制作流程 | 使用选定录音构建连续纹理 |
| 与源素材的关系 | 保持一个连续、正向播放的基础周期 | 仅重组源录音中相位相容的区域，同时保留素材身份 |
| 输出时长 | 可精确指定任意时长：短于、等于或长于源选区 | 可精确指定任意时长 |
| 长段输出 | 在相位对齐的边界上重复处理完成的基础周期 | 延续确定性、持续变化的素材遍历 |
| 主要塑形维度 | 接缝位置与接缝修复 | 宏观包络、局部动态、时间遍历与可选声音性格 |
| 更适合 | 需要保留原始时间线和运动过程的声音 | 需要转化为更稳定的纹理，同时不能变成合成声或泛化噪声的素材 |

`LOOP` 并非只用于缩短素材。目标较短时，它会在选区中寻找边界条件良好的
精确长度周期；目标较长时，它会先建立一个可循环的基础周期，再重复完整周期，使最终
边界回到相同相位。它覆盖声音设计师制作 loop 时通常要完成的操作：选择可用素材、
确定循环边界、建立连续接缝，并让结果适配所需的最终时长。

`TEXTURE` 是独立的源素材驱动构建模式，既不是单纯的长段输出模式，也不只是 ADSR
抹平器。它可以弱化宏观包络和通过感、控制较短尺度上反复出现的动态，并在环形时间线
上衔接相位相容的区域，使结果不再机械遵循原始时间顺序。声音材料始终来自选定录音；
不会用振荡器、合成噪声底、变调、拉伸或反向播放替代源素材。

## 功能

- 两种模式均支持用户自定义的精确输出时长。
- 使用波形、相位、电平、频谱、瞬态和立体声连续性分析候选边界。
- 立体声遍历与处理保持声道联动。
- 支持 WAV、AIFF、FLAC 和 OGG 输入。
- 导出 24-bit WAV，并支持拖放到 DAW。
- 将生成结果保存在 DAW 工程状态中。
- 清理直流偏移和非有限数值，并应用 `-1 dBTP` 真峰值上限。

## 基本流程

1. 载入录音，或录制插件输入。
2. 使用 **Source In** 和 **Source Out** 圈定有效素材。
3. 选择 **LOOP** 或 **TEXTURE**。
4. 设置目标输出时长并调整当前模式的控制项。
5. 生成结果，并连续试听多个完整循环。
6. 保存 WAV，或将结果拖入 DAW。

## 控制项

| LOOP | 作用 |
| --- | --- |
| Final Length | 使用完整选区，或输入任意精确输出时长 |
| Seam | 设置循环边界的最大交叉淡化长度 |
| Audition | 混合监听源素材与生成结果 |
| Loop Start / Join Position | 调整循环边界位置 |
| Options A-C | 选择不同的分析候选周期 |

| TEXTURE | 作用 |
| --- | --- |
| Length | 设置精确输出时长 |
| Stability | 减少源素材的宏观包络运动 |
| Crush | 减少较短尺度上反复出现的振幅起伏 |
| Transform | 控制结果偏离原始时间线的程度 |
| Flow / Drift / Fracture | 选择素材遍历的尺度与连续性 |
| Patina / Bloom / Fray | 应用可选的后级声音性格处理 |

## 构建

需要 CMake 3.22 或更高版本、安装 C++ 桌面开发组件的 Visual Studio 2022
（macOS 使用 Xcode），以及首次配置时用于获取 JUCE 8.0.13 的 Git 网络连接。

```powershell
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --preset test-debug -C Debug --output-on-failure
```

Windows Debug VST3 输出位置：

```text
build/vs2022/plugin/LoopSurgeon_artefacts/Debug/VST3/loopy.vst3
```

将完整的 `loopy.vst3` 文件夹复制到 `C:\Program Files\Common Files\VST3\`，
然后在 DAW 中重新扫描插件。当前 REAPER 检查流程见
[`docs/WINDOWS_REAPER_TEST.md`](docs/WINDOWS_REAPER_TEST.md)。

## 验证边界

确定性引擎测试和状态测试用于验证精确时长、可重复生成及数值安全。这些自动检查
不能证明所有录音都能获得主观上自然的循环，也不能代替宿主兼容性验收。真实素材试听
与目标 DAW 验收仍需单独完成。

macOS 目标已配置，但尚未在当前 Windows 开发机上完成构建或签名。

## 仓库结构

- `plugin/Source/` — 分析、DSP、状态与 JUCE 界面
- `plugin/Tests/` — 确定性引擎测试
- `docs/` — 算法、架构与宿主测试说明
- `plugin/Assets/` — 内置 Space Grotesk 字体及其许可证

## 许可证

本项目尚未授予覆盖整个源代码仓库的许可证。JUCE 采用双重许可模式，分发前需确认
适用条款。Space Grotesk 依据 SIL Open Font License 收录，许可证文件位于
`plugin/Assets/SpaceGrotesk-OFL.txt`。
