# Matrix 数字雨屏保 · Matrix Digital Rain Screensaver

**[English](#english) | [中文](#中文)**

**⬇ Download / 下载：[Releases 页面 / Releases page](https://github.com/pedrotung/matrix-screensaver/releases/latest)** → `MatrixRain-windows.zip`

---

## English

A screensaver inspired by *The Matrix*: green katakana and digits fall down the screen with fading trails and a glowing leading character. You can set an exit password. When the screensaver wakes up, the screen turns into an Apple II style green phosphor terminal that types out `Wake up, Neo...` and `Follow the white rabbit.`, and it only closes after the correct password is entered.

This is my first project built with AI-assisted programming. It is free and open source. Enjoy!

### Download and install (Windows 10 / 11)

1. Go to the [latest release](https://github.com/pedrotung/matrix-screensaver/releases/latest) and download `MatrixRain-windows.zip`. (The same file is also in the [download](download/) folder of this repository.)
2. Unzip it and move `MatrixRain.scr` to a folder you will keep, for example `Documents\Screensavers`.
3. Right-click `MatrixRain.scr` and choose **Install**. Windows opens *Screen Saver Settings* with "Matrix 数字雨" selected.
4. The program is not code-signed. If Windows shows "Windows protected your PC", click **More info**, then **Run anyway**.

No extra runtime or fonts are needed. The file is about 650 KB.

### Settings

In *Screen Saver Settings*, click **Settings** to adjust:

| Option | Range |
| --- | --- |
| 速度 Speed | 0.3x to 2.5x |
| 密度 Density | 30% to 100% |
| 字号 Font size | 12 to 32 px (scaled with your display scaling) |
| 退出密码 Exit password | Type it twice, then click **确定 (OK)**. **清除密码 (Clear password)** removes it. |

The settings window is in Chinese. The labels above show what each control does.

### Exiting the screensaver

- **No password set:** move the mouse or press any key and it closes, like a normal screensaver.
- **Password set:** the green terminal appears and shows `]PASSWORD:`. Type the password and press **Enter**. A wrong password shows `?ACCESS DENIED`. **Esc** clears what you typed. After 30 seconds without input, it returns to the digital rain.
- While a password is set, the Win key, Alt+Tab, Alt+F4 and Ctrl+Esc are blocked.

### Security note

The screensaver password cannot block Ctrl+Alt+Del or a forced shutdown. To really protect your computer, also tick **"On resume, display logon screen"** in *Screen Saver Settings*.

### Web version

[web/index.html](web/index.html) is a single-file browser version with the same effects, adjustable speed, density and font size, and an exit password. Download it and open it in any browser. It is a demo only: closing the tab exits it, so it cannot lock your computer.

### Build from source

The Windows version is C++ / Win32, cross-compiled with MinGW-w64. On Ubuntu: `apt install mingw-w64`, then `sh windows/build.sh`. See [windows/README.md](windows/README.md).

### License

MIT, see [LICENSE](LICENSE).

---

## 中文

《黑客帝国》风格的数字雨屏幕保护程序：绿色片假名和数字从屏幕上方落下，带拖尾渐隐和发光的首字。可以设置退出密码，唤醒时画面切换为 Apple II 风格的绿色荧光终端，逐字打出 `Wake up, Neo...` 和 `Follow the white rabbit.`，输入正确密码才能退出。

这是我用 AI 编程完成的第一个作品，免费开源，欢迎试用。

### 下载试用（Windows 10 / 11）

1. 到 [Releases 页面](https://github.com/pedrotung/matrix-screensaver/releases/latest) 下载 `MatrixRain-windows.zip`（仓库的 [download](download/) 文件夹里也有同一个文件），然后解压。
2. 把 `MatrixRain.scr` 放到一个固定的文件夹，右键选择「安装」。
3. 在「屏幕保护程序设置」里点「设置」，可以调整速度、密度、字号和退出密码。

程序没有数字签名，第一次运行如果出现「Windows 已保护你的电脑」，点「更多信息」再点「仍要运行」。详细说明见 [windows/README.md](windows/README.md)。

### 网页版

[web/index.html](web/index.html) 是单文件网页版，下载后用浏览器打开即可，功能和 Windows 版一样：可调速度、密度、字号，可设置退出密码。网页版只是效果演示，关闭标签页就能退出，不能真正锁住电脑。

### 安全提醒

屏保密码挡不住 Ctrl+Alt+Del。要真正防止别人使用电脑，请在「屏幕保护程序设置」里勾选「在恢复时显示登录屏幕」。

### 目录

| 路径 | 内容 |
| --- | --- |
| `web/index.html` | 网页版（HTML + Canvas） |
| `windows/` | Windows 屏保源码（C++ / Win32，MinGW-w64 交叉编译） |
| `download/` | 编译好的 Windows 屏保压缩包 |

### 许可证

MIT，见 [LICENSE](LICENSE)。
