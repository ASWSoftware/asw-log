# Configuration reference

Every logger takes a `ASWLog::TASWLogConfig` (`ASWLog_Config.h`) in `Initialize()`. Its settings are grouped by
concern, and a logger ignores the groups it doesn't use, so one config can be passed to several loggers (a multi-log
passes its config on to the loggers it holds).

```cpp
ASWLog::TASWLogConfig config;
config.InitialMinimumLevel = ASWLog::Level::Debug;
config.File.FolderPath = "logs";
config.File.FilePath = "myapp.txt";
config.File.EnableRotation = true;
config.ApplyEnvironment(); // Optional: lets ASWLOG_* environment variables override the settings (see below)

auto& log = ASWLog::TASWFileLog::GetInstance();
log.Initialize(config);
```

To change the settings of a running logger, copy its current config, change the copy, and pass it to `Reconfigure()`,
which is safe while other threads log:

```cpp
auto config = *log.GetConfig();
config.Line.ShowThreadId = false;
log.Reconfigure(config);
```

`Reconfigure()` doesn't change the minimum level (use `SetMinimumLevel()`) and doesn't write the startup lines again.

The header comments in `ASWLog_Config.h` have the full details of each setting; this page is the overview.

## Which logger uses what

| Group | Used by |
|---|---|
| `InitialMinimumLevel` | Every logger, including the multi-log (its own pre-filter) |
| `Line`, `Startup`, `Shutdown`, `Async` | The text loggers: file, console, debugger, memory, syslog, Windows Event Log, and loggers derived from `TASWTextLogBase` |
| `File` | `TASWFileLog` |
| `Backtrace` | Every logger except the multi-log (each of its loggers keeps its own), the category logger and the null logger |
| `Memory` | `TASWMemoryLog` |
| `Syslog` | `TASWSyslogLog` |
| `EventLog` | `TASWWindowsEventLog` |
| `OnBeforeWrite` | Every logger; a multi-log calls its own once per entry, before passing the entry on |
| `OnLogEntry`, `OnLogEntryMinimumLevel` | The text loggers |
| `OnError`, `ErrorReportInterval` | Every logger; a multi-log passes them on, and each of its loggers reports its own failures |

A category logger (`TASWCategoryLog`) and the null logger (`TASWNullLog`) take no config: the category's output
belongs to the logger it wraps.

## Top level

| Setting | Default | What it does |
|---|---|---|
| `InitialMinimumLevel` | `Level::Info` | The minimum level `Initialize()` sets. Change it later with `SetMinimumLevel()`; this field doesn't follow it. |
| `OnBeforeWrite` | empty | Called for each entry that passes the level checks, before it is written: return `false` to drop it, or change it with `entry.SetMessage()`, `SetField()` or `RemoveField()` (e.g. to redact secrets). Runs on the logging thread, before the logger's lock, possibly on several threads at once. |
| `OnLogEntry` | empty | Called after an entry at or above `OnLogEntryMinimumLevel` was written, with the record and the line as written (e.g. for alerting). Runs outside the logger's lock; with `Async.Enabled`, on the logger's own thread. Exceptions are swallowed. |
| `OnLogEntryMinimumLevel` | `Level::Error` | The level an entry needs for `OnLogEntry`. `Level::Off` = never. |
| `OnError` | empty | Receives the logger's internal failures (a file that can't be opened, written, flushed or rotated, an invalid setting, ...) as a `TASWLogError`. Empty: each is written to stderr as one line. Runs outside the logger's lock. |
| `ErrorReportInterval` | 1 minute | Each kind of failure is reported at most once per interval; the next report says how many were left out. 0 = report every failure. |

## `Line`: the line layout (`TASWLineConfig`)

