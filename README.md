# coco-thing

A macOS terminal for the [Pico-Thing](https://github.com/markrvmurray/pico-thing-main), a
real MC6809/HD6309 board with RP2350 companions. It talks to the board's USB serial console (or
to any serial port, or to the [USim](https://github.com/markrvmurray/usim/tree/pico-thing)
emulator's console) and draws exactly what the board's DVI
video console draws: the CoCo 3 / OS-9 control codes, the `$1B` graphics commands, windows,
overlays and fonts.

It can draw exactly the same because it doesn't copy the renderer: it compiles the
**[pico-thing-video](https://github.com/markrvmurray/pico-thing-video) firmware's own sources**
unchanged, as the USim emulator does. The firmware's
text, graphics and keyboard handling run on the host, and coco-thing supplies only the platform
pieces: the serial link, the window, the keyboard and the pasteboard.

## Features

- **Bit-identical rendering.** It uses the video firmware's interpreter and renderer, including its
  USB keyboard cooker, so a key produces the same byte on the board's console and here. The
  cooker uses a UK PC layout, by physical key position.
- **Keeps the link across board resets.** When the board resets, its USB port vanishes and comes back
  under the same name, and coco-thing reconnects by itself. It does the same when USim quits and
  starts again.
- **Any serial port.** Each port has its own baud rate, data bits, parity (none, even or odd),
  stop bits and flow control (none or RTS/CTS). They are remembered per port and applied again on
  every reconnect. The board's own USB console takes no line settings, so they are greyed for it.
- **Loopback port.** Everything typed comes straight back, for trying keys, paste and graphics
  commands without a board.
- **Text area.** Its size is set in Preferences to match the OS-9 `Term` descriptor (`IT.COL` ×
  `IT.ROW`), so full-screen programs wrap where they expect to.
- **Scroll-back, selection and paste.** Text is CP437, and a paste is paced so the board's
  receive buffer is never overrun.
- **Fonts.** Choose the built-in Spleen face, a system font drawn into the 8×16 cell, or a font
  file (a raw 4096-byte face, or PSF1/PSF2). A face can be exported as a `font_8x16.h` for the
  firmware. The chosen face becomes the firmware's default face, so a guest's own fonts still work.
- **Traffic pane.** Debug ▸ Show Traffic shows the last 1 KB each way as a hex dump. It can be saved
  as Python or a C/C++ header, to replay later.

## Requirements

- macOS. The keyboard map is macOS-only, and CMake stops on other platforms.
- Qt 6 with the Widgets and SerialPort modules, for example `brew install qt`.
- A checkout of **pico-thing-video**, the video firmware, whose sources are compiled in:
  https://github.com/markrvmurray/pico-thing-video. By default the build looks for it at
  `~/GitHub/PICO/pico-thing-video`; set `PICOVIDEO` to use another path.
  Snapshots of the two are tagged in step: build coco-thing `SNAPSHOT-YYYYMMDD` against
  pico-thing-video's tag of the same name.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_PREFIX_PATH=$(brew --prefix qt) \
    -DPICOVIDEO=$HOME/GitHub/PICO/pico-thing-video
cmake --build build -j
ctest --test-dir build
```

The app is `build/coco-thing.app`. To install it, copy it into `~/Applications`. The linker signs
only the binary, so re-sign the bundle after copying:

```sh
ditto build/coco-thing.app ~/Applications/coco-thing.app
codesign --force --sign - ~/Applications/coco-thing.app
```

## Connecting

- **The board:** Connection ▸ Connect… (⌘K) and choose its `cu.usbmodem…` port. Or start it with
  the port name: `open build/coco-thing.app --args cu.usbmodem1101`.
- **USim** (`usim09pt`, from [usim's `pico-thing`
  branch](https://github.com/markrvmurray/usim/blob/pico-thing/README-pico-thing.md)): start it
  with its console on a pty, held at reset by `--prompt`:

  ```sh
  usim09pt --console-pty=/tmp/usim-console --prompt -d boot.dsk firmware.srec
  ```

  Type `/tmp/usim-console` into Connect, then `run` at the `usim09pt>` prompt. The path is a
  symlink to a new pty for each run, and coco-thing follows it from one run to the next.
- **Another serial port:** choose it in Connect and set its line settings there. Preferences ▸
  Connection changes the settings of the port in use.

The last port is reconnected at start-up (this can be turned off in Preferences). Menu shortcuts
use Command, because every Control chord goes to the guest. The Option key is Alt and sets bit 7
on printable characters, and the Help key is Insert.

## Known gaps

- The protocol's read-back channel (screen size, pixel read-back, mouse) needs the board's
  register window, which a serial link lacks. The firmware's VT100 queries (`ESC [ 6n`,
  `ESC [ 18 t`) do work, so a guest can still learn the text area.
- The Tone command (`$1B $27`) is not played yet.
- The firmware tree is not pinned. Any commit of it can be built, so use the matching snapshot tag.
