# Picasso Pictures

A lightweight, GPU-accelerated image viewer for Windows with a focus on smooth animations and a clean, minimal UI.

Built with Direct2D, Direct3D 11, and WIC (Windows Imaging Component).

## **Download Windows installer** (latest release) [here](https://github.com/LuisJalabert/Picasso-Pictures/releases/download/v1.51/PicassoPicturesSetup_1.51.exe)

[Bug reports](mailto:luisjalabert@gmail.com)

![Picasso Pictures](Pi7_GIF_CMP(2).gif)

---

## Features

- **Smooth zoom and pan** with animated transitions
- **Fullscreen mode** — double-click or press `F` to enter; the image flies in from its windowed position
- **Slideshow mode** — automatically advances through images in the folder with cross-fade transitions and a blurred background
- **Blurred background** — the current image is used as a softly blurred, cover-scaled background in both windowed and fullscreen modes
- **Trilinear mip-mapped rendering** — GPU-accelerated trilinear filtering via a full D3D11 mip chain; optional high-quality bicubic mode for maximum fidelity on capable hardware
- **Animated GIF support** with correct per-frame delay timings and full frame-composition (disposal methods, transparency)
- **Per-image state memory** — zoom, pan, and rotation are remembered for each file in the session
- **EXIF orientation** — images are automatically rotated to their correct upright orientation
- **File management** — send to Recycle Bin, permanently delete, copy to clipboard, open containing folder, set as desktop wallpaper — all from a right-click menu
- **File association** — set Picasso Pictures as the default viewer for all supported formats from the menu
- **Dark UI** — blurred-glass buttons that adapt to whatever is behind them
- **Single instance** — opening a second image from Explorer reuses the running instance

---

## Supported Formats

`JPG` · `JPEG` · `PNG` · `BMP` · `GIF` (animated) · `TIFF` · `TIF` · `WEBP` · `AVIF` · `JXL`

---

## Usage

**Open an image** by:
- Clicking the 📂 button in the top-left corner
- Pressing `O`
- Dragging and dropping an image file onto the window
- Launching with a file path as a command-line argument (e.g. from "Open with")
- Double-clicking any associated image file in Explorer

Once an image is open, the viewer automatically finds all other supported images in the same folder and lets you browse through them.

---
![Picasso Pictures](example.png)

## Controls

### Mouse

| Action | Result |
|---|---|
| Scroll wheel | Zoom in / out |
| Click and drag | Pan the image |
| Double-click image | Enter fullscreen |
| Double-click outside image | Exit fullscreen |
| Click outside image (fullscreen) | Exit fullscreen |
| Right-click | Context menu |

### Keyboard

| Key | Action |
|---|---|
| `A` / `←` | Previous image |
| `D` / `→` / `Space` | Next image |
| `W` / `↑` | Zoom in |
| `S` / `↓` | Zoom out |
| `Q` | Rotate 90° counter-clockwise |
| `E` | Rotate 90° clockwise |
| `F` | Toggle fullscreen |
| `F5` | Toggle slideshow mode |
| `O` | Open file dialog |
| `Delete` | Send current file to Recycle Bin |
| `Shift + Delete` | Permanently delete current file |
| `Escape` | Exit slideshow → exit fullscreen → quit |

### On-screen Buttons

Buttons appear when your mouse moves near the bottom of the screen (or the top-right corner for Exit). They have a blurred-glass background.

| Button | Action |
|---|---|
| 📂 | Open file |
| ☰ | Menu (keyboard shortcuts, about, HQ filter, file association) |
| `1:1` | Reset zoom to 100% |
| `▶` | Start slideshow |
| `⊕` / `⊖` | Zoom in / out |
| `⭯` / `⭮` | Rotate left / right |
| `⮜` / `⮞` | Previous / next image |
| `❌` | Exit (fullscreen only) |

### Right-click Menu

| Item | Action |
|---|---|
| Copy image | Copy image to clipboard |
| Delete | Send to Recycle Bin (`Shift` for permanent delete) |
| Open containing folder | Opens Explorer with the file selected |
| Properties | Shows file properties dialog |
| Set as wallpaper | Sets the image as the desktop wallpaper |

---

## Slideshow Mode

Press `F5` or click `▶` to enter slideshow mode. The screen fades to black, then the viewer enters fullscreen and begins automatically advancing through all images in the folder.

- Images cross-fade with a blurred, cover-scaled background
- Default interval: **6 seconds**
- Manual navigation with `A`/`D` or the arrow buttons resets the timer
- Press `Escape` or `F5` to exit

---

## Menu Options

Click the **☰** button (top-left) to access:

| Item | Description |
|---|---|
| Keyboard shortcuts | Shows all keyboard shortcuts |
| About | Version and credits |
| High quality filter | Toggles between trilinear (fast, default) and bicubic (slower, maximum quality) rendering. Setting is saved automatically. |
| Associate file types | Registers Picasso Pictures as the default viewer for all supported image formats |

---

## Building

**Requirements:**
- Windows 10 or later
- Visual Studio 2019 or later
- Windows SDK 10.0+

**Dependencies** (all system libraries, no external packages needed):
`d2d1` · `d3d11` · `dxgi` · `dxguid` · `dwrite` · `windowscodecs` · `dwmapi` · `uxtheme` · `shell32` · `comctl32` · `d3dcompiler`

Open `Picasso Pictures.sln` in Visual Studio and build in Release x64.

---

## License

[PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/)

Free to use, share, and modify for non-commercial purposes.
