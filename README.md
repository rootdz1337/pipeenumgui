# pipeenumgui
pipe enum gui a full enum on pipe detect and trace 


GUI-based **Named Pipe Detection** tool. It detects open named pipes on the system (useful for spotting C2 channels, malware, or inter-process communication), and lets you filter and inspect them.


## What this tool does

Enumerates every named pipe in the `\\.\pipe\` namespace via `FindFirstFileW(L"\\\\.\\pipe\\*")`, counts the number of **instances** for each pipe (by repeatedly opening it), and displays them in a resizable list.

### GUI features

| Control | Purpose |
|---|---|
| **Filter box** | Live case-insensitive substring filter on pipe names |
| **Refresh** | Re-enumerates all pipes |
| **Copy Name** | Copies `\\.\pipe\<name>` to clipboard |
| **Open Pipe** | Attempts to open the selected pipe (RW, then RO fallback) and reports success/error |
| **Details pane** | Shows full path + instance count for the selected pipe |
| **Double-click row** | Shows details + copies path to clipboard |
| **Status bar** | Shows count of displayed/total pipes and operation results |

### Instance counting

The loop tries to `CreateFileW` on `\\.\pipe\<name>` with `OPEN_EXISTING` and no sharing. Each successful open is one instance. A cap of 100 prevents hangs on always-available pipes (e.g. `lsass`, `epmapper`). Note that a few system pipes will block briefly if they are in a synchronous server listen state — the cap and non-overlapped open keep this bounded.

## Build

```
cl /EHsc /W3 pipe_gui.cpp /link user32.lib gdi32.lib comctl32.lib
```

Visual Studio: **Windows Desktop Application**, subsystem **Windows** (`/SUBSYSTEM:WINDOWS`), link `comctl32.lib`.

## Notes / caveats

- **No admin needed** for enumeration, but some pipes (especially `InitShutdown`, `lsass`, `ntsvcs`) may return `ERROR_PIPE_BUSY` or `ACCESS_DENIED`; that's normal and simply results in `0` or fewer instances counted.
- The "Open Pipe" button is **diagnostic only** — it opens and immediately closes the handle. It does not terminate or inject into the pipe's server.
- If you want to **detect which process owns a pipe**, that requires `NtQueryObject` with `ObjectNameInformation` on a handle obtained by `CreateFileW(..., FILE_FLAG_OVERLAPPED, ...)`, plus `GetNamedPipeServerProcessId()` — that's a good next step if you want to turn this into a full C2-hunting tool.
- The `EnumChildWindows` + lambda trick requires C++11 or later (works with MSVC 2015+).
