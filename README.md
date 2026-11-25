<p align="center">
  <img src=".github/icon.png" width="96" height="96" alt="Sail icon">
</p>

<h1 align="center">Sail</h1>

<p align="center">
  A modern Chromium browser for BlackBerry 10.<br>
  Native Cascades interface around Chromium for BB10 by sw7ft.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/BlackBerry_10-10.3-222222" alt="BlackBerry 10.3">
  <img src="https://img.shields.io/badge/version-0.2.3.2-2f80ed" alt="Version 0.2.3.2">
  <img src="https://img.shields.io/badge/license-MIT-34d399" alt="MIT licence">
</p>

---

BlackBerry 10's built-in browser stopped keeping up with the web years ago. Sail pairs a current Chromium engine with a full browser interface written in Cascades, so it looks and behaves like a native BB10 app and works with the Passport's keyboard.

## Features

- Tabs, an address bar with suggestions from your history and bookmarks, back, forward, reload and load progress.
- History and bookmarks, both stored on the device.
- Downloads saved to shared storage, with a downloads list.
- Copy selected text and paste into pages through the menu, using the system clipboard.
- Copy or share the current link.
- Settings: search engine (Google, DuckDuckGo, Bing or Brave Search), rendering sharpness, clear history, clear cookies and site data.
- If the engine stops, Sail says so and reopens the page on request instead of closing.

Certificate checks stay on. The engine runs as a single process with one profile, so there are no private tabs.

## Install

Download `org.bq.sail.bar` from the [latest release](https://github.com/iroham/bb10-sail/releases/latest). It is about 60 MB because it carries the whole engine.

The package is not signed by BlackBerry, so it installs through the system installer running as root. You need a BlackBerry 10.3 device with root SSH access.

```bash
mkdir -p app/build && cp ~/Downloads/org.bq.sail.bar app/build/
BB10_HOST=root@192.0.2.10 app/install.sh
```

`install.sh` copies the package to `/var/nt/work` on the device, writes an installer job and prints the result. It calls `grep` and `sleep` from `/var/nt/bin`; change `B=` in the script if your tools live elsewhere. `BB10_HOST` defaults to an SSH alias named `passport`.

## How it works

Sail is two programs. The Cascades app (`app/`) draws every piece of interface. The engine, `sail-engine`, is the Chromium content shell from [chromium-for-bb10](https://github.com/sw7ft/chromium-for-bb10) (`berry-v3` branch). The app starts it through a small launcher (`app/launch.c`), and the engine draws the page into a child window inside the app. The two talk over a private socket pair: the app sends navigation, tab, key and clipboard commands, and the engine reports titles, URLs, progress and tab events back.

| Path | Contents |
| --- | --- |
| `app/src/` | C++: engine control (`engine.cpp`), history and bookmarks (`store.cpp`) |
| `app/assets/` | QML interface, icons and nine-slice shapes |
| `app/launch.c` | Sets up the engine's environment and starts it |
| `app/tools/` | Scripts that render the icons and shapes |
| `engine/` | Patches and setup scripts applied to the Chromium tree |
| `engine/gcc9/` | Builder for the BB10 gcc 9.3 toolchain the engine needs |
| `build-env/` | Dockerfile for the engine build host |

This repository holds the app and the engine integration only. The engine itself is built from sw7ft's repository.

## Building from source

The app needs the BlackBerry 10 Native SDK (QNX toolchain, Qt 4.8, Cascades). The engine needs a Chromium checkout of chromium-for-bb10 `berry-v3`, clang 17 and the BB10 gcc 9.3 sysroot.

1. Build the gcc 9.3 toolchain with `engine/gcc9/` and the build host image from `build-env/Dockerfile`.
2. Prepare the Chromium tree with `engine/setup-build.sh`, apply `engine/fix-qnx800.py` to the sysroot headers, then add the embedding hooks with `engine/sail-embed.py`. All three are safe to run again.
3. Build the engine with ninja and place it with its libraries and data files in `app/build/payload/`.
4. Build and package the app:

```bash
app/build.sh                    # -> app/build/sail, app/build/sail-launch
python3 app/package.py          # -> app/build/org.bq.sail.bar
app/install.sh
```

The app build script targets a specific Docker setup: Docker context `colima-aosp`, image `a11-build`, the clang toolchain in volume `a11-src`, and this repository checked out as `native-browser/` next to a `phase6/` directory holding the SDK. Edit the `docker run` line in [`app/build.sh`](app/build.sh) for your own setup.

## Credits

The browser engine is Chromium for BB10 by [sw7ft](https://github.com/sw7ft), used and distributed with the author's permission ([chromium-for-bb10#7](https://github.com/sw7ft/chromium-for-bb10/issues/7)). Chromium is under the BSD 3-Clause licence. The gcc 9.3 toolchain builder in `engine/gcc9/` comes from [didacticode](https://didacticode.ca/blog/development-tools-for-blackberry-10). Interface icons are rendered from [Tabler Icons](https://tabler.io/icons) (MIT). See [NOTICE.md](NOTICE.md).

## Licence

The Sail app and integration code in this repository are MIT. See [LICENSE](LICENSE). The engine keeps its own licences.
