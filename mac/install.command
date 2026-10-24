#!/bin/bash
# sustain-hold 安装脚本 (macOS)
# 用法: 双击本文件, 或在终端里 bash install.command
# 前提: 已安装官方 TAL-U-NO-LX-V2 (https://tal-software.com 免费下载)
set -e
cd "$(dirname "$0")"

VST_DIR="/Library/Audio/Plug-Ins/VST"
VST3_DIR="/Library/Audio/Plug-Ins/VST3"
AU_DIR="/Library/Audio/Plug-Ins/Components"
CLAP_DIR="/Library/Audio/Plug-Ins/CLAP"
AAX_DIR="/Library/Application Support/Avid/Audio/Plug-Ins"
HOLD_DIR="/Library/Application Support/sustainhold"
NAME="TAL-U-NO-LX-V2"

if [ ! -d "./$NAME.vst" ]; then
  echo "错误: 本目录下找不到代理 $NAME.vst"; exit 1
fi

# 1) 原版必须已安装(至少 VST2 版)
if [ ! -d "$VST_DIR/$NAME.vst" ] && [ ! -d "$HOLD_DIR/$NAME.vst" ]; then
  echo "错误: 没找到官方 TAL-U-NO-LX-V2。先去 tal-software.com 下载安装, 再运行本脚本。"
  exit 1
fi

echo "==> 需要管理员密码 (sudo)"
sudo mkdir -p "$HOLD_DIR"

# 2) 把原版所有格式挪进隐藏目录 (DAW 里只会看到代理, 不会选错)
[ -d "$VST_DIR/$NAME.vst" ]  && { sudo rm -rf "$HOLD_DIR/$NAME.vst";        sudo mv "$VST_DIR/$NAME.vst" "$HOLD_DIR/"; }      || true
[ -d "$VST3_DIR/$NAME.vst3" ] && { sudo rm -rf "$HOLD_DIR/$NAME.vst3";      sudo mv "$VST3_DIR/$NAME.vst3" "$HOLD_DIR/"; }    || true
[ -d "$AU_DIR/$NAME.component" ] && { sudo rm -rf "$HOLD_DIR/$NAME.component"; sudo mv "$AU_DIR/$NAME.component" "$HOLD_DIR/"; } || true
[ -d "$CLAP_DIR/$NAME.clap" ] && { sudo rm -rf "$HOLD_DIR/$NAME.clap";      sudo mv "$CLAP_DIR/$NAME.clap" "$HOLD_DIR/"; }    || true
[ -f "$AAX_DIR/$NAME.aax" ]   && { sudo rm -f "$HOLD_DIR/$NAME.aax";        sudo mv "$AAX_DIR/$NAME.aax" "$HOLD_DIR/"; }      || true

# 3) 移除旧代理(如有), 安装新代理
sudo rm -rf "$VST_DIR/$NAME.vst"
sudo cp -R "./$NAME.vst" "$VST_DIR/"

# 4) 写配置
sudo tee "$VST_DIR/$NAME.vst/Contents/MacOS/sustainhold.ini" > /dev/null <<EOF
[sustainhold]
target=$HOLD_DIR/$NAME.vst/Contents/MacOS/$NAME
max_voices=6
spacing_ms10=15
log=0
EOF

# 5) 去掉下载隔离标记 + 重新 ad-hoc 签名 (拷贝可能破坏原签名)
sudo xattr -dr com.apple.quarantine "$VST_DIR/$NAME.vst" 2>/dev/null || true
sudo codesign --force --sign - "$VST_DIR/$NAME.vst"

echo ""
echo "✅ 安装完成。"
echo "   在 Studio One 里: 插件 > 插件管理器 > 清除阻止列表(如有), 然后重新扫描 VST2 文件夹,"
echo "   加载 'TAL-U-NO-LX-V2' 即为带延音踏板的代理版。"
echo "   卸载: 运行同目录 uninstall.command"
