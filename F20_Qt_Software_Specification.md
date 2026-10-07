# F20 Measurement Software — Specification

| | |
|---|---|
| Status | Draft for review — code not started |
| Author | Hao Tran |
| Date | 2026-10-06 |
| Toolchain | Qt Creator 6 · Qt 6 (C++) · MSVC · C++/CLI (no C#) |
| Main sources | `F20_User_Manual.pdf` rev 6.1.0 (2011); items marked **(\*)** exist only from FILMeasure 7 (manual rev 7.3.2.0, 2013) |
| Related docs | `F20_Tu_dong_hoa_bao_cao.md` (feasibility, Vietnamese), `F20_Automation_and_Data_Summary.md` (FIRemote summary) |

**In one sentence:** two programs on the Windows NUC — a Qt 6 operator app
(`f20app`) and a small C++/CLI bridge (`f20bridge`) that is the only code
talking to FILMeasure's FIRemote class — automate the F20 and connect it to
the Linux server.

---

## 1. Purpose and scope

The software lets the production line measure film thickness with the F20
without anyone clicking inside FILMeasure.

**The software does:**

- run measurements (choose recipe → measure → show and store the result)
- guide the operator through the 3-step baseline and track baseline age
- store every result and spectrum; show trend/SPC charts
- give the Linux server / PLC a network interface (commands in, results out)

**Stays in FILMeasure (not rebuilt by us):**

- all optics control, spectrum fitting, thickness/n/k calculation
- recipes: created and tuned by an engineer in FILMeasure's Edit Recipe
- pass/fail limits: the recipe's **GOF Error** and **Thickness Limits**
  (Alarms tab) are the source of truth — our software reads the verdict,
  it does not invent its own limit system

**Out of scope:** creating recipes from code (FIRemote cannot), controlling
the lamp power switch, moving samples (the production machine does that),
XY mapping.

## 2. System overview

```
┌──────────────── Linux server ────────────────┐
│ dashboard · database of record · SPC · alarms │
└──────────────┬───────────────▲───────────────┘
        commands│               │results, status, alarms
                ▼   LAN · MQTT (broker on the server)
┌────────────────── NUC · Windows 10/11 ───────────────────┐
│  ┌─────────────────────┐              ┌───────────────┐  │
│  │ f20app  (Qt 6, x64) │  local TCP   │ f20bridge.exe │  │
│  │ operator screens    │◄────────────►│ C++/CLI       │  │
│  │ storage · SPC       │  JSON lines  │ (in FILMeasure│  │
│  │ MQTT client         │              │  folder)      │  │
│  └─────────────────────┘              └───────┬───────┘  │
│                                      FIRemote │ (.NET)   │
│                               ┌───────────────▼────────┐ │
│                               │ FILMeasure.exe         │ │
│                               │ fitting · recipes      │ │
│                               └───────────────┬────────┘ │
└───────────────────────────────────────────────┼──────────┘
                                            USB │ 2.0
                                            ┌───▼───┐  fiber  ┌────────────┐
                                            │  F20  │◄───────►│ LA1-RKM    │
                                            └───────┘         │ inline head│
                                                              └────────────┘
```

**Why two programs instead of one:**

| Reason | Detail |
|---|---|
| Bitness | FIRemote loads only into a .NET process of FILMeasure's bitness. If FILMeasure.exe is 32-bit, a 64-bit Qt 6 app can never load it. The bridge is built to match; `f20app` stays x64. |
| Folder rule | The program referencing FILMeasure.exe must sit in the FILMeasure folder (2013 manual). Only the small bridge has to live there. |
| Crash isolation | If FILMeasure hangs or throws, `f20app` keeps running, restarts the bridge, and reports the fault to the server. |
| Future move | The UI could later run on the Linux server with no bridge change (same protocol over LAN). |

## 3. Words used

| Word | Meaning |
|---|---|
| channel | One measuring head. An F20 has exactly one. Addressed by GUID; found by serial number (e.g. `F20:09A006`), never by list index. |
| recipe | FILMeasure file: film stack + acquisition + analysis settings + alarm limits. |
| baseline | 3-step calibration: sample spectrum → reference spectrum → dark (background). Required before measuring. |
| reference material | Known reflectance standard (usually Si wafer), chosen from FILMeasure's list; custom = `.rrr` file in the Material subfolder. |
| GOF | Goodness of fit, 0–1. 1 = calculated spectrum matches measured perfectly. |
| spectrum | Reflectance vs wavelength array. Saved as `.fmspe` (full data, FILMeasure ≥ 6), `.spe`, `.csv`, `.txt`. |

## 4. Measure bridge (`f20bridge`)

A console program, C++/CLI (`/clr`), no UI of its own. It is the **only**
code that references FILMeasure.exe.

**Does:** open one FIRemote session, serve the JSON socket, translate each
command to one FIRemote call, translate every .NET exception to an error
reply, push events (startup warning, FILMeasure died).

**Does not:** store data, decide pass/fail, talk to the network beyond
`localhost`, retry failed calls on its own.

**Code structure (all-C++ team, .NET quarantined to one file):**

| File | Compiled as | Contents |
|---|---|---|
| `main.cpp`, `socket_server.cpp`, `json_protocol.cpp` | standard C++17 | config, socket loop, protocol — nlohmann/json (single header), native sockets, spdlog logging |
| `firemote_gateway.h` | standard C++17 header | plain C++ interface + result structs; **no .NET types in it** |
| `firemote_gateway.cpp` | the **only** `/clr` file | each gateway call → one FIRemote call, every .NET exception caught; ~300 lines, translated from the `FIRemoteTest` VB.NET sample |

`json_protocol.cpp` is shared with `f20bridge-sim`, so the simulator and the
real bridge cannot drift apart. Qt stays out of the bridge (moc + `/clr` is
the known-bad combination).

Lifecycle:

```
start ──► read config (port, expected serial, showGui)
      ──► New(showGui, warning, msg)      // 3-argument constructor; the
      │                                   // 1-argument form is deprecated
      ├─ warning = StartupRecipeLoadFailure ──► event "startupWarning"
      ──► find channel GUID by serial number (MeasChannelGuids +
      │   MeasChannelHWSerialNumber) — never by index (manual p. 90)
      ──► listen on 127.0.0.1:<port>, one client at a time
      ──► serve commands until "quit" or socket closes
      ──► dispose FIRemote, exit 0
```

Rules:

- 🔴 One worker thread makes **all** FIRemote calls (they block; FILMeasure
  is not documented as thread-safe). The socket reader queues commands to it.
- 🔴 Every FIRemote call sits in try/catch; the exception text goes into the
  error reply, never crashes the bridge.
- 🟡 Only one command runs at a time; a second command while busy gets
  `{"ok": false, "error": {"code": "busy"}}` immediately.
- 🟡 `showGui=true` in maintenance, `false` in production (config flag).

## 5. Bridge protocol

Newline-delimited JSON over local TCP. One request → one reply, matched by
`id`. Events have no `id`.

```json
→ {"id": 7, "cmd": "measure", "params": {"addToHistory": false}}
← {"id": 7, "ok": true, "result": {"layers": [{"layer": 1, "thicknessNm": 512.3}],
    "gof": 0.987, "summary": "…", "passed": true}}
← {"event": "filmeasureDied", "data": {"exitCode": -1}}
```

### 5.1 Commands

| Command | FIRemote call(s) | Result |
|---|---|---|
| `getVersion` | — (bridge) + FILMeasure version | bridge + FILMeasure versions |
| `getStatus` | `BaselineExistsAndIsAuthenticated` (*), else bridge-tracked state | `baselineValid`, `busy`, `channelSerial` |
| `listChannels` | `MeasChannelGuids`, `MeasChannelHWName`, `MeasChannelHWSerialNumber` | name, serial, guid per channel |
| `setRecipe` | `SetRecipe(name)` — subfolders with `\` | — |
| `setThicknessNm` | `SetThickness(layer, nm)` | — |
| `setRoughnessNm` | `SetRoughness(layer, nm)` | — |
| `baselineSetRefMat` | `BaselineSetRefMat(name)` | — |
| `baselineStep1` | `BaselineAcquireSpectrumFromSample` | — |
| `baselineStep2` | `BaselineAcquireReference` | — |
| `baselineStep2FromOldSample` | `BaselineAcquireReferenceUsingOldSampleReflectance` | — |
| `baselineStep3` | `BaselineAcquireBackgroundAfterRef` | — |
| `baselineCommit` | `BaselineCommit` | — |
| `baselineRecover` | `AuthenticateRefBac` | — |
| `measure` | `Measure(rtf=false, addToHistory)` | layers[] (thicknessNm, and n, k, roughnessNm when the recipe solves them), gof, summary, passed |
| `acquireSpectrum` | `AcquireSpectrum(…)` | wavelengthNm[], reflectance[] |
| `analyzeSpectrum` | `AnalyzeSpectrum(…)` | same as `measure` |
| `saveSpectrum` | `SaveSpectrum(path)` — format from extension | — |
| `openSpectrum` | `OpenSpectrum(path)` | — (call `analyzeSpectrum` after; FILMeasure does not re-analyze on open) |
| `getDiagnostics` | `SpectrometerDiagnostics` (*) | reference/background counts |
| `quit` | — | bridge exits |

Baseline step order is enforced by FILMeasure itself (step 2 throws unless
step 1 ran, unless timing mode is Manual) — the bridge passes the error
through; the order logic for the operator lives in `f20app`.

### 5.2 Error model

Reply: `{"ok": false, "error": {"code": "...", "message": "<exception text>"}}`

| code | Raised when | Operator message (shown by f20app) |
|---|---|---|
| `measureNotReady` | `Measure` throws because the Measure button would be disabled (usually: no baseline) | "Run the baseline first" |
| `baselineOrderWrong` | a baseline step throws because its rule is not met | "Baseline steps out of order — restart the baseline" |
| `baselineRecoverFailed` | `AuthenticateRefBac` throws (no old baseline, hardware changed…) | "No saved baseline — run a full baseline" |
| `referenceSignalBad` | reference step error from Reference Signal Thresholds (Setup > Options) | "Reference reading too high/low — check wafer and focus" |
| `recipeNotFound` | `SetRecipe` throws | "Recipe '<name>' not found in FILMeasure" |
| `fileOpenFailed` | `OpenSpectrum` / `SaveSpectrum` throws | "Cannot open/save file" |
| `layerOutOfBounds` | `SetThickness`/`SetRoughness` throws | "Layer number or value out of range" |
| `hardwareMissing` | 0 channels, or serial not found | "F20 not connected — check USB" |
| `busy` | command while another runs | — (f20app queues) |
| `filmeasureError` | any other exception | message text as-is |

Full `.NET` exception types are not listed in the manual → the mapping is
by message text, finalized on the NUC against `FIRemoteTest.exe` behavior
(open item, §9).

## 6. Operator app (`f20app`)

Qt 6 Widgets (or Quick — implementer's choice), x64, full screen on the NUC.
Four screens.

### 6.1 Measure screen

```
┌────────────────────────────────────────────────────┐
│ Recipe: [SiO2 on Si ▼]   Baseline: 🟢 12 min ago   │
│                                                    │
│  ┌ spectrum chart ───────────────┐  Layer 1        │
│  │  measured (blue)              │  512.3 nm       │
│  │  calculated (red)             │  GOF 0.987      │
│  │                               │  ┌──────────┐   │
│  └───────────────────────────────┘  │   PASS   │   │
│ Operator: [____]  Sample: [____]    └──────────┘   │
│        [ MEASURE ]        [ auto cycle: off ]      │
└────────────────────────────────────────────────────┘
```

- Recipe list comes from the FILMeasure Recipes folder (configured path).
- Result shows per-layer `thicknessNm` (plus n, k, roughness when the recipe
  solves them), GOF, and PASS/FAIL.
- **Pass/fail verdict**: FAIL when FILMeasure flags the result invalid
  (recipe GOF Error / Thickness Limits) or when GOF < the recipe limit.
  Verdict is stored with every result.
- Auto cycle: measure every N seconds, or on a remote `measure` command
  from the server/PLC (§6.6).
- Chart: measured (blue) vs calculated (red), same colors as FILMeasure.
- A measurement is blocked (button grey, reason shown) when the baseline is
  missing or stale (§6.2).

### 6.2 Baseline screen

Guided wizard, same steps and words as the FILMeasure dialog:

```
set reference material ──► step 1: sample on stage, MEASURE SAMPLE
   ──► step 2: reference wafer on stage, MEASURE REFERENCE
   ──► step 3: remove reference (dark), MEASURE BACKGROUND
   ──► COMMIT ──► baseline timestamp saved
```

Rules (from the manual, cross-checked with the ASU SOP):

| Rule | Value | Behavior in f20app |
|---|---|---|
| Lamp warm-up | 15 min for films < 250 nm or n&k; 5 min otherwise. (Manual states 10 min in one chapter and 5/15 min in another — we take the stricter per film type.) | Warm-up countdown shown after power-on; baseline button disabled until done (override with reason, logged). |
| Baseline age | films < 1000 Å: redo every 20–30 min; thicker: per shift | Age shown next to every measurement: 🟢 fresh, 🟡 > 20 min, 🔴 > 30 min (thin-film recipes) → measuring blocked, server notified. Thresholds per recipe in config. |
| Invalidated by | integration time change, fiber moved, room temperature change > 5 °F | "Baseline invalid" button for the operator + automatic invalidation when the recipe's integration settings change. |
| Same height | reference surface at sample height | Written as an instruction in the wizard step. |
| Recover | `baselineRecover` after restart/power cut | Offered at startup when FILMeasure has a stored baseline. |

### 6.3 History / SPC screen

- Table of past results (newest first): time, recipe, operator, sample,
  thicknessNm per layer, GOF, PASS/FAIL.
- Trend chart per recipe/layer with mean, ±3σ control limits, and the
  recipe's thickness limits; 🟡 drift warning when the last N points trend
  one-sided (classic SPC run rule).
- Export selection to CSV.
- Re-analyze: pick an old spectrum file → `openSpectrum` +
  `analyzeSpectrum` with the current recipe (e.g. after a limit change);
  re-analyzed results are marked as such, never overwrite the original.

### 6.4 Diagnostics screen

- Bridge/FILMeasure/F20 state (🟢🟡🔴), channel serial, versions.
- Signal health: reference/background counts from `getDiagnostics` (*) —
  good signal 2500–3500 counts (Diagnostics chapter); near 4095 =
  saturation 🔴; a falling reference count over days = lamp aging 🟡 →
  alarm to the server. Without (*) commands, the fallback is the reference
  spectrum level captured at each baseline.
- Log view (last 200 lines), bridge restart button.

### 6.5 Storage

- **SQLite** file on the NUC, one row per measurement:
  `time, recipeName, channelSerial, operatorName, sampleId, layerNumber,
  thicknessNm, n, k, roughnessNm, gof, passed, spectrumFile, reanalyzedFrom`.
- Spectra: `spectra/<date>/<time>_<sampleId>.fmspe` (full fidelity) +
  `.csv` (readable anywhere), both via `saveSpectrum`.
- Retention configurable (default: keep everything; the Linux server is the
  database of record and pulls data over the API).
- FILMeasure's own Data Recording option (Setup > Options) may be switched
  on as an independent backup file — noted, not relied on.

### 6.6 Network interface (MQTT — plant standard)

The plant standardizes on MQTT over the LAN, so MQTT is the primary
interface. `f20app` is an MQTT client; the broker (e.g. Mosquitto) runs on
the Linux server. It follows the **company command format**: two topics
per machine and one JSON envelope. QoS 1; plant LAN only.

| Topic | Published by | Subscribed by |
|---|---|---|
| `{SerialNumber}/ar/f20/send` | f20app | server |
| `{SerialNumber}/ar/f20/receive` | server | f20app |

`{SerialNumber}` is the F20 serial number. Topic names are confirmed with
the server team (open item 14).

Envelope (every message, both directions):

```json
{"command": "status", "command_type": "request", "data": {},
 "machine_name": "F20", "machine_sn": "09A006",
 "transaction_id": "4941-20240907-141649"}
```

| Field | Meaning |
|---|---|
| `command` | command name (table below) |
| `command_type` | `request`, `response` or `ack` |
| `data` | command data; `{}` if none |
| `machine_name` | `F20` from f20app; `AR` from the server |
| `machine_sn` | F20 serial number |
| `transaction_id` | new for each request; copied into its response or ack. Format `NNNN-yyyyMMdd-HHmmss` |

Rules:

- If `status` stops, the server treats the F20 as offline.
- Keys are snake_case; numbers are JSON numbers; thickness in nm; times in UTC.
- On failure, `data.error` holds the code (§5.2) and `data.message` the
  operator text; `error` is `""` on success.

| Command | Direction | Answer |
|---|---|---|
| `status` | F20 → server, every 1 s | none |
| `result` | F20 → server, after each measurement | ack |
| `alarm` | F20 → server | ack |
| `measure` | server → F20 | response: result or error |
| `baseline_invalidate` | server → F20 | ack |
| `get_results` | server → F20 | response, paged |
| `get_spectrum` | server → F20 | response: spectrum as CSV |

The company commands `setting` and `update` are not used: recipes live in
FILMeasure, and software is installed manually.

Data per command:

- **status** — `machine_status` is a state from §7:
  `{"software_version": "1.0.0", "bridge_version": "1.0.0",
  "filmeasure_version": "6.1.0", "machine_status": "Ready",
  "recipe_name": "SiO2 on Si", "baseline": {"valid": true, "age_minutes": 12},
  "warm_up_left_minutes": 0, "processed_today": 50, "error": ""}`
- **result** — also the data of a successful `measure` response; `n`, `k`
  and `roughness_nm` appear only when the recipe solves them:
  `{"result_id": "3f2a9c1e-…", "measured_at": "2026-10-07T08:15:30.120Z",
  "recipe_name": "SiO2 on Si", "sample_id": "LOT42-07",
  "operator_name": "op-01", "passed": true, "gof": 0.987,
  "layers": [{"layer": 1, "thickness_nm": 512.3}],
  "baseline_age_minutes": 12, "reanalyzed_from": "", "error": ""}`
- **measure** — both fields optional; the measurement uses the recipe and
  sample ID it carries: `{"recipe_name": "SiO2 on Si", "sample_id":
  "LOT42-07"}`. A refusal: `{"error": "baselineStale", "message":
  "Baseline too old - redo the baseline"}`.

| Command | Request data | Answer data |
|---|---|---|
| `alarm` | `kind` (`fail`, `baseline_stale`, `signal_low`, `fault`), `message` | ack: `{}` |
| `baseline_invalidate` | `reason`, e.g. `"fiber moved"` | ack: `error` |
| `get_results` | `since` (UTC), `page` | `results`, `page`, `pages`, `error` |
| `get_spectrum` | `result_id` | `result_id`, `csv`, `error` |

- Broker host, credentials/TLS and MQTT version follow the plant standard —
  confirm with IT (open item 10).
- Spectra are kilobytes, so they fit in the reply payload; no file channel.
- REST + WebSocket is the documented alternative (not the default); the
  internal design keeps the transport behind one interface.

## 7. Behavior rules

State machine in `f20app` (single source of truth for "may we measure?"):

```
        ┌──────────┐ bridge up ┌──────────┐ baseline ok ┌─────────┐
 boot ─►│ starting │──────────►│ noBaseline│────────────►│  ready  │◄─┐
        └────┬─────┘           └──────────┘             └────┬────┘  │ done
             │ bridge dead                ▲ stale/invalid    │measure│
             ▼                            └──────────────────┤       │
        ┌──────────┐  restart ok                        ┌────▼────┐  │
        │  fault   │────────────────────────────────────│measuring│──┘
        └──────────┘                                    └─────────┘
```

- Bridge dies or socket drops → state `fault`, auto-restart up to 3 times
  (then 🔴 alarm to server + operator). FILMeasure is restarted by the
  bridge, so a FILMeasure crash is the same case.
- USB unplugged → bridge reports `hardwareMissing` → `fault` with its own
  message; recovery = replug + bridge restart + baseline recover/redo.
- Exactly one FILMeasure instance (license terms + one channel): the bridge
  refuses to start when FILMeasure is already running.
- Logging: both programs write rotating plain-text logs
  (`logs/f20app_<date>.log`, `logs/f20bridge_<date>.log`); every bridge
  command/reply is logged with its `id`.

## 8. Build and deployment

One repository, one top-level CMake project, opened in Qt Creator 6:

| Target | Language / toolchain | Arch | Notes |
|---|---|---|---|
| `f20app` | C++17, Qt 6, MSVC | x64 | Widgets/Quick, QtCharts, QtNetwork, QtSql (SQLite), Eclipse Paho MQTT C++ (EPL/EDL; Qt MQTT is GPLv3 or commercial only) |
| `f20bridge` | C++/CLI, MSVC `/clr`, .NET Framework | match FILMeasure (corflags, §9) | references `FILMeasure.exe` as assembly; **no Qt** in this target; sockets + JSON in standard C++ (nlohmann/json); only `firemote_gateway.cpp` compiled `/clr` (§4) |
| `f20bridge-sim` | C++17, plain | x64 | simulator speaking the same protocol with canned spectra → lets `f20app` be developed and tested without the instrument |

- C++/CLI builds only with MSVC on Windows; CMake supports it
  (`/clr` flag per target). If Qt Creator's CMake integration fights the
  `/clr` target, the fallback is a separate `.vcxproj` for the bridge built
  by the same CI script — decided on the NUC (§9).
- Deployment: `f20app` via `windeployqt` to its own folder;
  `f20bridge.exe` + its config copied **into the FILMeasure folder**;
  `f20app` autostarts with Windows and launches the bridge.
- Config: one `f20.ini` next to `f20app` (bridge port, expected channel
  serial, recipe folder path, baseline thresholds per recipe, MQTT broker
  host/port + credentials, showGui flag).

## 9. Open items — must be checked on the NUC before coding the bridge

| # | Check | How | Decides |
|---|---|---|---|
| 1 | FILMeasure.exe bitness + .NET version | `corflags FILMeasure.exe`, VS Object Browser | bridge arch (x86/x64) |
| 2 | FILMeasure version 6 or 7 | Help > About FILMeasure | whether (*) commands (`getStatus` real check, `getDiagnostics`) exist |
| 3 | `FIMeasResults` field names | VS Object Browser (not in the manual) | `measure` result mapping |
| 4 | Exception types/messages per failure | provoke each with `FIRemoteTest.exe` | error-code table §5.2 |
| 5 | FIRemote vs Access Control | enable access control, run FIRemoteTest | whether production NUC can use logins |
| 6 | Reference material exact string for our wafer | FILMeasure Baseline dialog list | `baselineSetRefMat` value |
| 7 | Real measure cycle time with our recipe | stopwatch in FIRemoteTest | auto-cycle rate, command timeouts |
| 8 | Qt Creator + CMake builds the `/clr` target | try on the NUC | §8 fallback decision |
| 9 | Our unit's spec configuration | config sheet / serial | which spec table applies (2011 manual: 15 nm–100 µm, 0.4 %/2 nm · 2025 datasheet: 15 nm–70 µm, 0.2 %/2 nm) |
| 10 | Broker host, MQTT version, credentials/TLS | ask IT | `[mqtt]` settings in `f20.ini` (library decided: Eclipse Paho MQTT C++) |
| 14 | Company MQTT conventions for the F20 | agree with the server team | topic names and envelope details (§6.6) |

## 10. Acceptance tests (bench, with the F20 connected)

1. Bridge start: FILMeasure hidden, channel found by serial, `getStatus` ok.
2. Each §5.1 command gives the same effect as the matching button in
   `FIRemoteTest.exe` (spot checks for all; exact for baseline + measure).
3. Full production cycle: warm-up → baseline wizard → `setRecipe` →
   `measure` → result + spectrum stored → result arrives at an MQTT
   subscriber → PASS/FAIL matches FILMeasure's own display for the same sample.
4. Error paths: measure without baseline → `measureNotReady` and a clear
   operator message; wrong recipe name → `recipeNotFound`; USB unplug →
   `fault` state, alarm pushed, recovery per §7.
5. Stale baseline: with a thin-film recipe, age > 30 min blocks measuring
   locally and refuses the remote measure command.
6. Kill FILMeasure in Task Manager: bridge restarted automatically, state
   returns to `noBaseline`, baseline recover offered.
7. Re-analyze an old spectrum with a second recipe; original result
   unchanged, new result marked re-analyzed (uses `addToHistory=false`).
8. 4-hour auto-cycle soak: no memory growth in either program, no missed
   results in the database.

---

*Written from the F20 manuals in this folder. Where the 2011 manual and the
2025 datasheet disagree on numbers, both are quoted and item 9 of §9 decides.*
