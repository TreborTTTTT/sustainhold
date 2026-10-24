# sustain-hold Mac 版使用说明（TAL-U-NO-LX-V2 延音踏板补丁）

老版 TAL-U-No-62 的 Mac 版是 32 位，macOS Catalina 及以后完全无法加载。
Mac 上用官方免费继任者 **TAL-U-NO-LX-V2**（同为 Juno-60 复刻，6 声部，含 64 位 VST2），
本补丁给它加上 CC64 延音踏板（踩住延音、松开除音、6 声部自动顶替最老保持音），
行为与 Windows 版 TAL-U-No-62 补丁完全一致。

## 安装步骤

1. 先装官方插件：https://tal-software.com 下载 TAL-U-NO-LX-V2（免费），运行官方 installer。
2. 解压 `sustainhold-mac.tar.gz` 到任意文件夹。
3. 双击 `install.command`（如提示"无法打开"，右键 → 打开），输入开机密码。
4. 打开 Studio One：
   - 选项 → 插件 → VST 插件：确认扫描位置包含 `/Library/Audio/Plug-Ins/VST`；
   - 插件管理器里如有 TAL 被阻止，先"清除阻止列表"，再重新添加/扫描插件。
5. 加载 **TAL-U-NO-LX-V2**（就是带踏板的代理版），插踏板上车即用。

## 确认补丁生效

- 踩住踏板弹和弦 → 松手音继续响；
- 踏板不放开直接弹新和弦 → 新和弦全部发声，最老的保持音被顶掉（Juno-60 HOLD 行为）；
- 快速换和弦（手有重叠）→ 手还按着的音不会被杀掉。

## 卸载

双击 `uninstall.command`，原版 TAL-U-NO-LX-V2 全部格式会放回原位。

## 高级配置

编辑 `/Library/Audio/Plug-Ins/VST/TAL-U-NO-LX-V2.vst/Contents/MacOS/sustainhold.ini`：

```ini
[sustainhold]
max_voices=6      ; 声部池上限（0 = 不限制/不顶替）
spacing_ms10=15   ; 松踏板排空 note-off 的间隔(0.1ms 单位)
log=0             ; 1 = 在 /tmp/sustainhold_log.txt 记录 MIDI 事件
```

## 常见问题

- **插件列表里出现两个 TAL**：说明原版没挪干净，重新运行 install.command。
- **扫描后找不到插件**：插件管理器 → 清除阻止列表 → 重新扫描。
- **提示已损坏/无法验证**：终端运行
  `sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST/TAL-U-NO-LX-V2.vst"`