| Setting | Default | What it does |
|---|---|---|
| `Formatter` | empty | Formats each line. Empty: the built-in layout (`TASWTextFormatter`) from the `Show*` settings. Assign a `TASWPatternFormatter` (e.g. `"{time} {level:5} {message}"`), a `TASWJSONFormatter` (JSON Lines) or your own `IASWLogFormatter`. One formatter can be shared by several loggers. |
| `Ending` | `LineEnding::LF` | Added after each formatted line (`LF` or `CRLF`); not after `LogRaw()` entries written as they are. |
| `Multiline` | `MultilineMode::Preserve` | The line breaks inside an entry: written as they are (`Preserve`), each followed by `    \| ` (`Indent`), or as `\r` and `\n` (`Escape`). |
| `TimestampZone` | `TimeZone::UTC` | `UTC` (e.g. `2026-09-28T21:02:44.342Z`) or `Local` (e.g. `2026-09-28T16:02:44.342-05:00`). Also sets when daily rolling starts a new file and the time in backup names. |
| `TimestampPrecision` | `TimePrecision::Milliseconds` | `Milliseconds`, `Microseconds` or `Nanoseconds`. |
| `ShowTimestamp` | `true` | The entry's time. |
| `ShowLevel` | `true` | The entry's level, e.g. `[INFO]`. |
| `ShowCategory` | `true` | The entry's category, e.g. `[Net]`, if it has one. |
| `ShowProcessId` | `true` | The OS process id. |
| `ShowThreadId` | `true` | The OS id of the thread that logged the entry. |
| `ShowWorkingSet` | `false` | The process's memory use. |
| `ShowPeakWorkingSet` | `false` | The process's peak memory use. |
| `ShowFunctionName` | `false` | The function that logged the entry. |
| `ShowSourceLine` | `false` | The source file and line that logged the entry. |
| `ShowFields` | `true` | The entry's fields (its own and its scopes'), if it has any. |

The `Show*` settings are read by the built-in layout and by `TASWJSONFormatter`; a pattern decides its own fields.

## `Startup`: the lines `Initialize()` writes (`TASWStartupConfig`)

| Setting | Default | What it does |
|---|---|---|
| `ApplicationVersion` | empty | The application's version, written on the application line as `app_version='...'`. Empty: on Windows, the executable's file version from its version resource (e.g. `2.3.1.45`, set in RAD Studio under Project > Options > Application > Version Info), if it has one; nothing on Linux. |
| `Banner` | empty | A line written first, if set. |
| `WriteLoggerVersion` | `true` | The ASWLog version and the logger's class, right after the banner, e.g. `ASWLog: version=1.0.0, logger=TASWFileLog`, so a log says which ASWLog wrote it. |
| `WriteApplicationInfo` | `true` | The executable's path, the application's version (see `ApplicationVersion`) and the build target, e.g. `App: app_exe='C:\MyApp\MyApp.exe', app_version='2.3.1.45', app_target=Win64`. |
| `WriteCommandLine` | `false` | The command line, added to the application line. |
| `WriteDriveInfo` | `true` | The disk's total, free and used space. |
| `WriteMemoryUsage` | `true` | The process's memory use (working set and peak). |
| `WriteOSInfo` | `true` | The operating system, its version and the architecture. |
| `WriteSystemMemoryInfo` | `true` | The system's total and available memory. |
| `WriteTimeInfo` | `true` | The time in UTC, and in local time with its offset. |

## `Shutdown`: the lines at shutdown and on a crash (`TASWShutdownConfig`)

| Setting | Default | What it does |
|---|---|---|
| `WriteLine` | `true` | "Logger shutdown: <time>" when the logger is finalized (its destructor, or process exit for a `GetInstance()` singleton), followed by the banner if set. |
| `Banner` | empty | A line after the shutdown line. |
| `WriteCrashLine` | `true` | A Critical "Crash: <reason>" line when the application crashes (with the crash handlers installed, see `ASWLog_CrashHandler.h`). The logger is flushed on a crash either way. |

## `File`: the log file (`TASWFileConfig`)

