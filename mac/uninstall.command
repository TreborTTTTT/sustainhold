#!/bin/bash
# sustain-hold 卸载脚本 (macOS): 删代理, 把原版放回原位
set -e
cd "$(dirname "$0")"

VST_DIR="/Library/Audio/Plug-Ins/VST"
VST3_DIR="/Library/Audio/Plug-Ins/VST3"
AU_DIR="/Library/Audio/Plug-Ins/Components"
CLAP_DIR="/Library/Audio/Plug-Ins/CLAP"
AAX_DIR="/Library/Application Support/Avid/Audio/Plug-Ins"
HOLD_DIR="/Library/Application Support/sustainhold"
NAME="TAL-U-NO-LX-V2"

echo "==> 需要管理员密码 (sudo)"
sudo rm -rf "$VST_DIR/$NAME.vst"

# 放回原版各格式
[ -d "$HOLD_DIR/$NAME.vst" ]       && sudo mv "$HOLD_DIR/$NAME.vst" "$VST_DIR/"        || true
[ -d "$HOLD_DIR/$NAME.vst3" ]      && sudo mv "$HOLD_DIR/$NAME.vst3" "$VST3_DIR/"      || true
[ -d "$HOLD_DIR/$NAME.component" ] && sudo mv "$HOLD_DIR/$NAME.component" "$AU_DIR/"   || true
[ -d "$HOLD_DIR/$NAME.clap" ]      && sudo mv "$HOLD_DIR/$NAME.clap" "$CLAP_DIR/"      || true
[ -f "$HOLD_DIR/$NAME.aax" ]       && sudo mv "$HOLD_DIR/$NAME.aax" "$AAX_DIR/"        || true
sudo rmdir "$HOLD_DIR" 2>/dev/null || true

echo "✅ 已卸载代理, 原版 TAL-U-NO-LX-V2 已恢复原位。"
echo "   在 Studio One 里重新扫描插件即可。"
