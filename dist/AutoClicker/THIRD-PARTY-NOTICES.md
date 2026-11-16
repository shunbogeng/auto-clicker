# 第三方声明

本程序的安装包/分发物中包含以下第三方组件，各自遵循其原始许可证。

| 组件 | 许可证 | 在本程序中的用法 | 备注 |
| --- | --- | --- | --- |
| Qt 6（Core / Gui / Widgets / Network） | LGPL v3 | 动态链接 | 安装包内已附带 Qt 的动态库与其许可证文本；如需替换 Qt 版本，可直接替换同名动态库后重新运行 |
| OpenCV（core / imgproc / imgcodecs） | Apache License 2.0 | 动态链接 | 仅使用模板匹配与图片编解码 |
| X11 / XTest / XComposite / XRecord（仅 Linux） | MIT / X11 许可 | 动态链接系统库 | 由发行版提供 |
| Windows / macOS 系统框架 | 系统自带 | 动态链接 | SendInput、User32、Cocoa、ApplicationServices 等 |

Qt 为 LGPL v3：本程序以动态链接方式使用 Qt，未修改 Qt 源码，用户可以用自备的 Qt 动态库替换
（Windows 下替换安装目录中的 `Qt6*.dll`，Linux 下替换 AppImage/tar 包中的 `libQt6*.so*`，
macOS 下替换 `.app/Contents/Frameworks/` 中的同名 framework），以行使 LGPL 赋予的替换权利。

Qt 源码与许可证全文：<https://www.qt.io/>

OpenCV 许可证全文：<https://github.com/opencv/opencv/blob/4.x/LICENSE>

本程序自身代码采用 MIT 许可证，见仓库根目录 `LICENSE`。
