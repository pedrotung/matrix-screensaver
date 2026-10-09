# Matrix Digital Rain Screensaver (Windows)

`MatrixRain.scr` is a standalone 64-bit Windows screensaver for Windows 10 / 11. No extra runtime or fonts are needed.

## Install

1. Put `MatrixRain.scr` in a folder you will keep, for example `C:\Windows\System32` (needs administrator rights) or `Documents\Screensavers`.
2. Right-click `MatrixRain.scr` and choose **Install**. Windows opens *Screen Saver Settings* with "Matrix 数字雨" selected.
3. The first time, if Windows shows "Windows protected your PC", click **More info**, then **Run anyway**. This appears because the program is not code-signed.

## Settings

Click **Settings** in *Screen Saver Settings*. The window is in Chinese:

- 速度 (Speed): 0.3x to 2.5x
- 密度 (Density): 30% to 100%
- 字号 (Font size): 12 to 32 px, scaled with your display scaling
- 退出密码 / 确认密码 (Exit password / Confirm password): type the same password in both boxes, then click 确定 (OK). 清除密码 (Clear password) removes it; 取消 is Cancel.

Settings are stored in the registry at `HKEY_CURRENT_USER\Software\MatrixRainSaver`. Only a SHA-256 digest of the password is stored.

## Exiting

- No password: moving the mouse or pressing any key closes the screensaver.
- With a password: the screen switches to an Apple II style green terminal, types `Wake up, Neo...` and `Follow the white rabbit.`, then shows `]PASSWORD:`. Type the password and press Enter. A wrong password shows `?ACCESS DENIED`. Esc clears your input. After 30 seconds without input it goes back to the digital rain.
- While a password is set, the Win key, Alt+Tab, Alt+F4 and Ctrl+Esc are blocked.

## Security note

The screensaver password cannot block Ctrl+Alt+Del or a forced shutdown. To really protect your computer, also tick **"On resume, display logon screen"** in *Screen Saver Settings*.

## Build from source

On Ubuntu, run `apt install mingw-w64`, then `sh build.sh`. The glyphs are pre-rendered into `glyphs.h`. To regenerate them, download VT323.ttf from Google Fonts and run `python3 tools/gen_glyphs.py <ipag.ttf> <VT323.ttf> glyphs.h`.

Fonts: the rain glyphs come from IPAGothic (IPA Font License); the terminal glyphs come from VT323 (SIL Open Font License).
