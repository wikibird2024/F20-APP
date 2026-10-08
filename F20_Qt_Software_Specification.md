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
Five screens.

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

#### 6.6.1 Topics

| Topic | Published by | Subscribed by |
|---|---|---|
| `{SerialNumber}/ar/f20/send` | f20app | server |
| `{SerialNumber}/ar/f20/receive` | server | f20app |

`{SerialNumber}` is the F20 serial number. Topic names are confirmed with
the server team (open item 14).

#### 6.6.2 General structure of command

Every message, both directions:

```json
{"command": "status", "command_type": "request", "data": {},
 "machine_name": "F20", "machine_sn": "09A006",
 "transaction_id": "4941-20240907-141649"}
```

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `command` | Command name (6.6.4) | String, snake_case | `status` |
| 2 | `command_type` | Kind of message | Enum: `request` / `response` / `ack` | `request` |
| 3 | `data` | Command data; `{}` if none | Object | `{}` |
| 4 | `machine_name` | Sender: `F20` from f20app, `AR` from the server | String | `F20` |
| 5 | `machine_sn` | F20 serial number, without the `F20:` prefix | String | `09A006` |
| 6 | `transaction_id` | New for each request; copied into its response or ack | String `NNNN-yyyyMMdd-HHmmss` (first part: open item 14) | `4941-20240907-141649` |

#### 6.6.3 Rules

- `measure`, `get_results` and `get_spectrum` get a response; `result`,
  `alarm` and `baseline_invalidate` get an ack; `status` gets no answer.
- Keys are snake_case. Codes and states are camelCase words, as in §5.2 and §7.
- Numbers are JSON numbers, never strings. A number with a unit carries it
  in its key (`thickness_nm`, `age_minutes`).
- Times are UTC strings `yyyy-MM-ddTHH:mm:ss.zzzZ`, e.g.
  `2026-10-07T08:15:30.120Z`.
- An optional field is left out when it has no value; it is never `null`.
- On failure, `data.error` holds the code and `data.message` the operator
  text; `error` is `""` on success.
- Offline: no `status` for 5 s, or the broker's Last Will (a `status` with
  `machine_status` `offline`, sent when f20app drops off the broker).

#### 6.6.4 Command list

| No. | Command | Direction | Answer |
|---|---|---|---|
| 1 | `status` | F20 → server, every 1 s | none |
| 2 | `result` | F20 → server, after each measurement | ack |
| 3 | `alarm` | F20 → server | ack |
| 4 | `measure` | server → F20 | response: result or error |
| 5 | `baseline_invalidate` | server → F20 | ack |
| 6 | `get_results` | server → F20 | response, paged |
| 7 | `get_spectrum` | server → F20 | response: spectrum as CSV |

The company commands `setting` and `update` are not used: recipes live in
FILMeasure, and software is installed manually.

#### 6.6.5 Detail data of command

##### 6.6.5.1 STATUS (F20 → server)

Sent every 1 s; no answer.

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `software_version` | f20app version | String | `1.0.0` |
| 2 | `bridge_version` | f20bridge version | String | `1.0.0` |
| 3 | `filmeasure_version` | FILMeasure version | String | `6.1.0` |
| 4 | `machine_status` | f20app state (§7); `offline` only in the Last Will | Enum: `starting` / `noBaseline` / `baselining` / `ready` / `measuring` / `analyzing` / `fault` / `offline` | `ready` |
| 5 | `recipe_name` | Recipe selected in f20app | String | `SiO2 on Si` |
| 6 | `baseline` | Baseline in use | Object | |
| 7 | `baseline.valid` | `true` when a committed baseline exists and is not invalidated | Boolean | `true` |
| 8 | `baseline.age_minutes` | Minutes since the last baseline commit; 0 if none | Integer | `12` |
| 9 | `warm_up_left_minutes` | Lamp warm-up time left; 0 when done | Integer | `0` |
| 10 | `processed_today` | Results stored since midnight, NUC local time | Integer | `50` |
| 11 | `error` | Code of the current fault (§5.2); `""` when none | String | `""` |

```json
{"software_version": "1.0.0", "bridge_version": "1.0.0",
 "filmeasure_version": "6.1.0", "machine_status": "ready",
 "recipe_name": "SiO2 on Si", "baseline": {"valid": true, "age_minutes": 12},
 "warm_up_left_minutes": 0, "processed_today": 50, "error": ""}
```

##### 6.6.5.2 RESULT (F20 → server)

