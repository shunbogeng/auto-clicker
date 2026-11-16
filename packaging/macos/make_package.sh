#!/usr/bin/env bash
# 生成 macOS .dmg（未签名，首次打开需在「隐私与安全性」里放行）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
OUT_DIR="${OUT_DIR:-$ROOT/dist}"
APP="$BUILD_DIR/AutoClicker.app"

[ -d "$APP" ] || { echo "找不到 $APP，先构建 Release 版"; exit 1; }
mkdir -p "$OUT_DIR"

MACDEPLOYQT="${MACDEPLOYQT:-$(command -v macdeployqt)}"
[ -n "$MACDEPLOYQT" ] || { echo "没找到 macdeployqt"; exit 1; }

cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE" 2>/dev/null || true
cp "$ROOT/docs/使用说明.md" "$APP/Contents/Resources/" 2>/dev/null || true
cp "$ROOT/packaging/THIRD-PARTY-NOTICES.md" "$APP/Contents/Resources/" 2>/dev/null || true

"$MACDEPLOYQT" "$APP" -dmg -always-overwrite
mv "$BUILD_DIR/AutoClicker.dmg" "$OUT_DIR/AutoClicker-macos.dmg" 2>/dev/null || true

# 未签名包会把 quarantine 标记带过去，给出提示
cat <<'EOF'
已生成 dist/AutoClicker-macos.dmg（未签名/未公证）。
首次打开：右键 → 打开；或在终端执行：
  xattr -dr com.apple.quarantine /Applications/AutoClicker.app
然后再到 系统设置 → 隐私与安全性 授予「辅助功能」「屏幕录制」。
EOF
