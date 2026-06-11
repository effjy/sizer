# Sizer v2.0.1

**Disk Space Analyzer** — a GTK3 desktop application written in C that reports
the files and folders taking the most space on your system, starting from the
`/` directory, with live **percentages** and an interactive **donut diagram**.

Author: **Jean-Francois Lachance-Caumartin**

![Sizer](sizer-256.png)

## Features

- Full-system scan starting at `/` (or any directory)
- Per-item size and **percentage of total**
- In-cell percentage bars for quick visual comparison
- Interactive **donut / pie diagram** with a color legend that matches the list
- Double-click a folder to **drill down**; **Up** and **Root /** buttons to navigate
- Threaded scanning with a **live progress bar** and a **Cancel/Stop** button
  (the UI never freezes during a scan)
- Human-readable sizes (B / KB / MB / GB / TB)
- **About dialog** with the program logo, full feature list and author
- Installs a **global application icon** shown in menus and the window/taskbar
- Optional command-line argument: `sizer /path` scans that path on startup

## Build dependencies

- `gcc` (or any C compiler)
- `make`
- GTK+ 3 development files (`libgtk-3-dev` on Debian/Ubuntu)
- `librsvg2-bin` (`rsvg-convert`) — only needed if you want to regenerate the
  PNG icons from the SVG with `make icons`

## Build

```sh
make
```

Run without installing:

```sh
./sizer            # then click "Scan" to analyze /
./sizer /home      # auto-scan /home on launch
```

## Install

```sh
sudo make install
```

This installs:

| File | Destination |
|------|-------------|
| `sizer` binary | `/usr/bin/sizer` |
| Desktop entry | `/usr/share/applications/sizer.desktop` |
| Scalable icon | `/usr/share/icons/hicolor/scalable/apps/sizer.svg` |
| Raster icons | `/usr/share/icons/hicolor/<size>/apps/sizer.png` |
| Pixmap fallback | `/usr/share/pixmaps/sizer.png` |

After installation, launch **Sizer** from your application menu or run `sizer`.

> **Tip:** To scan the whole filesystem including protected directories,
> run with elevated privileges: `sudo sizer` (or `pkexec sizer`).

## Uninstall

```sh
sudo make uninstall
```

## License

MIT © 2026 Jean-Francois Lachance-Caumartin