Sent after each measurement; the server acks it. The same data is the
answer to a successful `measure`.

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `result_id` | ID of the stored result | String, UUID | `3f2a9c1e-…` |
| 2 | `measured_at` | Time of the measurement | String, UTC time (6.6.3) | `2026-10-07T08:15:30.120Z` |
| 3 | `recipe_name` | Recipe used | String | `SiO2 on Si` |
| 4 | `sample_id` | Sample ID from the operator or the server; `""` if none | String | `LOT42-07` |
| 5 | `operator_name` | Operator entered on the measure screen; `""` if none | String | `op-01` |
| 6 | `passed` | Verdict from the recipe limits (§6.1) | Boolean | `true` |
| 7 | `gof` | Goodness of fit | Number, 0–1 | `0.987` |
| 8 | `layers` | One entry per recipe layer | Array of Object | |
| 9 | `layers[].layer` | Layer number as in the recipe | Integer, from 1 | `1` |
| 10 | `layers[].thickness_nm` | Thickness | Number, nm | `512.3` |
| 11 | `layers[].n` | Refractive index; only when the recipe solves it | Number, optional | `1.46` |
| 12 | `layers[].k` | Extinction coefficient; only when the recipe solves it | Number, optional | `0` |
| 13 | `layers[].roughness_nm` | Roughness; only when the recipe solves it | Number, nm, optional | `2.1` |
| 14 | `baseline_age_minutes` | Age of the baseline used | Integer | `12` |
| 15 | `reanalyzed_from` | `result_id` of the original when re-analyzed; `""` otherwise | String | `""` |
| 16 | `error` | `""` on success | String | `""` |

```json
{"result_id": "3f2a9c1e-5b7d-4e8a-9c21-7d4e5f6a8b90",
 "measured_at": "2026-10-07T08:15:30.120Z", "recipe_name": "SiO2 on Si",
 "sample_id": "LOT42-07", "operator_name": "op-01", "passed": true,
 "gof": 0.987, "layers": [{"layer": 1, "thickness_nm": 512.3}],
 "baseline_age_minutes": 12, "reanalyzed_from": "", "error": ""}
```

##### 6.6.5.3 MEASURE (server → F20)

The server asks for one measurement now. The answer is a response: the
RESULT data (6.6.5.2), or a refusal.

Request data:

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `recipe_name` | Recipe to use; the current recipe when left out | String, optional | `SiO2 on Si` |
| 2 | `sample_id` | Sample ID stored with the result | String, optional | `LOT42-07` |

Refusal data:

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `error` | Why the measurement did not run | Enum: `storageDown` / `fault` / `starting` / `baselineWizardOpen` / `busy` / `noBaseline` / `baselineStale`, or a §5.2 code (e.g. `recipeNotFound`) | `baselineStale` |
| 2 | `message` | Operator text for the code | String | `Baseline too old - redo the baseline` |

```json
→ {"command": "measure", "command_type": "request",
   "data": {"recipe_name": "SiO2 on Si", "sample_id": "LOT42-07"},
   "machine_name": "AR", "machine_sn": "09A006",
   "transaction_id": "4941-20261007-081529"}
← {"command": "measure", "command_type": "response",
   "data": {"error": "baselineStale", "message": "Baseline too old - redo the baseline"},
   "machine_name": "F20", "machine_sn": "09A006",
   "transaction_id": "4941-20261007-081529"}
```

##### 6.6.5.4 ALARM (F20 → server)

Sent when something needs a person; the server acks with `{}`.

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `kind` | What happened | Enum: `fail` / `baselineStale` / `signalLow` / `bridgeFault` / `storageFailed` | `baselineStale` |
| 2 | `message` | Short text for the server dashboard | String | `Baseline too old - redo the baseline` |
| 3 | `result_id` | The failed result; only for `fail` | String, optional | `3f2a9c1e-…` |

```json
{"kind": "baselineStale", "message": "Baseline too old - redo the baseline"}
```

##### 6.6.5.5 BASELINE_INVALIDATE (server → F20)

The server marks the baseline invalid; f20app moves to `noBaseline`. During
a measurement it takes effect when the measurement ends (§7).

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `reason` | Why; stored in the baseline history | String | `fiber moved` |

Ack data: `error` (String, `""` when done).

```json
{"reason": "fiber moved"}
```

##### 6.6.5.6 GET_RESULTS (server → F20)

The server reads stored results, one page per request.

Request data:

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `since` | Results measured at or after this time | String, UTC time | `2026-10-07T00:00:00.000Z` |
| 2 | `page` | Page to return | Integer, from 1 | `1` |

Response data:

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `results` | RESULT data (6.6.5.2), oldest first | Array of Object | |
| 2 | `page` | This page | Integer | `1` |
| 3 | `pages` | Number of pages; 0 when no results | Integer | `3` |
| 4 | `error` | `""` on success | String | `""` |

##### 6.6.5.7 GET_SPECTRUM (server → F20)

The server reads the spectrum of one result.

Request data:

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `result_id` | Result to read | String, UUID | `3f2a9c1e-…` |

Response data:

| No. | Key | Description | Format | Example |
|---|---|---|---|---|
| 1 | `result_id` | Copied from the request | String, UUID | `3f2a9c1e-…` |
| 2 | `csv` | Text of the result's `.csv` spectrum file (§6.5); `""` on error | String | |
| 3 | `error` | `""` on success; `resultNotFound` for an unknown ID | String | `""` |

