# F20 – "Automation and Data" chapter: short summary

Source: `F20_User_Manual.pdf` (rev 6.1.0, 2011), pages 83-100.
Items marked **(2013)** come from the newer F20 manual rev 7.3.2.0 (2013):
<https://www.egr.msu.edu/psp/sites/default/files/content/F20%20User%20Manual.pdf>

Written 2026-10-05. Not yet tested on our hardware.

> **Update 2026-10-06:** our implementation is all C++ (C++/CLI bridge, no
> C#) with MQTT to the server — see `F20_Qt_Software_Specification.md`. The
> C#/VB.NET wording below comes from the manual's own samples and still
> applies as API reference.

**In one sentence:** your own .NET program can run the F20 through the
**FIRemote** class inside FILMeasure.exe, and FILMeasure still does all the
measuring and calculating.

```
 Your program (C# / VB.NET) on the NUC
        │  uses the FIRemote class
        ▼
 FILMeasure.exe  ── does the fit: thickness, n, k, GOF
        │  USB
        ▼
       F20
```

## 1. Getting started

- **Sample program:** `C:\Program Files\Filmetrics\FILMeasure\SourceCode\FIRemoteTest`. Start here.
- **Test tool:** `FIRemoteTest.exe` in the FILMeasure folder lets you try every
  command without writing code.
- **Folder rule (2013):** your program must sit in the same folder as
  FILMeasure.exe, so copy the whole FILMeasure folder into your build output.
- **Window:** FILMeasure's window can be shown (good for testing) or hidden
  (good for production).

## 2. Key idea: channel and system

- **Channel** = one measuring head. An F20 has **one** channel.
- **System** = a group of channels, only on the F32 and F37.
- Each one has an ID (a GUID). With one F20 you can **skip the GUID** in most calls.
- Don't rely on list order. Find the unit by GUID or serial number
  (e.g. `F20:09A006`).

## 3. Commands by job

| Job | Commands | What it does |
|---|---|---|
| Start | `New(...)` | Starts FILMeasure inside your program |
| Choose product | `SetRecipe` | Loads a saved recipe (film model + settings) |
| Calibrate | `BaselineSetRefMat` → `BaselineAcquireSpectrumFromSample` → `BaselineAcquireReference` → `BaselineAcquireBackgroundAfterRef` → `BaselineCommit` | Runs the 3-step baseline: sample → reference → dark |
| Reuse calibration | `AuthenticateRefBac`, `BaselineAcquireReferenceUsingOldSampleReflectance` | Recovers the last baseline, or skips step 1 |
| **Measure + calculate** | **`Measure`** | Takes a spectrum and fits it; returns the results (`FIMeasResults`) |
| Split steps | `AcquireSpectrum`, then `AnalyzeSpectrum` | Measure first and calculate later, or calculate again with another recipe |
| Files | `SaveSpectrum`, `OpenSpectrum` | Saves or loads spectra (`.csv`, `.txt`, `.spe`, `.fmspe`) |
| Adjust model | `SetThickness`, `SetRoughness` (+ `SetMaterial`, `SetAnalysisWavelengthRange` in 2013) | Changes the starting guess or settings of a layer |
| Info | `MeasChannelHWName`, `MeasChannelHWSerialNumber`, `NumberOfChannels` | Shows which hardware is connected |
| Check (2013) | `BaselineExistsAndIsAuthenticated`, `SpectrometerDiagnostics` | Is the calibration valid? Is the signal OK? |
| Digital I/O (2013) | `GeneralPurposeIOIsSupported`, `GeneralPurposeIOReadValue`, `GeneralPurposeIOSetValue` | Reads and sets logic signals, only on some hardware |

**F32/F37 only, not needed for the F20:** `SystemMeasure`,
`SystemStartMonitoring`, `SystemStopMonitoring`, `SystemAutoSave`, and the
`SystemMeasurementCompleted` event.

## 4. Rules the chapter stresses

- 🔴 **Catch exceptions on every call.** For example, `Measure` throws if the
  Measure button is disabled.
- 🟡 **Nothing is re-analyzed for you.** After `SetRecipe` or `OpenSpectrum`,
  call `AnalyzeSpectrum` yourself.
- 🟡 **History.** `addToHistory = false` keeps a result out of FILMeasure's
  History, e.g. when you try two recipes and keep only the better one.
- 🟡 **Result fields** are not listed in the manual. Look them up in Visual
  Studio's **Object Browser**.

## 5. What this means for the NUC

```
 Machine PLC ──Ethernet (TCP / Modbus / OPC-UA: we build this)──► NUC
                                                                   │ Windows 10/11
                                                                   │ FILMeasure + our program
                                                                   └──USB──► F20
```

- Put Windows, FILMeasure and our program on the NUC.
- A normal production cycle is `SetRecipe` → `Measure` → read results, plus a
  baseline per shift. For films under 100 nm, redo the baseline every 20-30 min,
  after a 10-min lamp warm-up.
- The chapter offers **no network interface**. We build the link from the NUC
  to the machine ourselves.

## Next step

Run `FIRemoteTest.exe` on the NUC with the F20 connected and check that
`Measure` works.
