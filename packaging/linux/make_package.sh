#!/usr/bin/env bash
# 生成 Linux 分发物：优先 AppImage，失败则退回 tar.gz（内含二进制 + 依赖检查结果）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
OUT_DIR="${OUT_DIR:-$ROOT/dist}"
APP="$BUILD_DIR/AutoClicker"

[ -x "$APP" ] || { echo "找不到 $APP，先构建 Release 版"; exit 1; }
mkdir -p "$OUT_DIR"

VERSION=$(sed -n 's/^project(AutoClicker VERSION \(.*\) LANGUAGES.*/\1/p' "$ROOT/CMakeLists.txt" | head -1)
VERSION="${VERSION:-2.0.0}"

stage="$OUT_DIR/AutoClicker"
rm -rf "$stage"
mkdir -p "$stage/usr/bin" "$stage/usr/share/applications" "$stage/usr/share/icons/hicolor/256x256/apps"

cp "$APP" "$stage/usr/bin/AutoClicker"
cp "$ROOT/LICENSE" "$stage/"
cp "$ROOT/docs/使用说明.md" "$stage/"
cp "$ROOT/packaging/THIRD-PARTY-NOTICES.md" "$stage/"
cp "$ROOT/assets/icon.png" "$stage/usr/share/icons/hicolor/256x256/apps/AutoClicker.png"

cat > "$stage/AutoClicker.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=连点器 AutoClicker
Comment=鼠标连点 / 找图连点 / 监视窗口 / 录制回放
Exec=AutoClicker
Icon=AutoClicker
Terminal=false
Categories=Utility;
EOF
cp "$stage/AutoClicker.desktop" "$stage/usr/share/applications/"

# AppImage
export APPIMAGE_EXTRACT_AND_RUN=1
if [ "${USE_LINUXDEPLOY:-0}" = "1" ] || command -v linuxdeploy >/dev/null 2>&1; then
  TOOLS="$OUT_DIR/.tools"
  mkdir -p "$TOOLS"
  if [ ! -x "$TOOLS/linuxdeploy" ]; then
    ( cd "$TOOLS" && curl -L -o linuxdeploy \
      https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage && chmod +x linuxdeploy )
  fi
  if [ ! -x "$TOOLS/linuxdeploy-plugin-qt" ]; then
    ( cd "$TOOLS" && curl -L -o linuxdeploy-plugin-qt \
      https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage && chmod +x linuxdeploy-plugin-qt )
  fi
  chmod +x "$TOOLS/linuxdeploy" "$TOOLS/linuxdeploy-plugin-qt" 2>/dev/null || true
  export PATH="$TOOLS:$PATH"
  export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
  cd "$OUT_DIR"
  if ARCH=x86_64 linuxdeploy --appdir "$stage" --plugin qt --output appimage; then
    mv ./*.AppImage "$OUT_DIR/AutoClicker-linux-x86_64.AppImage" 2>/dev/null || true
    echo "AppImage：$OUT_DIR/AutoClicker-linux-x86_64.AppImage"
    exit 0
  fi
  echo "AppImage 打包失败，退回 tar.gz"
  cd "$OUT_DIR"
else
  cd "$OUT_DIR"
  tar czf "AutoClicker-linux-x86_64-$VERSION.tar.gz" AutoClicker
  echo "tar.gz：$OUT_DIR/AutoClicker-linux-x86_64-$VERSION.tar.gz"
fi
