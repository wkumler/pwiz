# msconvert --duckdb (fork of ProteoWizard)

This fork adds one output format to ProteoWizard's `msconvert`: a **DuckDB database** that holds a
whole LC-MS batch in one file, with plain tables you can query with SQL from R, Python, the DuckDB
command line, or any other DuckDB client. It follows the approach of
[mzsql](https://github.com/wkumler/mzsql) and [mzml2db](https://github.com/wkumler/mzml2db): MS data
is just a few long tables (`MS1`, `MS2`, ...), so extracting a chromatogram or finding the fragments
of a precursor is a single `WHERE` clause.

Everything msconvert can read works as input, including vendor formats on Windows (Thermo, SCIEX,
Agilent, Bruker, Waters, Shimadzu, Mobilion, UIMF), and all of msconvert's filters apply.

**Status:** not (yet) part of official ProteoWizard. It lives on the `duckdb-output` branch of
[wkumler/pwiz](https://github.com/wkumler/pwiz). There are no prebuilt downloads; build it as
described below. ProteoWizard's own documentation follows after this section.

## Install

You build msconvert from this branch. DuckDB itself is bundled (official DuckDB 1.5.6 binaries in
`libraries/duckdb`), so there is nothing extra to install for it.

| Platform | Supported | Input formats |
|---|---|---|
| Windows, 64-bit | yes | mzML, mzXML, MGF, mz5, ... **and vendor formats** |
| Linux, 64-bit (gcc) | yes | open formats only (ProteoWizard has no vendor readers on Linux) |
| macOS, 32-bit Windows | no (msconvert builds without `--duckdb`) | |

### Windows

1. Install **Visual Studio Community 2026** with:
   - the "Desktop development with C++" workload
   - the ".NET desktop development" workload
   - under *Individual components*: **"C++/CLI support for v145 build tools (Latest)"**
     (easy to miss; the vendor readers will not build without it)

   The .NET Framework 4.7.2 developer pack is also required; Visual Studio usually provides it.
2. Get the source (about 1.5 GB):
   ```
   git clone --filter=blob:none -b duckdb-output https://github.com/wkumler/pwiz.git
   cd pwiz
   ```
3. Build msconvert, in a Command Prompt in that folder (about 15 minutes the first time):
   ```
   quickbuild.bat --abbreviate-paths --i-agree-to-the-vendor-licenses toolset=msvc-14.5 address-model=64 -j4 msconvert
   ```
   `--i-agree-to-the-vendor-licenses` enables the vendor readers; read the vendor license
   agreements first (the `EULA.*` files that end up next to msconvert).
4. The result is in `build-nt-x86\msvc-release-x86_64\`: `msconvert.exe`, `duckdb.dll`, the vendor
   DLLs and the license files. This folder is self-contained; copy it anywhere (keep `duckdb.dll` next
   to `msconvert.exe`) and add it to your `PATH` if you like.

To also build MSConvertGUI, SeeMS and the other tools, use the target `executables` instead of
`msconvert`, or `pwiz-bin.tar.bz2` for an archive like ProteoWizard's own Windows download.

### Linux

Tested on Ubuntu 24.04 with gcc 13 (in a Docker container). Install the build tools, clone, build:
```
sudo apt-get install build-essential git ca-certificates bzip2 xz-utils unzip python3
git clone --filter=blob:none -b duckdb-output https://github.com/wkumler/pwiz.git
cd pwiz
./quickbuild.sh --abbreviate-paths --i-agree-to-the-vendor-licenses -j4 address-model=64 toolset=gcc msconvert
```
The first build takes about 15 minutes and needs about 6 GB of memory at `-j4` (use `-j2` on smaller
machines). The result is `build-linux-x86_64/gcc-release-x86_64/msconvert`, a standalone executable
with DuckDB built in.

## Use

Convert a batch into one database:
```
msconvert *.raw --duckdb --outfile batch.duckdb
```
- `--outfile` is required: it names the database that **all** the input files go into.
- Running msconvert again with the same `--outfile` **adds** the new files to the existing database.
  Each file is written in one transaction, so a failed conversion leaves the database unchanged.
- A file whose name is already in the database is an error; add `--duckdbReplaceRuns` to replace it.
- msconvert's filters work as usual, for example centroiding with the vendor's algorithm and keeping
  positive-mode scans:
  ```
  msconvert *.raw --filter "peakPicking vendor msLevel=1-" --filter "polarity positive" --duckdb --outfile batch.duckdb
  ```
- `-o <folder>` sets where the database goes. `--merge`, `--gzip` and writing to stdout (`-o -`) are
  not supported with `--duckdb`.

## What is in the database

Rows are identified by `filename`, the input file's name (for vendor files that contain several runs,
such as multi-sample SCIEX `.wiff` files, `file.wiff#<run id>`). Retention times are in **minutes**.

| Table | Columns | One row per |
|---|---|---|
| `MS1` | filename, scan_idx, rt, mz, int, ion_mobility | MS1 data point |
| `MS2` | filename, scan_idx, rt, premz, fragmz, int, voltage, ion_mobility | MS2 data point (per precursor, see below) |
| `scan_info` | filename, scan_idx, native_id, ms_level, rt, polarity, centroided, premz, voltage, tic, bpc, min_mz, max_mz, ion_mobility | spectrum (all MS levels) |
| `chroms` | filename, chrom_type, chrom_index, target_mz, product_mz, rt, int | chromatogram point (as in RaMS) |
| `file_info` | filename, n_scans, rt_start, rt_end, instrument, start_timestamp, msconvert_version, msconvert_args, ion_mobility_type | file (run) |

Details:
- `scan_idx` is the 0-based spectrum index after filtering; `native_id` is the vendor's scan identifier
  (e.g. `controllerType=0 controllerNumber=1 scan=793` for Thermo).
- `premz` is the precursor's isolation window target (or else its selected ion m/z), and `voltage` its
  collision energy. A **multiplexed** MS2 spectrum (several precursors, e.g. Thermo MSX) has its rows
  repeated once per precursor, so summing intensities across such spectra counts each peak several times.
- Spectra above MS2 (MS3 etc.) are listed in `scan_info` but their peaks are not stored.
- `ion_mobility` is NULL for data without ion mobility. Otherwise it is the drift time, 1/K0 or FAIMS
  compensation voltage of the spectrum, or the per-point value when ProteoWizard provides an ion mobility
  array (for example with msconvert's `--combineIonMobilitySpectra`). The kind and units are in
  `file_info.ion_mobility_type`, e.g. `inverse reduced ion mobility (volt-second per square centimeter)`.
- `chroms` holds the chromatograms in the file (TIC, SRM/MRM transitions, pressure traces, ...), with
  `chrom_type` the chromatogram's id and `target_mz`/`product_mz` the Q1/Q3 targets. Chromatograms come
  straight from the input file, so msconvert's spectrum filters (such as `polarity`) do not apply to them.
- UV/DAD spectra appear in `scan_info` (with no MS level) but their absorbance data is not stored.

## Query it

R:
```r
library(DBI)
con <- dbConnect(duckdb::duckdb(), "batch.duckdb", read_only = TRUE)
# extracted ion chromatogram of glycine betaine ([M+H]+ 118.0865, +/- 10 ppm) in every file
eic <- dbGetQuery(con, "SELECT filename, rt, sum(int) AS int FROM MS1
                        WHERE mz BETWEEN 118.0853 AND 118.0877 GROUP BY filename, rt ORDER BY filename, rt")
dbDisconnect(con, shutdown = TRUE)
```
More SQL, from any DuckDB client:
```sql
-- one spectrum
SELECT mz, int FROM MS1 WHERE filename = 'sample1.raw' AND scan_idx = 100;
-- fragments of a precursor across all files
SELECT * FROM MS2 WHERE premz BETWEEN 118.0853 AND 118.0877;
-- TIC and base peak chromatograms
SELECT filename, rt, tic, bpc FROM scan_info WHERE ms_level = 1 ORDER BY filename, rt;
-- what is in the database
SELECT filename, n_scans, rt_start, rt_end, instrument FROM file_info;
-- export a table to Parquet
COPY (SELECT * FROM MS1) TO 'MS1.parquet' (FORMAT parquet);
```
Databases written by this build open in older DuckDB clients too (tested with R's duckdb 1.3.2).

## How it was checked

The output was compared against independent readers of the same data, and matches them exactly
(apart from last-digit rounding of times written as text in mzML):
- [RaMS](https://github.com/wkumler/RaMS) and mzml2db on mzML files;
- Thermo's own RawFileReader (via [rawrr](https://github.com/fgcz/rawrr)) on `.raw` files;
- a standalone mzML decoder, for all of ProteoWizard's vendor test data: every vendor format, ion mobility
  (Agilent, Bruker timsTOF, Waters, Mobilion, UIMF), MRM/SRM and chromatogram-only files, multi-run
  files and multiplexed spectra.

ProteoWizard's full test suite passes on Windows and Linux.

## Known limitations

- Write only: msconvert and other ProteoWizard tools cannot read a `.duckdb` file back.
- No macOS or 32-bit Windows support (no bundled DuckDB library for them).
- The schema may still change. Databases written by earlier builds of this branch cannot be appended to
  by later ones (start a new database).
- Thermo centroids from `peakPicking vendor` include a few small peaks next to very intense ones that
  rawrr does not report (ProteoWizard asks Thermo's library for "reference and exception" peaks; rawrr
  does not). This is long-standing ProteoWizard behavior, the same in mzML output.

---

![ProteoWizard Logo](http://www.proteowizard.org/img/proteowizard-logo.jpg "ProteoWizard")

The ProteoWizard Library and Tools are a set of modular and extensible open-source, cross-platform tools and software libraries that facilitate proteomics data analysis.

The libraries enable rapid tool creation by providing a robust, pluggable development framework that simplifies and unifies data file access, and performs standard chemistry and LCMS dataset computations.

Core code and libraries are under the Apache open source license; the vendor libraries fall under various vendor-specific licenses.

## Features
* reference implementation of HUPO-PSI mzML standard mass spectrometry data format
* supports HUPO-PSI mzIdentML 1.1 standard mass spectrometry analysis format
* supports reading directly from many vendor raw data formats (on Windows)
* writes whole LC-MS batches to a single DuckDB database for SQL queries (`msconvert --duckdb`; 64-bit Windows and Linux)
* modern C++ techniques and design principles
* cross-platform with native compilers (MSVC on Windows, gcc on Linux, darwin on OSX)
* modular design, for testability and extensibility
* framework for rapid development of data analysis tools
* open source license suitable for both academic and commercial projects (Apache v2)

## Official build status

| OS      | Status |
| ------- | ------ |
| Windows | ![Windows status](https://img.shields.io/teamcity/https/teamcity.labkey.org/s/bt83.svg?label=VS%202022) |
| Native Linux | ![Linux status](https://img.shields.io/teamcity/https/teamcity.labkey.org/s/bt17.svg?label=GCC%204.9) |
| Wine Linux | ![Docker-Wine status](https://img.shields.io/teamcity/https/teamcity.labkey.org/s/ProteoWizardAndSkylineDockerContainerWineX8664.svg?label=Docker-Wine) |

Click [here](https://proteowizard.sourceforge.io/download.html) to visit the official download page.

### Unofficial toolsets
![Unofficial toolset build status](https://github.com/ProteoWizard/pwiz/actions/workflows/build_and_test.yml/badge.svg)
| OS      | Toolset    |
| ------- | -------    |
| Linux   | GCC 13    |
| ~~OS X~~    | ~~Clang 12~~   |

## Developer quickstart (Cursor)

This repository uses native builds (MSVC on Windows, GCC on Linux) and Boost.Build/Jamfiles. For a fast local build on Windows:

- Open a PowerShell or Developer Command Prompt
- Run: `quickbuild.bat`

Alternative entry points:

- Open `pwiz.sln` in Visual Studio 2022 and build the solution
- Use `quickbuild.sh` on Linux/macOS

Key locations:

- C++ libraries and tools: `pwiz/`, `pwiz_tools/`, `pwiz_aux/`
- Command-line apps (e.g., msconvert): `pwiz_tools/commandline/`
- MSVC build output (after quickbuild): `build-nt-x86/msvc-release-x86_64/`
- Third-party deps and Boost.Build: `libraries/`

Common tasks:

- Clean build outputs: `clean.bat`
- Build quickly with defaults: `quickbuild.bat`
- Documentation entry point: `doc/index.html`

### Skyline development

Skyline lives under `pwiz_tools/Skyline` and depends on `pwiz_tools/Shared` and the full ProteoWizard tree. Always work from a full checkout of this repository, not just the `Skyline` subtree.

- Build entire repo (recommended first step):

```bat
bs.bat
```

This calls the app toolset build (e.g., `pwiz_tools\build-apps.bat 64 --i-agree-to-the-vendor-licenses toolset=msvc-14.3 %*`) to build ProteoWizard libraries, command-line tools, and Skyline.

- Open Skyline in VS: `pwiz_tools/Skyline/Skyline.sln`
- Ensure `.NET` Developer Pack is installed if prompted

Full setup and troubleshooting guide: [How to Build Skyline](https://skyline.ms/wiki/home/software/Skyline/page.view?name=HowToBuildSkylineTip).

Skyline C# coding conventions: see `STYLEGUIDE.md`.

### Threading Guidelines

The project avoids `async`/`await` and .NET Task support in favor of deterministic threading:

- **Use `CommonActionUtil.RunAsync()`** (in Shared projects) or `ActionUtil.RunAsync()` (in Skyline) instead of `Task.Run()` or `async`/`await`
- **Avoid .NET thread pool** - Use allocated threads for more deterministic behavior and easier debugging
- **Prefer synchronous operations** on background threads when possible
- **Thread marshaling** - Use `Invoke()` for UI thread operations from background threads

Executables note: Projects under `pwiz_tools/Skyline/Executables` are separate solutions (most build stand-alone EXEs or developer tools, some ship with Skyline). They are not built by `Skyline.sln`, but should generally follow the same coding conventions unless a local project override is required. See the Tool Store: https://skyline.ms/tools.url

EditorConfig: Repository-wide `.editorconfig` enforces core C# naming/formatting so separate solutions (including `pwiz_tools/Skyline/Executables`) inherit consistent style in Visual Studio.

Notes for AI/code assistants (Cursor):

- Prefer invoking `quickbuild.bat` on Windows; avoid ad-hoc compiler calls
- Do not reformat unrelated code; keep original indentation and spacing
- Use existing Jamfiles/solution instead of introducing new build systems
- When adding C++ files, update the appropriate Jamfile or Visual Studio project as needed