# TOD Timer — OBS Studio Plugin

A countdown timer source for OBS Studio that counts down to a configurable
**time of day** (wall-clock target). When the target time is reached the
timer stops, hides, or simply holds at zero — your choice.

---

## Installation

1. Download the latest release for your platform from the
   [Releases](../../releases) page.
2. Copy the plugin into your OBS plugins folder:
   - **macOS:** `~/Library/Application Support/obs-studio/plugins/`
   - **Windows:** `%APPDATA%\obs-studio\plugins\`
   - **Linux:** `~/.config/obs-studio/plugins/`
3. Restart OBS Studio.

---

## Adding the Source

1. In the **Sources** panel click **+** and choose **TOD Countdown Timer**.
2. Give it a name and click **OK**.
3. In the Properties panel click **Configure…** to open the settings dialog.

---

## Settings

### Target Time

Enter the time of day to count down to in `h:mm:ss.t` format.
Partial input is accepted — `1:30` means 01:30:00.0.
Select **AM** or **PM** from the picker to the right.

> If the target time is earlier than the current time, the timer
> automatically wraps to the **next occurrence** of that time (i.e. it
> counts down to tomorrow's occurrence).

---

### Display Format

Controls how each time unit is displayed. Four blue pill dropdowns
represent **hours : minutes : seconds . tenths**.

Each unit (except tenths) has five options:

| Option | Example | Description |
|--------|---------|-------------|
| `h` / `m` / `s` | `7:05:03` | Always shown, no leading zero |
| `hh` / `mm` / `ss` | `07:05:03` | Always shown, with leading zero |
| `─h─` *(strikethrough)* | `5:03` | Show only when value > 0, no leading zero |
| `─hh─` *(strikethrough)* | `05:03` | Show only when value > 0, with leading zero |
| `--` | — | Hide; roll the value into the next lower unit |

The **tenths** dropdown has two options: `t` (always shown) or `--` (hidden).

**Roll-down example:** setting hours to `--` causes hours to roll into
minutes, so a 90-minute countdown displays as `90:00` rather than `1:30:00`.

**Leading-zero propagation:** once any higher-order non-zero unit has been
shown, all lower-order units always display with a leading zero — so
`57:02` never collapses to `57:2`.

---

### Appearance

| Control | Description |
|---------|-------------|
| **Font** | Font family, style (Regular, Bold, Heavy, Semibold, etc.), and point size |
| **Color** | Text colour (includes opacity/alpha) |
| **Shadow** | Drop shadow |
| **Stroke** | Outline stroke |

---

### Behaviour

| Option | Default | Description |
|--------|:-------:|-------------|
| Start when source becomes active | ✓ | Timer starts automatically when the scene containing this source goes live |
| Stop when source is not active | ✓ | Timer pauses when the scene is not active |
| Stop when countdown reaches zero | ✓ | Freeze the display at `00:00:00.0` when the target time is reached |
| Hide when countdown reaches zero | - | Clear the text entirely so the source renders at 0×0 when the target time is reached |

---

## Supported Platforms

| Platform | OBS Version |
|----------|-------------|
| macOS 13+ | OBS 31.x |
| Windows 10/11 | OBS 31.x |
| Ubuntu 24.04 | OBS 31.x |

---

## Version History

### 1.0.0 — 2026-05-08
Initial release.

- Countdown to a configurable wall-clock time of day
- Wraps automatically to the next occurrence when the target has already passed
- Flexible per-unit display format (always / if non-zero / hide+roll-down) with leading-zero propagation
- Font, colour, shadow, and stroke appearance controls
- Auto-start / auto-stop with scene activation; stop-at-zero and hide-at-zero options
- macOS, Windows, and Linux support

