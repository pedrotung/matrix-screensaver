# Matrix 数字雨屏保

《黑客帝国》风格的数字雨屏幕保护程序：绿色片假名和数字从屏幕上方落下，带拖尾渐隐和发光的首字。可以设置退出密码，唤醒时画面切换为 Apple II 风格的绿色荧光终端，逐字打出 `Wake up, Neo...` 和 `Follow the white rabbit.`，输入正确密码才能退出。

这是我用 AI 编程完成的第一个作品，免费开源，欢迎试用。

A Matrix "digital rain" screensaver for Windows, plus a browser version. Waking it plays the "Wake up, Neo..." sequence on an Apple II style green terminal and asks for an exit password.

## 下载试用（Windows 10 / 11）

1. 下载 [download/MatrixRain-windows.zip](download/MatrixRain-windows.zip) 并解压。
2. 把 `MatrixRain.scr` 放到一个固定的文件夹，右键选择「安装」。
3. 在「屏幕保护程序设置」里点「设置」，可以调整速度、密度、字号和退出密码。

程序没有数字签名，第一次运行如果出现「Windows 已保护你的电脑」，点「更多信息」再点「仍要运行」。详细说明见 [windows/README.md](windows/README.md)。

## 网页版

[web/index.html](web/index.html) 是单文件网页版，下载后用浏览器打开即可，功能和 Windows 版一样：可调速度、密度、字号，可设置退出密码。网页版只是效果演示，关闭标签页就能退出，不能真正锁住电脑。

## 安全提醒

屏保密码挡不住 Ctrl+Alt+Del。要真正防止别人使用电脑，请在「屏幕保护程序设置」里勾选「在恢复时显示登录屏幕」。

## 目录

| 路径 | 内容 |
| --- | --- |
| `web/index.html` | 网页版（HTML + Canvas） |
| `windows/` | Windows 屏保源码（C++ / Win32，MinGW-w64 交叉编译） |
| `download/` | 编译好的 Windows 屏保压缩包 |

## 许可证

MIT，见 [LICENSE](LICENSE)。
