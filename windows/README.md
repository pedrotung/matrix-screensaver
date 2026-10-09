# Matrix 数字雨屏保（Windows 版）

English version: [README-en.md](README-en.md)

`MatrixRain.scr` 是一个独立的 64 位 Windows 屏保，Windows 10 / 11 均可用，不需要另外安装运行库或字体。

## 安装

1. 把 `MatrixRain.scr` 放到一个固定的位置，比如 `C:\Windows\System32`（需要管理员权限），或者 `文档\屏保` 这类不会被删掉的文件夹。
2. 右键 `MatrixRain.scr`，选择「安装」。Windows 会打开「屏幕保护程序设置」，并且已经选中「Matrix 数字雨」。
3. 第一次运行时，如果出现「Windows 已保护你的电脑」，点「更多信息」，再点「仍要运行」。出现这个提示是因为程序没有数字签名。

## 设置

在「屏幕保护程序设置」里点「设置」按钮，可以调整：

- 速度：0.3x 到 2.5x
- 密度：30% 到 100%
- 字号：12 到 32 px（会随系统缩放比例放大）
- 退出密码：两栏输入一致后点「确定」保存。点「清除密码」可以取消密码。

设置保存在注册表 `HKEY_CURRENT_USER\Software\MatrixRainSaver`，密码只保存 SHA-256 摘要。

## 退出

- 没设密码：移动鼠标或按任意键，屏保直接退出。
- 设了密码：屏幕切换为 Apple II 风格的绿色终端，依次打出 `Wake up, Neo...` 和 `Follow the white rabbit.`，然后出现 `]PASSWORD:`。输入密码后按回车退出。输错会显示 `?ACCESS DENIED`，按 Esc 清空已输入的内容，30 秒没有输入则回到数字雨。
- 设了密码时，屏保运行期间会屏蔽 Win 键、Alt+Tab、Alt+F4、Ctrl+Esc。

## 安全提醒

屏保密码无法拦截 Ctrl+Alt+Del，也无法阻止别人强制关机。如果要真正保护电脑，请在「屏幕保护程序设置」里同时勾选「在恢复时显示登录屏幕」。

## 从源码编译

在 Ubuntu 上执行 `apt install mingw-w64`，然后运行 `sh build.sh`。字形已经预先做成位图放在 `glyphs.h` 里。若要重新生成，先从 Google Fonts 下载 VT323.ttf，再运行 `python3 tools/gen_glyphs.py <ipag.ttf> <VT323.ttf> ../core/glyphs.h`。

字体：数字雨字符取自 IPAGothic（IPA Font License），终端字符取自 VT323（SIL Open Font License）。
