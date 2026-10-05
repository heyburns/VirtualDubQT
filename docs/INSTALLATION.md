# Install and start using VirtualDubQT

You do not need to write code to use VirtualDubQT or follow the build guide.
You will need to download files and run a few terminal commands. The steps below
explain which commands to use, what they do, and what success looks like.

Choose one route:

- **Run the app:** [download a package](#choose-a-package). The AppImage includes
  the main dependencies and is the simplest starting point.
- **Build the app yourself:** follow [Building VirtualDubQT](BUILDING.md).
- **Already installed, but AviSynth Fast Recompress fails:** see
  [FFmpeg and AviSynth](#ffmpeg-and-avisynth).

## Before running commands

Open your desktop's Terminal application. These examples use Bash, the usual
terminal shell on Ubuntu and Fedora. If you use another shell, type `bash` first.

- Use only the instructions for your chosen package or distro. A *distro* is
  your Linux distribution, such as Ubuntu or Fedora.
- Copy a whole command, including continued lines ending in `\`. That character
  joins lines; it is not a separate command. Run commands in the listed order.
- `sudo` asks permission to install system files. Type your login password when
  asked; it is normal for the terminal to show no characters while you type it.
- If a command fails, stop at that step and keep the error text. Later commands
  usually cannot fix an earlier failure. See [common problems](#common-problems).

## Choose a package

Open the [0.1.2 release](https://github.com/heyburns/VirtualDubQT/releases/tag/v0.1.2)
and expand **Assets** if necessary. Download **one** of the files below. GitHub's
“Source code” ZIP and tar.gz files are for building, not ready-to-run programs.

| Download | Choose this when | What it supplies |
| --- | --- | --- |
| `VirtualDubQT-0.1.2-x86_64.AppImage` | You want the bundled version | Qt, FFmpeg/ffprobe with AviSynth support, and AviSynth+ |
| `virtualdubqt_0.1.2_amd64.deb` | You use Ubuntu 24.04 or matching packages | AviSynth+; your package manager installs Qt and FFmpeg |
| `virtualdubqt-0.1.2-1.x86_64.rpm` | You use Fedora 43 or matching packages | AviSynth+; your package manager installs Qt and FFmpeg |

All three are for 64-bit Intel/AMD computers. `amd64` and `x86_64` mean the same
architecture here; neither is an ARM build. If you are unsure of your system,
these commands show your distro, processor architecture, and core C library:

```bash
cat /etc/os-release
uname -m
getconf GNU_LIBC_VERSION
```

Look for your distro's `PRETTY_NAME`, `x86_64`, and a result such as `glibc 2.39`.
The AppImage needs **glibc 2.38 or newer**, plus normal desktop graphics, font,
and audio libraries. glibc is part of the operating system, not an app setting.

The project targets reasonably current Linux desktops, roughly the last couple
of years. Package checks cover Ubuntu 24.04 (AppImage and DEB) and Fedora 43
(RPM). Other distros and physical display/audio/capture hardware have not been
verified by those checks. A newer distro may use different library versions;
that does not automatically make its packages interchangeable.

### AppImage

In your file manager, open the folder containing the downloaded AppImage and
choose **Open in Terminal**. Alternatively, use `cd "$HOME/Downloads"` if you
saved it in Downloads. Run:

```bash
chmod +x VirtualDubQT-0.1.2-x86_64.AppImage
./VirtualDubQT-0.1.2-x86_64.AppImage
```

`chmod +x` marks the file as a program you can run. `./` means “the file in this
folder.” The editor window should open. Keep the AppImage wherever you want to
run it from; it does not install a separate copy of the application.

If it reports missing FUSE support, this command runs it without FUSE:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./VirtualDubQT-0.1.2-x86_64.AppImage
```

Next, try [your first open and export](#your-first-open-and-export). There is no
need to build FFmpeg or AviSynth for the bundled setup.

### DEB on Ubuntu 24.04

Open a terminal in the folder containing the downloaded `.deb`, then run:

```bash
sudo apt install ./virtualdubqt_0.1.2_amd64.deb
VirtualDubQt
```

APT installs the app and its required distro packages. Afterward, you can also
open **VirtualDubQT** from your desktop's application menu. Next, try
[your first open and export](#your-first-open-and-export).

### RPM on Fedora 43

Open a terminal in the folder containing the downloaded `.rpm`, then run:

```bash
sudo dnf install ./virtualdubqt-0.1.2-1.x86_64.rpm
VirtualDubQt
```

DNF installs the app and its required distro packages. Afterward, you can also
open **VirtualDubQT** from your desktop's application menu. Next, try
[your first open and export](#your-first-open-and-export).

### Check a download

To check that the file downloaded correctly, also download `SHA256SUMS` from
the **same release**, place it beside the package, and run:

```bash
sha256sum --check --ignore-missing SHA256SUMS
```

Expect your downloaded filename followed by `OK`. You do not need all three
packages; `--ignore-missing` skips the ones you did not download. A `FAILED`
result means the file does not match: download it again before using it.

## Your first open and export

You can open an ordinary video with **File → Open video file…** (`Ctrl+O`).
To try AviSynth without finding a sample video or installing extra plugins,
run this command. It creates a plain-text script in a folder in your home directory:

```bash
mkdir -p "$HOME/VirtualDubQT-examples"
printf '%s\n' 'BlankClip(length=48, width=320, height=240, fps=24, pixel_type="RGB24", audio_rate=0, color=$204080)' \
    > "$HOME/VirtualDubQT-examples/blank.avs"
```

1. Open `blank.avs` from the `VirtualDubQT-examples` folder using **File → Open
   video file…**. Expect a solid blue image: it is a two-second, silent clip.
2. Select **Video → Full processing mode**. This is a useful starting mode for
   applying filters and making a new video file.
3. Select **Video → Compression…**, choose **FFV1**, and confirm. FFV1 is a
   lossless encoder—a way to save the video without throwing away image detail.
4. Select **File → Save video…** (`F7`). Set **Files of type** to **Matroska
   (*.mkv)** and use **Browse…** to choose a destination such as `blank-test.mkv`.
   Save it, then reopen that output to check the result.

**Save Project** saves your editing session so you can resume it later.
**Save video…** makes the finished video file.

If this example works, move on to your own media and scripts. If a script needs
an extra source reader or filter, follow that plugin's Linux installation
instructions. AviSynth scripts are text files; they are opened by the app, not
typed as terminal commands. A plugin's instructions may use a line such as
`LoadPlugin("/full/path/to/plugin.so")` inside the `.avs` file. Replace that
example path with the actual Linux plugin file. Windows plugin DLLs do not work.

## FFmpeg and AviSynth

FFmpeg reads and writes media. `ffmpeg` is its conversion command, `ffprobe`
reports information about media, and its *libraries* are supporting files loaded
by the application. Qt supplies the application's windowing and controls.

VirtualDubQT opens `.avs` scripts directly through AviSynth+. **Fast Recompress
of an `.avs` script also asks the FFmpeg command to read that script.** This
mode skips VirtualDubQT's RGB/filter processing, and requires FFmpeg built with
`--enable-avisynth`. A distro's FFmpeg may omit that feature. This is why a
script can open successfully but fail in Fast Recompress.

- **AppImage:** includes matching FFmpeg tools and AviSynth+. Extra script
  plugins are still yours to install.
- **Native package or source build:** can use the distro's FFmpeg for ordinary
  work. If you need AviSynth Fast Recompress, run the
  [FFmpeg/AviSynth check](BUILDING.md#check-ffmpeg-and-avisynth-together). If it
  fails, use the AppImage or follow the [custom FFmpeg recipe](BUILDING.md#optional-build-custom-ffmpeg).

Replacing the `ffmpeg` command does not replace a native package's library
dependencies. If a package cannot find the library version it needs, choose a
matching package, use the AppImage, or [build the app for your system](BUILDING.md).
Renaming library files or forcing an install will not make versions compatible.

VapourSynth `.vpy` scripts need the `vapoursynth` input module in both the FFmpeg
libraries and commands. The custom FFmpeg recipe does not enable it.

## Common problems

| What you see | What to do next |
| --- | --- |
| Downloaded filename: “No such file or directory” | Open a terminal in the download folder. Run `pwd` to show the current folder and `ls` to list its files; use the filename that is actually there. |
| AppImage: “Permission denied” | Run its `chmod +x` command above, then launch it again. |
| AppImage: FUSE or `libfuse.so.2` error | Use the `APPIMAGE_EXTRACT_AND_RUN=1` command above. |
| AppImage: `GLIBC_2.38 not found` | Your system's glibc is below this package's requirement. Use an appropriate system or a source build with compatible dependencies. |
| APT/DNF: dependencies cannot be satisfied | Check that the package matches your distro/version. Use the AppImage or a source build if it does not. |
| Missing encoder, or “Unknown encoder” | For native installs, run `ffmpeg -encoders` to see what your FFmpeg supplies. Select an available encoder such as FFV1, or use a build containing the encoder you want. |
| Script: missing function or plugin | Try `blank.avs` first. If it works, check the failing script's plugin requirements and file paths. |
| Script opens, but Fast Recompress fails | Run the linked FFmpeg/AviSynth check. Use **Full processing mode** for the initial test export. |

## Getting help and reporting bugs

Run the relevant check above and keep its output. It is fine to be unsure whether
the cause is your setup or the app; say what you tried. Reports that the documented
steps do not work, or leave something unexplained, are welcome too.

Use the [issue tracker](https://github.com/heyburns/VirtualDubQT/issues) and include:

- Distro/version (`cat /etc/os-release`), app version, and AppImage/DEB/RPM/source build.
- The operation and processing mode, what you expected, and what happened.
- The exact error and the relevant command output as text.
- For script problems, the smallest failing script and any required plugin versions.

For a build failure, include the first actual error and the preceding lines, not
just the final “build failed” message. For a native install, include
`command -v ffmpeg ffprobe` and `ffmpeg -version`. For the AppImage, identify the
AppImage version; system FFmpeg does not describe its bundled tools.

Users maintain their own distro packages, custom FFmpeg builds, and third-party
plugins. The project documents a working setup and accepts reproducible app and
documentation problems; general Linux administration and arbitrary plugin
debugging belong with the relevant project. Programming knowledge is not a
prerequisite for asking a useful question or reporting a bug.