Spectra are kilobytes, so they fit in the reply payload; no file channel.

#### 6.6.6 Receive rules (f20app)

f20app checks each message on `.../receive` in this order:

1. Not valid JSON, or an envelope field missing → drop it and log it.
2. `machine_sn` is not this F20's serial → drop it and log it.
3. `command_type` is `ack` or `response` → match it to the sent message by
   `transaction_id`; no match → log it and drop it.
4. `transaction_id` already answered → send the same answer again; do not
   run the command again. QoS 1 can deliver a message twice; f20app keeps
   the last 100 answers.
5. `command` not in 6.6.4 → response with error `unknownCommand`.
6. A `data` field missing or of the wrong type → response with error
   `badRequest`; `message` names the field.
7. Otherwise run the command. `measure` passes the same check as the
   MEASURE button first (refusal codes in 6.6.5.3).

`unknownCommand`, `badRequest` and `resultNotFound` exist only on MQTT.

#### 6.6.7 Broker and transport

- Broker host, credentials/TLS and MQTT version follow the plant standard —
  confirm with IT (open item 10). All of them are set in the Settings
  screen (§6.7).
- MQTT 3.1.1 (default) or MQTT 5; clean session; client ID
  `f20-<bare serial>` (e.g. `f20-09A006`) unless the Settings screen sets
  another one.
- TLS (usually port 8883): the broker certificate is always checked against
  the company CA file, including the host name; there is no "accept any
  certificate" switch. A client certificate (mutual TLS) is optional:
  certificate file + key file (or both in one file) + key password. Files
  are PEM; a `.pfx/.p12` from IT is converted once with
  `openssl pkcs12 -in f20.pfx -out f20.pem`.
- REST + WebSocket is the documented alternative (not the default); the
  internal design keeps the transport behind one interface.

### 6.7 Settings screen

For the engineer or installer, not the operator: every setting that
changes at go-live or on site is set here, without editing files.

- **Lock.** The screen opens locked. One engineer password unlocks it; on
  first use the engineer chooses it (at least 4 characters). Input is
  masked; after 5 wrong tries the next try waits 30 s; the screen locks
  again after 10 min without input or when the engineer leaves it.
  Only a salted hash is stored (PBKDF2-HMAC-SHA256, 600 000 iterations,
  same format as the C# app). Forgotten password: an administrator deletes
  `engineerPasswordHash` from the settings file (below) and sets a new one.
- **Groups and fields.**

| Group | Fields |
|---|---|
| Server (MQTT broker) | broker address (empty = no server, messages only logged), port, user name, password, TLS on/off, CA certificate file, client certificate file, client key file, key password, client ID, MQTT version · **Test connection** |
| Device | F20 serial number, with the MQTT topics it gives (§6.6.1) |
| Bridge | port |
| Recipes | recipes folder · **Reload recipes** |
| Baseline | warn after / block after (minutes), lamp warm-up (minutes) — the defaults for recipes without their own profile |

- **Test connection** connects to the broker with the values in the form
  (not yet saved) and its own client ID, then disconnects; it shows one
  sentence: connected, login refused, certificate not trusted, or no
  answer.
- **Save** checks every value with the same rules as the app start
  (ports, serial, certificate files exist and match the TLS switch,
  recipes folder exists, warn < block, …); nothing is saved while a problem
  is listed. Each save is logged with the names of the changed settings
  (never the passwords).
- **Restart.** New values are used after a restart. After Save the status
  bar shows "Settings changed – restart to use them" until the app
  restarts; **Restart the app now** closes the app and starts it again
  (refused while measuring, analyzing or in the baseline wizard).
- **Files.** The shipped `f20.ini` next to `f20app` holds the defaults with
  comments and is never written by the app. The Settings screen saves only
  the changed values to `C:\ProgramData\Greystone\f20app.ini`; a value
  in that file wins over `f20.ini`. Passwords in it are encrypted for this
  PC (Windows DPAPI, machine scope): a copied file is useless elsewhere,
  but a local administrator can still read them.
- Settings that rarely change (timeouts, database file, keep-alive,
  per-recipe baseline profiles) stay in `f20.ini` only.

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
- Config: `f20.ini` next to `f20app` holds the defaults (bridge port,
  expected channel serial, recipe folder path, baseline thresholds per
  recipe, MQTT broker, TLS, timeouts, showGui flag). Changes made in the
  Settings screen (§6.7) go to `C:\ProgramData\Greystone\f20app.ini`;
  the installer creates that folder writable for the users who run
  `f20app`.
- MQTT over TLS needs Paho built with OpenSSL (vcpkg:
  `paho-mqttpp3[ssl]`).

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
| 10 | Broker host, MQTT version, credentials/TLS | ask IT | values in the Settings screen §6.7 (library decided: Eclipse Paho MQTT C++, TLS supported) |
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