| Setting | Default | What it does |
|---|---|---|
| `FolderPath` | `"logs"` | The folder of the log file, relative to the working directory unless absolute. |
| `FilePath` | `"aswlog.txt"` | The log file's name, inside `FolderPath`, or a path of its own if absolute. `ResolvePath()` gives the result. |
| `AutoOpenClosePerWrite` | `false` | Opens and closes the file for every entry. Required when several processes write to one log file. |
| `Flush` | `FlushMode::EveryWrite` | When the file's buffer is flushed: after every entry (`EveryWrite`), after every entry that ends a line (`OnNewLine`, so a `LogRaw()` piece without a line break waits), only on `Flush()` or when the buffer is full (`Manual`), or every `FlushInterval` by the logger's own thread (`Periodic`). See [Guarantees.md](Guarantees.md#crash-durability). |
| `FlushInterval` | 1000 ms | `FlushMode::Periodic`'s interval. 0 or less = every entry. |
| `FlushImmediatelyAtLevel` | `Level::Error` | An entry at or above this level is flushed as soon as it is written, whatever `Flush` says. `Level::Off` = only `Flush` decides. |
| `SyncToDiskAtLevel` | `Level::Off` | An entry at or above this level is also synced to disk (`FlushFileBuffers`/`fsync`), so it survives a power loss. A sync often takes milliseconds: keep it for rare entries. |
| `OpenRetryCount` | 5 | How often opening the file is tried before an entry is dropped (e.g. while another program holds the file). |
| `OpenRetryDelay` | 50 ms | The wait between those tries. |
| `CircuitBreakerResetDelay` | 500 ms | After the file couldn't be reopened, entries are dropped without trying again until this long after the last failure. 0 = try on every entry. |
| `EnableRotation` | `false` | Renames the file to a backup when it reaches `MaxFileSizeBytes`. |
| `MaxFileSizeBytes` | 10 MB | The size that triggers a rotation. |
| `RotationRetryDelay` | 500 ms | After a failed rotation, entries go on to the current file, and rotating isn't tried again until this long after the failure. |
| `EnableDailyRolling` | `false` | Rotates the file at midnight (in `Line.TimestampZone`), and at `Initialize()` if the file was last written on an earlier day. |
| `OnBackupCreated` | empty | Called for each backup a rotation makes, with its path and reason, e.g. to compress, upload or move it. Runs outside the logger's lock, on the thread whose call rotated the log. |
| `RetentionMaxAge` | 0 (off) | After each rotation, deletes this log's backups older than this. |
| `MaxBackupFiles` | 0 (unlimited) | After each rotation, keeps only the newest this many backups. |
| `MaxBackupTotalBytes` | 0 (unlimited) | After each rotation, keeps only the newest backups that together take at most this many bytes. Set it above `MaxFileSizeBytes`. |

Backups are named `<stem>.<reason>.<time>.bak`, e.g. `myapp.size.2026-10-10_143005_123.bak`, and a name is never
reused. `TASWFileLog::DeleteOldLogs()` deletes other old files by a wildcard pattern.

## `Async`: asynchronous writing (`TASWAsyncConfig`)

| Setting | Default | What it does |
|---|---|---|
| `Enabled` | `false` | The logging call formats the entry and queues it; the logger's own thread writes the queued entries in order. |
| `QueueCapacity` | 8192 | How many entries can wait in the queue. |
| `OverflowPolicy` | `AsyncOverflowPolicy::Block` | When the queue is full: the call waits for room (`Block`), or the entry is dropped and the drops are counted and reported (`DropNewest`). |
| `WaitAtLevel` | `Level::Error` | The call of an entry at or above this level returns only once the entry (and every entry queued before it) is written and flushed, and such an entry is never dropped. `Level::Off` = no call waits. |

## `Backtrace`: recent entries below the minimum level (`TASWBacktraceConfig`)

| Setting | Default | What it does |
|---|---|---|
| `Capacity` | 0 (off) | How many entries below the minimum level are kept in memory; the oldest goes when it is full. |
| `LowestLevel` | `Level::Trace` | Entries below it aren't kept. |
| `DumpAtLevel` | `Level::Error` | Writing an entry at or above this level writes the kept entries first. `Level::Off` = only `DumpBacktrace()` and a crash write them. |

## `Memory`: what a memory logger keeps (`TASWMemoryConfig`)

| Setting | Default | What it does |
|---|---|---|
| `MaxLines` | 1000 | The most lines kept; the oldest go first. 0 = no limit. |
| `MaxBytes` | 0 (no limit) | The most bytes of text kept. A line longer than this on its own is cut, ending with `" [cut]"`. |

## `Syslog`: the syslog logger (`TASWSyslogConfig`, Linux)

| Setting | Default | What it does |
|---|---|---|
| `Ident` | empty | The name syslog shows, e.g. `myapp` in `myapp[1234]: ...`. Empty: the executable's file name. Process-wide: the syslog logger initialized last sets it. |
| `Facility` | `SyslogFacility::User` | The facility each message is sent with. |

## `EventLog`: the Windows Event Log logger (`TASWEventLogConfig`, Windows)

| Setting | Default | What it does |
|---|---|---|
| `Source` | empty | The event source Event Viewer shows, in the Application log. Empty: the executable's file name without its extension. Register it once with `TASWWindowsEventLog::RegisterSource()` (needs administrator rights) for plain messages. |
| `EventIds` | 1000 to 1005 | The event ID of each level's entries, indexed by level (Trace first). |

## Settings from environment variables

`config.ApplyEnvironment()` reads these variables, named with a prefix (`ASWLOG_` by default), and their values
replace what the code set. Nothing reads the environment unless the application calls it. Call it after setting
`OnError` and before `Initialize()`. A variable that isn't set or is empty changes nothing, and neither does an invalid
value, which is reported to `OnError` (or stderr) as `ErrorKind::InvalidSetting`. Keys and values ignore case. Each
logger can have its own prefix, e.g. `config.ApplyEnvironment("MYAPP_CONSOLE_")`. `config.ApplySetting(key, value)`
sets one of the same settings, e.g. from a command line.

| Variable | Values | Setting |
|---|---|---|
| `ASWLOG_LEVEL` | `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Critical` or `Off` | `InitialMinimumLevel` |
| `ASWLOG_CATEGORIES` | Category levels, e.g. `*=Info,Net=Debug,Net.Http=Trace` | `TASWCategoryLog::ApplyLevels()` (process-wide: use it under one prefix only) |
| `ASWLOG_FOLDER` | A folder (UTF-8) | `File.FolderPath` |
| `ASWLOG_FILE` | A file name or path (UTF-8) | `File.FilePath` |
| `ASWLOG_FLUSH` | `EveryWrite`, `OnNewLine`, `Manual` or `Periodic` | `File.Flush` |
| `ASWLOG_SYNC_AT_LEVEL` | A level | `File.SyncToDiskAtLevel` |
| `ASWLOG_ASYNC` | `1`, `true`, `yes`, `on`; `0`, `false`, `no`, `off` | `Async.Enabled` |
| `ASWLOG_BACKTRACE` | How many entries to keep; 0 = none | `Backtrace.Capacity` |
| `ASWLOG_TIME_ZONE` | `UTC` or `Local` | `Line.TimestampZone` |
| `ASWLOG_TIME_PRECISION` | `ms`, `us` or `ns` (or `Milliseconds`, ...) | `Line.TimestampPrecision` |
| `ASWLOG_MULTILINE` | `Preserve`, `Indent` or `Escape` | `Line.Multiline` |
| `ASWLOG_SHOW_FUNCTION` | As `ASWLOG_ASYNC` | `Line.ShowFunctionName` |
| `ASWLOG_SHOW_SOURCE` | As `ASWLOG_ASYNC` | `Line.ShowSourceLine` |
| `ASWLOG_FORMAT` | `Text` (the built-in layout) or `JSON` | `Line.Formatter` |
| `ASWLOG_PATTERN` | A `TASWPatternFormatter` pattern, e.g. `{time} {message}` (applied after `FORMAT`) | `Line.Formatter` |

For example, to see everything a deployed application logs, without rebuilding it:

```text
set ASWLOG_LEVEL=Trace
myapp.exe
```

## Settings on the logger itself

Some settings belong to the logger object rather than its config, and can be changed from any thread at any time:

| Method | What it does |
|---|---|
| `SetMinimumLevel(level)` | The level an entry needs to be written (lock-free). |
| `SetEnabled(enabled)` | Turns all writing off and on, forced entries included, keeping the output open (lock-free). |
| `TASWConsoleLog::SetColorMode(mode)` | `ColorMode::Auto` (color only on a console or terminal that shows it, unless `NO_COLOR` is set), `Always` or `Never`. |
| `TASWCategoryLog::SetMinimumLevel(level)` | A category's own level, which replaces the wrapped logger's for that category's entries. |
