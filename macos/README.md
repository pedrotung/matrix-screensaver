# Matrix Digital Rain Screensaver for macOS / macOS 版

**[English](#english) | [中文](#中文)**

## English

`MatrixRain.saver` runs on macOS 10.15 or later, on both Apple silicon and Intel Macs.

### Install

1. Download [MatrixRain-macos.zip](../download/MatrixRain-macos.zip) and double-click it to unzip. You get `MatrixRain.saver`.
2. The screensaver is not notarized by Apple, so macOS blocks it at first. Open **Terminal** and run:
   ```
   xattr -dr com.apple.quarantine ~/Downloads/MatrixRain.saver
   ```
   (Change the path if you unzipped it somewhere else.) Or: double-click it, close the warning, open **System Settings → Privacy & Security**, and click **Open Anyway**.
3. Double-click `MatrixRain.saver` and choose to install it.
4. Open **System Settings → Screen Saver** and pick **Matrix Rain**. Click **Options…** to change speed, density, font size, and the "Wake up, Neo" interlude.

### How it differs from the Windows version

macOS closes any screensaver as soon as you touch the mouse or keyboard, and then shows its own lock screen. A screensaver cannot ask for its own password, so:

- The "Wake up, Neo..." / "Follow the white rabbit." Apple II terminal plays as an interlude about 12 seconds after the screensaver starts and then every 3 minutes. You can turn it off in Options.
- To protect your Mac, go to **System Settings → Lock Screen** and set **"Require password after screen saver begins or display is turned off"** to **Immediately**. Your Mac's login password then works as the exit password.

### Build from source

On a Mac with the Xcode command line tools: `sh macos/build.sh`. The GitHub Actions workflow `.github/workflows/macos.yml` builds it on every change and saves the result to `download/MatrixRain-macos.zip`.

## 中文

`MatrixRain.saver` 适用于 macOS 10.15 及以上版本，支持 Apple 芯片和 Intel 芯片的 Mac。

### 安装

1. 下载 [MatrixRain-macos.zip](../download/MatrixRain-macos.zip)，双击解压，得到 `MatrixRain.saver`。
2. 这个屏保没有经过 Apple 公证，macOS 一开始会拦截。打开「终端」，运行：
   ```
   xattr -dr com.apple.quarantine ~/Downloads/MatrixRain.saver
   ```
   （如果解压到了别的位置，请改成对应路径。）也可以先双击它，关掉警告，再打开「系统设置 → 隐私与安全性」，点「仍要打开」。
3. 双击 `MatrixRain.saver`，选择安装。
4. 打开「系统设置 → 屏幕保护程序」，选择 **Matrix Rain**。点「选项…」可以调整速度、密度、字号，以及是否播放 Wake up, Neo 片段。

### 和 Windows 版的区别

macOS 在你碰到鼠标或键盘时会直接关闭屏保，然后显示系统自己的锁屏界面，屏保无法自己弹出密码框。所以：

- Wake up, Neo... / Follow the white rabbit. 的 Apple II 终端画面，改为在屏保启动约 12 秒后播放一次，之后每 3 分钟播放一次，可以在「选项」里关闭。
- 要保护你的 Mac，请到「系统设置 → 锁定屏幕」，把「屏幕保护程序启动或显示器关闭后需要输入密码」设为「立即」。这样 Mac 的登录密码就是退出密码。

### 从源码编译

在装有 Xcode 命令行工具的 Mac 上运行 `sh macos/build.sh`。GitHub Actions（`.github/workflows/macos.yml`）会在每次修改后自动编译，并把结果保存到 `download/MatrixRain-macos.zip`。
