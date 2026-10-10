# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Versions before 0.26.1 are not itemized here;
see [0.26.1](#0261---2026-09-17) for the initial versioned baseline.

## [Unreleased]

### Added

- Local-time and finer timestamps: `Line.TimestampZone` (`TimeZone::UTC`,
  the default, or `TimeZone::Local`) and `Line.TimestampPrecision`
  (`TimePrecision::Milliseconds`, the default, `Microseconds` or
  `Nanoseconds`), e.g. `2026-09-28T16:02:44.342519-05:00` for local time
  with microseconds. The digits past the system clock's resolution are 0
  (100 ns with MSVC and MinGW, 1 us with RAD Studio's libc++). The zone also
  decides when daily rolling starts a new file (local midnight with
  `Local`), the time in backup names, and the time in the "Logger shutdown"
  line; a `Reconfigure()` that changes it rolls over next at the new zone's
  midnight. The fixed-layout crash line (in a POSIX signal handler, or when
  the logger's lock stays busy) stays UTC with milliseconds. Local time
  costs a `localtime_s`/`localtime_r` call per line. `Time::WriteISO8601()`
  and `Time::ToISO8601String()` take the zone and precision too, and
  `Time::ToDateString()` the zone.
- Categories with their own levels: `TASWCategoryLog` (new
  `ASWLog_CategoryLog.h`) wraps any logger, e.g.
  `static ASWLog::TASWCategoryLog NetLog("Net", ASWLog::TASWFileLog::GetInstance());`,
  and passes its entries on with the category's name, which the built-in
  layout shows after the level (`[2026-10-08T14:31:00.217Z][DEBUG][Net]...`;
  hide it with the new `Line.ShowCategory`). Entries without a category
  are written as before. A category follows the wrapped logger's minimum
  level until `SetMinimumLevel()` gives it one of its own, which then
  replaces the wrapped logger's for that category's entries, both ways: with
  the file logger at Info, a `Net` category at Debug writes its Debug
  entries and a `Db` category at Warn skips its Info entries (the backtrace
  still keeps them; behind a multi-log, each logger still applies its own
  level). `ResetMinimumLevel()` makes it follow again, and
  `SetEnabled(false)` silences only that category. A category wrapping
  another one is named after both, e.g. `Net.Http`, and inherits its level.
  `GetConfig()`, `Flush()`, `IsOpen()` and `DumpBacktrace()` pass on to the
  wrapped logger; `Initialize()`, `Reconfigure()`, `Open()` and `Close()` do
  nothing and return false. `TASWLogRecord` gains `Category` and
  `CategoryLevel`, which `OnLogEntry` and custom formatters see, and the
  fixed-layout crash and backtrace lines show the category too. Breaking
  for a class that implements `IASWLog` directly: it must now implement
  `ShouldLog(const TASWLogRecord&)` (classes derived from `TASWLogBase` get
  it), which tells a category whether an entry with its level would be
  written. `TASWLogRecord`'s `Raw` and `Forced` now come right after
  `LogLevel`, so designated initializers must follow the new order.
- Line patterns: `TASWPatternFormatter` writes each line from a pattern,
  e.g. `config.Line.Formatter = std::make_shared<const ASWLog::TASWPatternFormatter>("{time} {level:5} {[category] }{message}");`
  for `2026-10-08T14:31:00.217Z DEBUG [Net] connected`. The placeholders are
  `{time}` (in `Line.TimestampZone` and `Line.TimestampPrecision`),
  `{level}`, `{category}`, `{pid}`, `{tid}`, `{ws}`, `{pws}`, `{function}`,
  `{file}` (the name without folders), `{line}` and `{message}`; `{{` and
  `}}` write a brace. `{level:5}` pads a field to a minimum width. Text
  inside the braces around the name is written only when the field isn't
  empty: punctuation and spaces as they are (`{[category] }`), anything else
  in single quotes, with `''` for a quote (`{'cat=' category ' '}`). The
  pattern is parsed once, when the formatter is made, and an invalid one
  throws `std::invalid_argument` naming the problem and where it is. The
  `Line.Show*` options don't apply to a pattern, and the logger still adds
  `Line.Ending` after each line.
- JSON Lines output: `TASWJSONFormatter` writes each entry as a JSON object
  on one line, e.g.
  `config.Line.Formatter = std::make_shared<const ASWLog::TASWJSONFormatter>();`
  for
  `{"time":"2026-10-08T14:31:00.217Z","epoch_ms":1791469860217,"level":"DEBUG","category":"Net","pid":4120,"tid":7788,"message":"connected"}`.
  It writes the fields that the `Line.Show*` options turn on, in the
  built-in layout's order: `time` (in `Line.TimestampZone` and
  `Line.TimestampPrecision`) with `epoch_ms` (whole milliseconds since 1970
  UTC), `level`, `category`, `pid`, `tid`, `ws`, `pws`, `function`, `file`
  and `line`, then always `message`. Texts are escaped as RFC 8259 asks,
  and invalid UTF-8 is replaced with U+FFFD, so a multi-line message stays
  on its line. A `LogRaw()` entry gets an object of its own with
  `"raw":true` instead of being written as is, and the startup, shutdown
  and crash lines are objects too, including the fixed-layout crash line
  (in a POSIX signal handler, or when the logger's lock stays busy). A
  custom formatter can format raw entries too by overriding the new
  `IASWLogFormatter::FormatsRawEntries()` (false by default), and
  `JSON::AppendString()` writes a text as an escaped JSON string.
- Structured fields and scoped context (new `ASWLog_Fields.h`; add
  `ASWLog_Fields.cpp` to projects that list the ASWLog sources). Every
  logging method has an overload that takes fields for that entry, e.g.
  `logger.LogInfo("Order placed", {{"orderId", 17}, {"total", 9.99}})`,
  and every `*Fmt` method one with the fields first, e.g.
  `logger.LogInfoFmt({{"orderId", id}}, "Order {} placed", id)`. A value
  is a signed or unsigned integer, a double, a bool or text, viewed rather
  than copied, so the call doesn't allocate; characters, enums and
  pointers other than to text are rejected at compile time. A
  `TASWLogScope`, e.g. `ASWLog::TASWLogScope scope{{"requestId", id}};`,
  adds its fields to every entry the thread logs while it exists, also in
  the functions it calls; scopes nest, and each key is written once, the
  entry's own value winning over a scope's and an inner scope's over an
  outer one's. Scopes stay on their thread: `TASWLogScope::Capture()`
  copies the current ones into a `TASWLogContext`, and
  `TASWLogScope scope(context);` applies it on another thread, e.g. in a
  thread pool. The built-in layout writes the fields before the message,
  `[requestId="8f3a" orderId=17 total=9.99]: Order placed`, text always
  in quotes and escaped (hide them with the new `Line.ShowFields`); the
  pattern formatter has `{fields}`, and the JSON formatter writes a
  `"fields"` object (a NaN or infinite double as the string `"NaN"`,
  `"Infinity"` or `"-Infinity"`). A `LogRaw()` entry is still written as
  is by the text layouts. The backtrace, an asynchronous logger's
  `OnLogEntry` and the fixed-layout crash lines keep the fields, and the
  crash line gets the crashing thread's scope fields. `TASWLogRecord`
  gains `Fields` and `Scope` (both pointers, so a filtered call costs no
  more), read with `ForEachField()`.
- Multi-line message handling: `Line.Multiline` (`MultilineMode::Preserve`,
  the default, writes line breaks as they are, as before). With
  `MultilineMode::Indent`, each `\n` or `\r\n` in a formatted line is
  written as the line ending followed by `    | ` (`    |` for an empty
  line), so every line after an entry's first is marked as part of it and
  can't pass for an entry of its own; a lone `\r` stays as it is. With
  `MultilineMode::Escape`, each CR and LF is written as the two characters
  `\r` and `\n` (other characters, backslashes included, as they are), so
  every entry is one line. It applies to the line of any formatter,
  including your own (a JSON line has no line breaks), and to the
  fixed-layout crash and backtrace lines; `LogRaw()` entries written as is
  stay as they are.
- A hook to drop or change entries before they are written:
  `OnBeforeWrite`, e.g. to redact secrets or personal data, or to drop
  noisy entries by content, category or field. It gets a `TASWPendingEntry`
  for each entry that passes the level checks (`LogRaw()` entries
  included): return false to drop it, or change it with `SetMessage()`,
  `SetField()` and `RemoveField()` (read with `GetRecord()`, `FindField()`
  and `ForEachField()`); level, time and category stay as they are. It runs
  on the logging thread before the logger's lock or queue, so it may log
  elsewhere; an entry logged from inside it is written as is. Entries the
  backtrace keeps are kept as changed, `OnLogEntry` sees the changed entry,
  and a hook that throws drops the entry and reports it (`OnError`). The
  logger's own lines (startup, shutdown, backtrace markers, crash lines)
  don't pass through it. A multi-log runs its hook once per entry and
  passes its config on without it.
- `TASWNullLog` (new `ASWLog_NullLog.h`): a logger that writes nothing,
  e.g. to turn logging off behind an `IASWLog&` or to pass to code under
  test; `TASWNullLog::GetInstance()` gives a shared one, e.g. for a default
  argument. No logging call formats or copies anything (`ShouldLog()` and
  `IsEnabled()` are always false), while the lifecycle methods succeed,
  `GetConfig()` returns the config last passed in, and the minimum level
  reads back as set.
- `TASWMemoryLog` (new `ASWLog_MemoryLog.h`): a text logger that keeps its
  newest lines in memory, formatted like a file logger's and without the
  line ending, e.g. for an in-app log viewer, a crash report or tests of
  code that logs. The new `TASWLogConfig::Memory` group sets how much it
  keeps: `MaxLines` (default 1000) and `MaxBytes` (default 0, no limit; a
  single longer line is cut to fit, ending with " [cut]"); the oldest lines
  go first. `GetLines()`, `GetLineCount()` and `Clear()` read and clear
  them, and `GetLinesSince(n)` returns only the lines numbered n and up
  (every kept line gets the next sequence number), with the number to ask
  for next and how many lines were missed, so a viewer can poll for new
  lines.
- `TASWDebuggerLog` (new `ASWLog_DebuggerLog.h`): writes each line to the
  debugger's output with `OutputDebugStringW` on Windows, shown in the RAD
  Studio Event Log, Visual Studio's Output window and DebugView (even
  without a debugger; each line then still costs some microseconds, so add
  it where wanted, e.g. in debug builds). `TASWDebuggerLog::GetInstance()`
  gives the shared one. Other platforms have no debugger output: there the
  class builds but writes nothing (`ShouldLog()` is false and no line is
  formatted), so the same code can add it everywhere.
- `TASWSyslogLog` (new `ASWLog_SyslogLog.h`): sends each line to the system
  log with `syslog()` on Linux (the journal on systemd hosts), without its
  line ending, the level as the priority (Trace and Debug `LOG_DEBUG`, Info
  `LOG_INFO`, Warn `LOG_WARNING`, Error `LOG_ERR`, Critical `LOG_CRIT`).
  The new `TASWLogConfig::Syslog` group sets the ident (`Ident`, empty =
  the executable's file name; passed to `openlog()` with `LOG_PID`, so it
  is process-wide) and the facility (`Facility`, a new `SyslogFacility`:
  `User`, the default, `Daemon` or `Local0` to `Local7`; sent with each
  message). The line is formatted as configured, so consider turning off
  `Line.ShowTimestamp` and `Line.ShowProcessId`, which syslog adds itself.
  `TASWSyslogLog::GetInstance()` gives the shared one. Windows has no
  syslog: there the class builds but writes nothing (`ShouldLog()` is false
  and no line is formatted).
- `TASWWindowsEventLog` (new `ASWLog_WindowsEventLog.h`): reports each line
  to the Windows Event Log (Application log), without its line ending: Trace,
  Debug and Info as Information events, Warn as Warning, Error and Critical
  as Error. The new `TASWLogConfig::EventLog` group sets the event source
  (`Source`, empty = the executable's file name without its extension) and
  the event ID per level (`EventIds`, by default 1000 for Trace to 1005 for
  Critical). A line longer than `TASWWindowsEventLog::MaxMessageSize`
  (31,839 bytes) is cut to fit, ending with ` [cut]`. Each event takes
  about a hundred microseconds, so it suits warnings and errors.
  `TASWWindowsEventLog::RegisterSource()` and `UnregisterSource()` register
  a source (administrator rights, e.g. in an installer) with the .NET
  Framework's `EventLogMessages.dll`, so that Event Viewer shows the text
  without "The description for Event ID ... cannot be found".
  `TASWWindowsEventLog::GetInstance()` gives the shared one. Other platforms
  have no Event Log: there the class builds but writes nothing.
- Rate-limited logging (new `ASWLog_Limiter.h`): `ASWLOG_ONCE(log, level,
  ...)`, `ASWLOG_EVERY_N(log, n, level, ...)` and `ASWLOG_EVERY_INTERVAL(log,
  interval, level, ...)` log only the first, every nth, or at most one entry
  per interval at the place they are written, e.g.
  `ASWLOG_EVERY_INTERVAL(log, std::chrono::seconds(5), ASWLog::Level::Warn,
  "Retrying {}", host);`. The arguments after the level are those of
  `LogFmt()` (format string, arguments, optional fields). Only entries the
  logger would write count, and only those are formatted. After skipped calls,
  `ASWLOG_EVERY_INTERVAL` adds a `suppressed` field with their number. The
  macros keep a lock-free `TASWLogLimiter` (`Once()`, `EveryN()`,
  `EveryInterval()`) in a static where they are written; the class can also be
  used on its own.
- Settings from environment variables, e.g. to turn on full verbosity in a
  deployed release build without rebuilding it:
  `config.ApplyEnvironment();` before `Initialize()` reads `ASWLOG_LEVEL`,
  `ASWLOG_CATEGORIES`, `ASWLOG_FOLDER`, `ASWLOG_FILE`, `ASWLOG_FLUSH`,
  `ASWLOG_SYNC_AT_LEVEL`, `ASWLOG_ASYNC`, `ASWLOG_BACKTRACE`,
  `ASWLOG_TIME_ZONE`, `ASWLOG_TIME_PRECISION`, `ASWLOG_MULTILINE`,
  `ASWLOG_SHOW_FUNCTION`, `ASWLOG_SHOW_SOURCE`, `ASWLOG_FORMAT` (`Text` or
  `JSON`) and `ASWLOG_PATTERN` (a `TASWPatternFormatter` pattern), and their
  values replace what the code set (see `TASWLogConfig::ApplyEnvironment()`
  for the values; case doesn't matter). Nothing reads the environment unless
  the application calls it, and each logger can have its own prefix, e.g.
  `ApplyEnvironment("MYAPP_CONSOLE_")`. A variable that isn't set or is
  empty changes nothing; an invalid value changes nothing either and is
  reported to `OnError` (or stderr) as the new `ErrorKind::InvalidSetting`.
  `ApplySetting(key, value)` sets one of the same settings, e.g. from a
  command line or a settings file. `ASWLOG_CATEGORIES`, e.g.
  `*=Info,Net=Debug,Net.Http=Trace`, sets category levels through the new
  `TASWCategoryLog::ApplyLevels()`: a name covers that category and those
  under it (`Net` covers `Net.Http`), `*` covers all, and the longest name
  wins; it applies process-wide, to the categories that exist and to those
  made later, and a spec with an invalid item changes nothing. On Windows,
  the values are read as UTF-8 (paths included). Breaking only for code that
  lists every `ErrorKind` (e.g. a `switch`) or relies on `ErrorKindCount`
  being 9.
- Wide-string logging (new `ASWLog_Unicode.h`; add `ASWLog_Unicode.cpp` to
  projects that list the ASWLog sources). Every logging method also takes a
  message of wide text, `wchar_t` (e.g. `L"Opened"`, a `std::wstring`, or
  C++Builder's `UnicodeString::c_str()`) or `char16_t` (e.g. `u"Opened"`),
  converted to UTF-8 only if the entry is used, so a filtered call converts
  nothing; field keys and texts stay UTF-8. Every `*Fmt` method also takes a
  `wchar_t` format string, checked at compile time like a narrow one, e.g.
  `log.LogInfoFmt(L"Opened {} ({} bytes)", fileName, size);` (wrap a
  run-time one in `RuntimeFormat()`); its arguments are formatted as wide
  text, so pass UTF-8 text as `ASWLog::UTF8(text)` (a narrow `std::string`
  doesn't compile there). In a narrow `*Fmt` call, `ASWLog::Wide(text)`
  formats wide text, converted only if the entry is formatted, e.g.
  `log.LogInfoFmt("Opened {}", ASWLog::Wide(fileName));`. `WideToUTF8()`
  (from `std::wstring_view` or `std::u16string_view`), `UTF8ToWide()` and
  `UTF8ToUTF16()` convert on any platform (`wchar_t` text is UTF-16 on
  Windows and UTF-32 on Linux); invalid input becomes U+FFFD. `std::format`
  has no `char16_t` format strings, so `char16_t` text goes in as a message
  or through `Wide()`.

### Changed

- Writing a formatted line is much faster: the built-in layout now writes
  the timestamp, level, process and thread ids and memory fields itself
  instead of through `std::format`, which took most of a line's time. A
  `LogInfo` line to a file with `FlushMode::Manual` takes about 0.4 us
  instead of 1.0 us (MinGW and RAD Studio, Windows); the output is
  unchanged. `Time::ToISO8601String()` and `Time::ToDateString()` (which
  daily rolling calls for each entry) use the same code, and the new
  `Time::WriteISO8601()` writes the timestamp into a buffer of
  `Time::ISO8601BufferSize` characters without allocating, e.g. for a
  custom formatter. A time before 1970 is now written as the day and
  millisecond it falls in, instead of a malformed time.

## [0.75.0] - 2026-10-06

### Added

- Optional asynchronous writing for the text loggers (file and console),
  in the new `TASWLogConfig::Async` group (`TASWAsyncConfig`), off by
  default: the default synchronous logger, which has written each entry
  when the call returns, stays the safest for finding the cause of a crash.
  With `Async.Enabled`, a logging call formats the entry and queues it, and
  the logger's own thread writes the queue in order, so the call doesn't
  wait for the file or console. `Async.QueueCapacity` (default 8192
  entries) and `Async.OverflowPolicy` (`AsyncOverflowPolicy::Block`, the
  default, waits for room; `DropNewest` drops the entry) decide what
  happens when the queue is full. A dropped run of entries is reported to
  `OnError` as the new `ErrorKind::EntriesDropped` (`ErrorKindCount` is
  now 9) and marked in the log by a "Dropped N entries" line where they
  would have been. An entry at or above `Async.WaitAtLevel` (default
  `Error`) is never dropped, and its call returns only once it and every
  entry before it are written and flushed, so errors still reach the file
  before a crash. `Flush()`, `Close()`, `Reconfigure()` and the destructor
  first wait for the queued entries. In async mode `OnLogEntry` is called
  on the logger's thread, after the entry was written; `File.Flush` set to
  `EveryWrite` or `OnNewLine` flushes once per batch of queued entries. A
  logger derived from `TASWTextLogBase` gets this without changes;
  `AfterQueuedEntriesUnlocked()` is a new protected hook for flushing a
  batch.
- Optional crash handlers, in the new `ASWLog_CrashHandler.h` (add
  `ASWLog_CrashHandler.cpp` to projects that list the ASWLog sources):
  `ASWLog::InstallCrashHandlers()` hooks `std::terminate()`, SIGABRT
  (`abort()`, a failed `assert()`) and the fatal faults (Windows:
  unhandled structured exceptions such as an access violation or a stack
  overflow; POSIX: SIGSEGV, SIGBUS, SIGFPE, SIGILL). On a crash, every
  initialized text logger waits briefly for its asynchronous queue to be
  written, is flushed, and gets a Critical "Crash: <reason>" line (e.g. the
  uncaught exception's `what()`, or the exception code and address), then
  the crash is passed on to the handler installed before, so error reports
  and core dumps still happen. `TASWCrashHandlerOptions::WaitTimeout`
  (default 1 s) bounds the wait. In a POSIX signal handler the line has a
  fixed layout, written straight to the file; elsewhere it uses the
  logger's formatter. Best effort: the header lists what isn't handled
  (e.g. a debugger attached, fast fail, the process being killed), and a
  logger whose lock stays busy (e.g. the crashed thread held it) gets only
  the fixed-layout line, written straight to its output, and loses its
  buffered entries. `UninstallCrashHandlers()` restores the previous
  handlers; `ASWLog::HandleCrash(reason)` does the logging part for an
  application with its own crash reporter. `Shutdown.WriteCrashLine`
  (default true) leaves a logger's crash line out; the line is synced to
  disk if `File.SyncToDiskAtLevel` is Critical or lower. A logger derived
  from `TASWTextLogBase` is covered without changes; it can override the new
  protected hooks `FlushForCrashUnlocked()` and `WriteCrashLineDirect()` to
  get its crash line in a POSIX signal handler, or when its lock stays busy.
- A backtrace buffer, in the new `TASWLogConfig::Backtrace` group
  (`TASWBacktraceConfig`), off by default: with `Backtrace.Capacity` above 0,
  a logger keeps its most recent entries below its minimum level (from
  `Backtrace.LowestLevel` up, default Trace) in memory, and writes them when
  an entry at or above `Backtrace.DumpAtLevel` (default Error) is written,
  just before it, so a log written at Info still shows the Debug lines that
  led up to an error. They are written with their own time, level and
  location between a "Backtrace: the last N entries below the minimum
  level" and a "Backtrace end" line, then forgotten. The new
  `IASWLog::DumpBacktrace()` writes them on request, and a crash handler
  writes them before the crash line. `ShouldLog()` is now true for the
  levels the backtrace keeps, so the `*Fmt` methods format those entries
  too. Each logger keeps its own backtrace; a multi-log keeps none and
  filters with its own minimum level first, so set that to
  `Backtrace.LowestLevel` or lower when its loggers keep a backtrace.
  Breaking for a class that implements `IASWLog` directly: it must now
  implement `DumpBacktrace()` (classes derived from `TASWLogBase` get it).

### Fixed

- `FlushMode::Periodic` now flushes on a timer: a thread of the file
  logger's own flushes the file every `File.FlushInterval`, so the last
  entries reach the file within that time even when nothing more is logged.
  They used to stay in the buffer until the next entry came after the
  interval, or until the file was closed. Writing an entry no longer reads
  the clock in this mode. The thread runs only while a logger in Periodic
  mode is initialized (not with `File.AutoOpenClosePerWrite`, which flushes
  by closing the file after each entry); `Reconfigure()` starts or stops it
  or applies a new interval at once, and `Close()`, the destructor and the
  exit handler of `GetInstance()` stop it without waiting for the interval.
  A failed flush on that thread goes to `OnError` on that thread. A
  `FlushInterval` of 0 or less flushes every entry, as before. ASWLog now
  needs the threads library: the CMake target `ASWLog::ASWLog` links
  `Threads::Threads`; other Linux builds may need `-pthread`. A logger
  derived from `TASWTextLogBase` can use the same thread through the new
  protected hooks `GetWorkerIntervalUnlocked()` and `OnWorkerWakeUnlocked()`.
- In a ThreadSanitizer build with libstdc++ (GCC's standard library, which
  Clang also uses on most Linux systems), destroying a logger now tells
  ThreadSanitizer that its locks are gone. ThreadSanitizer used to take the
  locks of a logger created later at the same address (e.g. on the stack)
  for the old ones, and could report a lock-order inversion (potential
  deadlock) between loggers that never existed at the same time.

## [0.66.0] - 2026-10-04

### Added

- `TASWLogConfig::OnError`, which reports a logger's internal failures that
  used to be silent: the log file couldn't be opened (or its folder
  created), written, flushed, synced to disk, closed or rotated, backup
  cleanup couldn't delete an old backup (or list its folder), a console
  logger's stdout or stderr failed, or an unexpected exception (e.g. a
  throwing formatter) dropped an entry or the startup lines. Each report is
  a `TASWLogError`: an `ErrorKind`, a message, the path and
  `std::error_code` if any, and `SuppressedCount`. Without a handler, each
  report is written to stderr as one line
  (`TASWLogError::ToString()`). Reports, to `OnError` or to stderr, are
  limited per logger and kind by the new `ErrorReportInterval` (default 1
  minute; 0 = every failure), and each says how many of its kind were left
  out since the previous one. Like `OnLogEntry`, `OnError` is called
  outside the logger's lock and its exceptions are swallowed; a failure the
  handler itself causes isn't reported again. A logger derived from
  `TASWLogBase` reports with `ReportError()` (from a `TASWTextLogBase` hook,
  `ReportErrorUnlocked()`). A `*Fmt` format error isn't reported: the entry
  is still written, with the error in its line.
- `File.MaxBackupFiles` and `File.MaxBackupTotalBytes` (0 = unlimited, the
  default) limit a file logger's backups. After each successful rotation,
  together with `File.RetentionMaxAge`, the oldest backups are deleted until
  at most `MaxBackupFiles` are left and they take at most
  `MaxBackupTotalBytes`; the size limit is strict and deletes even the backup
  just made if it alone is larger, so set it above `MaxFileSizeBytes`.
  Backups are ordered by their last write time, when their newest entry was
  written, so a daily backup counts as newer than the size backups of the
  same day. A backup that can't be deleted is reported to `OnError`.
- `File.OnBackupCreated`, called once for each backup that rotation makes,
  with a `TASWBackupInfo` (the log's path, the backup's path, and the
  reason: "size", "daily", or the tag given to `RotateLogFiles()`), e.g. to
  compress, upload or move the backup. Like `OnError`, it is called outside
  the logger's lock, on the thread whose call rotated the log, so hand slow
  work to another thread; exceptions are swallowed. The backup cleanup
  (`RetentionMaxAge`, `MaxBackupFiles`, `MaxBackupTotalBytes`) now runs
  after it, also outside the lock, so the callback always finds its backup;
  a backup it renames out of the `<stem>.<reason>.<time>.bak` form is left
  to the application. A logger derived from `TASWTextLogBase` can run its own
  work after the lock with `DeferUnlocked()`.
- `File.FlushImmediatelyAtLevel` (default `Level::Error`): a file logger
  flushes an entry at or above this level as soon as it is written, whatever
  `File.Flush` says, so with `Manual`, `Periodic` or `OnNewLine` flushing,
  Error and Critical entries (raw ones too) are in the file if the
  application crashes right after. `Level::Off` leaves flushing to
  `File.Flush` alone, as before. Nothing changes with the default
  `FlushMode::EveryWrite`.
- `File.SyncToDiskAtLevel` (default `Level::Off`): a file logger flushes an
  entry at or above this level and then syncs the file to disk
  (`FlushFileBuffers` on Windows, `fsync` on POSIX), so the entry survives a
  system crash or power loss too. A sync often takes milliseconds, so keep it
  for rare entries. A failed sync is reported to `OnError` as
  `ErrorKind::SyncFailed`. `TASWFileStream::SyncToDisk()` does the sync for
  a custom logger.

### Fixed

- `File.RetentionMaxAge` deleting another log's old backups when the two
  logs share a folder and one name starts with the other's (e.g. `app.log`
  and `app.audit.log`), and any other old file named `<stem>.<anything>.bak`.
  Backup cleanup now only deletes this log's backups, named as rotation
  names them: `<stem>.<reason>.<time>.bak`. A backup made by
  `RotateLogFiles()` with a reason tag that contains `.` isn't recognized,
  so it is kept. Retention now also works for a log in a root folder.

- After a failed write (e.g. a full disk that later had room again), a file
  logger's flushes did nothing until the file was reopened, so entries
  stayed in the file's buffer and `Flush()` kept returning false. Each flush
  now reaches the file and `Flush()` returns its own result.
  `TASWFileStream::Flush()` now returns whether it succeeded.

## [0.65.0] - 2026-10-03

### Added

- `Level::Off`, to turn a logger off through its level:
  `SetMinimumLevel(Level::Off)` (or `InitialMinimumLevel`) stops all
  entries except `LogForce`/`LogForceRaw`, which still ignore the minimum
  level, and `OnLogEntryMinimumLevel = Level::Off` turns off `OnLogEntry`. A
  message logged at `Off` is never written, even when forced.
  `Level_ToString` gives "OFF", and `Level_FromString` accepts "OFF" and
  "NONE". `LevelCount` stays 6 (the severity levels, not counting `Off`).
  Code that switches over every `Level` value must handle `Off`.
- `IASWLog::SetEnabled()`/`IsEnabled()`: a disabled logger writes nothing,
  not even forced entries or the startup and shutdown lines, while its
  output stays open and its level is kept (lock-free, like the level). Use
  it to stop logging; `Close()` releases the output, but with
  `File.AutoOpenClosePerWrite` the next entry reopens the file.
- `GetMinimumLevel()`/`SetMinimumLevel()` and the new `ShouldLog(level)` are
  now on `IASWLog`, so code holding only an `IASWLog&` can use them.
  `ShouldLog()` is true if `Log()` would write an entry at that level (for a
  multi-log, if any of its loggers would); use it to skip building an
  expensive message.
- `IASWLog::Flush()`, so code holding only an `IASWLog&` can push buffered
  entries out, e.g. before a risky operation or with `FlushMode::Manual`.
  The console logger flushes stdout and stderr, and a multi-log flushes
  every logger it holds, returning false if any of them failed. It returns
  false if the output isn't open, works while the logger is disabled, and
  never throws. A custom logger must add `bool Flush() noexcept override`,
  including one deriving from `TASWLogBase` (there is no default); one
  deriving from `TASWTextLogBase` implements the hook
  `bool FlushUnlocked() override` instead, which is called with the lock
  held.
- `IASWLog::Write(const TASWLogRecord&)`, the one method through which a
  logger receives every entry, and `Raw`/`Forced` flags on
  `TASWLogRecord`. A record can be passed on as is, e.g. to another logger.
- `IASWLog::Reconfigure(const TASWLogConfig&)` changes an initialized
  logger's settings safely while other threads log; entries written after it
  returns use them. A file logger whose file path or
  `File.AutoOpenClosePerWrite` changed closes the old file and opens the
  new one (if that fails, `Reconfigure()` returns false, the settings are
  kept and later entries retry the open); otherwise it flushes and keeps the
  file.
  It doesn't change the minimum level (use `SetMinimumLevel()`) or write the
  startup lines. A multi-log passes the settings on to every logger it
  holds, like `Initialize()`.
- `ASWLog_Version.h`, the single source of the version: the macros
  `ASWLOG_VERSION_MAJOR`, `ASWLOG_VERSION_MINOR`, `ASWLOG_VERSION_PATCH`,
  `ASWLOG_VERSION_PRERELEASE` (empty on a release, e.g. `dev.1` between
  releases) and `ASWLOG_VERSION_STRING` (e.g. `1.1.0-dev.1`), usable in
  `#if` to support several ASWLog versions, and the same values as
  `ASWLog::VersionMajor`, `VersionMinor`, `VersionPatch`,
  `VersionPreRelease` and `Version` constants. `GetVersionStr()` returns
  `ASWLOG_VERSION_STRING`. Projects that list the ASWLog sources themselves
  (rather than using the CMake target) must add `ASWLog_Version.cpp` and
  `ASWLog_Version.h`.

### Changed

- The `*Fmt` methods no longer format an entry that wouldn't be written:
  below the minimum level, at `Off`, or with the logger disabled (a forced
  entry is skipped only at `Off` or while disabled). Such an entry also no
  longer reaches the logger's `Log()`, which matters only for a custom
  logger that ignores its level. Code implementing `IASWLog` directly must
  add `IsEnabled`, `SetEnabled`, `GetMinimumLevel`, `SetMinimumLevel` and
  `ShouldLog` (deriving from `TASWLogBase` needs no change). `TASWLogBase`'s
  level is now private: a derived class that set `m_MinimumLevel` must call
  `SetMinimumLevel()` instead.
- The `*Fmt` methods check the format string against their arguments at
  compile time, like `std::format`, so a mismatch such as
  `LogInfoFmt("{} {}", 1)` no longer compiles (it used to log
  `[ASWLog format error: ...]`). A format string that isn't a compile-time
  constant, such as a `std::string` variable, must now be wrapped in the new
  `ASWLog::RuntimeFormat(...)`, which keeps the check at run time and still
  logs the format error on a mismatch. A custom `std::formatter` used with
  them needs a `constexpr` `parse()`, as `std::format` already requires.
  `TASWFormatString` is now a class template.
- Every logging method now builds a `TASWLogRecord` and passes it to
  `Write()`. `Log`, `LogRaw`, `LogForce`, `LogForceRaw` and the level
  shortcuts (`LogTrace` ... `LogCritical`) are no longer virtual, and a
  filtered `LogTrace` etc. is about 3x faster (one virtual call instead of
  two). An entry's time, process id and thread id are read when the call is
  made, on the calling thread, rather than once the logger holds its lock,
  so the time is the moment of the call; all loggers of a multi-log show
  the same time. Daily rolling only moves forward: an entry stamped just
  before midnight that is written after the log rolled over goes into the
  new day's log. Custom loggers must change: one deriving from
  `TASWLogBase` replaces its `Log`/`LogRaw`/`LogForce`/`LogForceRaw`
  overrides with `void WriteRecord(const TASWLogRecord&) override`, which
  gets only entries that pass its enabled and level checks, already
  stamped (use `record.Raw` and `record.Forced`); one implementing
  `IASWLog` directly implements `Write()` and applies those checks itself.
  `OnLogEntry` callbacks now take `(const TASWLogRecord& record,
  std::string_view formattedLine)` instead of `(Level, std::string_view)`;
  the level is `record.LogLevel`.
- Logging never throws into the application, and the signatures now say
  so: `Write()`, the `Log*` methods, the `*Fmt` methods, `Initialize()`,
  `Open()` and `Close()` are `noexcept` (every `IASWLog` method except
  `GetFullVersionStr()`), and `Initialize()`/`Open()`/`Close()` return
  false on an unexpected exception. A startup line whose formatter throws
  no longer makes `Initialize()` throw; the remaining startup lines are
  skipped and the logger is initialized. Custom loggers must declare
  `Initialize`, `Open`, `Close` and (when implementing `IASWLog` directly)
  `Write` overrides `noexcept`; a `WriteRecord()` override may still
  throw, since `TASWLogBase::Write()` drops the entry instead.
- `TASWLogConfig` is regrouped by concern, so it's clear which settings a
  logger uses: `Line` (`TASWLineConfig`, the line layout), `Startup`
  (`TASWStartupConfig`), `Shutdown` (`TASWShutdownConfig`) and `File`
  (`TASWFileConfig`, which the console logger and the multi-log ignore).
  `InitialMinimumLevel` and `OnLogEntry` stay at the top. Defaults and
  behavior are unchanged. Code that sets the config must rename its
  fields (old -> new):
  - `LogUTCDateTime` -> `Line.ShowTimestamp` (it turns the timestamp on
    or off; it is always UTC)
  - `LogLevelStr` -> `Line.ShowLevel`, `LogProcessId` ->
    `Line.ShowProcessId`, `LogThreadId` -> `Line.ShowThreadId`
  - `LogAppMem_WorkingSet` -> `Line.ShowWorkingSet`,
    `LogAppMem_PeakWorkingSet` -> `Line.ShowPeakWorkingSet`
  - `LogMethodName` -> `Line.ShowFunctionName`, `LogSourceLine` ->
    `Line.ShowSourceLine`
  - `LogLineEnding` -> `Line.Ending`, `Formatter` -> `Line.Formatter`
  - `BannerMessage_Init` -> `Startup.Banner`
  - `Init_LogApplicationInfo` -> `Startup.WriteApplicationInfo`,
    `Init_LogCommandLine` -> `Startup.WriteCommandLine`,
    `Init_LogDriveInfo` -> `Startup.WriteDriveInfo`,
    `Init_LogMemoryUsage` -> `Startup.WriteMemoryUsage`,
    `Init_LogOSInfo` -> `Startup.WriteOSInfo`, `Init_LogSysMemInfo` ->
    `Startup.WriteSystemMemoryInfo`, `Init_LogTimeInfo` ->
    `Startup.WriteTimeInfo`
  - `WriteShutdownLog` -> `Shutdown.WriteLine`, `BannerMessage_Shutdown`
    -> `Shutdown.Banner`
  - `LogsFolderPath` -> `File.FolderPath`, `LogFilePath` ->
    `File.FilePath`, `LogFlushMode` -> `File.Flush`
  - `AutoOpenClosePerWrite`, `FlushInterval`, `OpenRetryCount`,
    `OpenRetryDelay`, `CircuitBreakerResetDelay`, `EnableRotation`,
    `MaxFileSizeBytes`, `RotationRetryDelay`, `EnableDailyRolling` and
    `RetentionMaxAge` move into `File` under the same names (e.g.
    `File.EnableRotation`)
  - `ResolveLogFilePath()` -> `File.ResolvePath()`, `ResolveLogFileDir()`
    -> `File.ResolveFolder()`
  - `CallbackMinimumLevel` -> `OnLogEntryMinimumLevel`

  A custom formatter reads the line options from `config.Line`.
- `GetConfig()` returns the settings as an immutable snapshot,
  `std::shared_ptr<const TASWLogConfig>`, instead of a reference to the
  logger's own config; the non-const overload is removed. A snapshot stays
  valid and unchanged after the settings change. To change a setting after
  `Initialize()`, copy the snapshot, change the copy and pass it to
  `Reconfigure()`:
  `auto config = *logger.GetConfig(); config.Line.ShowThreadId = false; logger.Reconfigure(config);`.
  Read fields through the pointer (`logger.GetConfig()->File.FilePath`).
  Custom loggers must change: one implementing `IASWLog` directly, or
  deriving from `TASWLogBase`, implements
  `bool Reconfigure(const TASWLogConfig&) noexcept override`, and one
  deriving from `TASWLogBase` stores the config with the protected
  `SetConfig()` instead of assigning `m_Config` (now private), and reads it
  with `GetConfigUnlocked()` while holding its own lock. One deriving from
  `TASWTextLogBase` can override the new hook `ReconfigureUnlocked()`.

### Fixed

- A file logger with `File.AutoOpenClosePerWrite` now writes its shutdown
  line, and a second `Initialize()` call fails as for any initialized logger.
  Closing the file after each entry used to mark the logger as not
  initialized, so the shutdown line was skipped and a repeated
  `Initialize()` wrote the startup lines again.
- Data races when changing settings while other threads log: changing a
  field through `GetConfig()`'s reference, which had no lock, and calling
  the `OnLogEntry` callback, which was read from the config after the
  logger's lock was released. Settings now change only through
  `Reconfigure()`, under the lock, and the callback is called from the
  settings the entry was written with, so a callback may also reconfigure
  the logger it belongs to.

## [0.45.0] - 2026-10-01

### Added

- CMake library target `ASWLog::ASWLog`, defined by a new root
  `CMakeLists.txt`, for use with `add_subdirectory` or `FetchContent`.
  Linking to it adds the include folder, C++20 and `psapi`, so a CMake
  project no longer lists the logger's sources itself. The CMake example
  and unit-test projects now link to it.
- CMake option `ASWLOG_SANITIZE` (`thread`, or `address` with
  UndefinedBehaviorSanitizer) for GCC and Clang, building the library and
  everything that links to it with that sanitizer. CI runs the unit tests
  under ThreadSanitizer and AddressSanitizer + UBSan on Linux (Clang).
- `TASWTextLogBase` (`ASWLog_TextLogBase.h/.cpp`, a new source file to add
  to non-CMake projects), the base of `TASWFileLog` and `TASWConsoleLog`. It
  implements the logging methods, startup and shutdown lines, line format
  and `OnLogEntry` once; a new text logger derives from it and implements
  protected hooks for its output (`InitializeUnlocked`, `OpenUnlocked`,
  `CloseUnlocked`, `WriteLineUnlocked`, and optionally `EnsureReadyUnlocked`,
  `PrepareWriteUnlocked`, `AfterEntryUnlocked`).
- Custom line formats: assign an `IASWLogFormatter` to the new
  `TASWLogConfig::Formatter` to lay out each line your own way, without
  deriving a logger. A formatter gets a `TASWLogRecord` (time, level,
  message, source location, process and thread id) and the config, and can
  be shared by several loggers. Empty keeps the built-in layout, now also
  available as `TASWTextFormatter`. New source file `ASWLog_Formatter.h/.cpp`
  (add it to non-CMake projects).

### Changed

- Every library header now has `#pragma once` and includes the standard
  headers it uses, so each one compiles on its own, whatever it is included
  after. The startup "App:" line's unused `MacOSX` target branch was removed;
  the logger supports Windows and Linux only.
- `Initialize`, `Open`, `Close`, `IsOpen`, `Log`, `LogRaw`, `LogForce` and
  `LogForceRaw` are now `final` in `TASWFileLog` and `TASWConsoleLog`: a
  derived class that overrode them must use the `TASWTextLogBase` hooks or
  a formatter (`TASWLogConfig::Formatter`) instead. `DispatchLogCallback`
  moved from `TASWLogBase` to `TASWTextLogBase` (private). With
  `LogMethodName` or `LogSourceLine` on, the startup lines now name
  `TASWTextLogBase` and its source file. The output is otherwise unchanged.

## [0.43.0] - 2026-09-29

### Added

- CI workflow (`.github/workflows/ci.yml`), building and running the unit
  tests on Windows (MSVC, MinGW) and Linux (GCC, Clang). Each job shows its
  JUnit test report on the run's summary page and uploads it as an artifact,
  including for pull requests from forks.
- VCL GUI test runner (`tests/vcl/gui/rad370/ASWLogTests_VCL_GUI.cbproj`),
  running the unit tests in the ASWUnitTests GUI (a window with a test tree,
  per-test results and log, and Run Failed), built from the submodule's
  `vcl/gui/src` sources. Its project group also opens the console test
  project.
- This CHANGELOG.md file.
- `.gitattributes`, normalizing line endings.
- `GetWindowsEditionName()` (Windows only), naming the Windows edition for a
  `GetProductInfo()` product type, as used by `GetOSInfoString()`.
- `TASWLogBase::NowUTC()`, a protected virtual returning the time the logger
  uses for line timestamps, daily rolling, and backup names. A custom logger
  or a test can override it to control the logger's clock.
- `TASWLogConfig::CircuitBreakerResetDelay` (default 500 ms), how long
  `TASWFileLog` drops entries without trying to reopen a log file that
  couldn't be reopened, before trying again. Each try still uses
  `OpenRetryCount`/`OpenRetryDelay`; 0 tries on every entry.
- `TASWLogConfig::RotationRetryDelay` (default 500 ms), how long
  `TASWFileLog` waits after a failed size rotation before trying to rotate
  again; 0 tries on every entry.
- `PathToUTF8String()`, converting a `std::filesystem::path` to a UTF-8
  string without throwing.
- `ColorMode` (`Auto`, `Always`, `Never`), with `ColorMode_ToString()` and
  `ColorMode_FromString()`, and `TASWConsoleLog::SetColorMode()`/
  `GetColorMode()` to choose it (default `Auto`).
- `TASWConsoleLog::DetectStreamColorSupport()`, a protected virtual deciding
  whether stdout or stderr shows colors; override it to change the detection.
- `IsRootFolder()`, telling whether a folder is the root of a drive, network
  share, volume, or file system (e.g. `C:\`, `\\server\share\`, `/`), as
  used by `DeleteOldLogs`.
- `Time::GetUTCOffsetMinutes()`, the local time zone's offset from UTC at a
  given time, including daylight saving time, and
  `Time::ToLocalISO8601String()`, formatting a local time with milliseconds
  and that offset (e.g. `2026-09-28T21:02:44.123-05:00`).
- `GetCurrentOSProcessId()` and `GetCurrentOSThreadId()`, the operating
  system's ids for the current process and the calling thread.
- `RenameWithoutReplacing()`, renaming a file only if the new name isn't
  taken, even when another process creates it at the same moment (unlike
  `std::filesystem::rename`, which replaces it).

### Changed

- Unit tests now use [ASWUnitTests](https://github.com/ASWSoftware/asw-unit-tests)
  1.0.0 as a git submodule in `third_party/asw-unit-tests`, replacing the
  ASWUnitTests 0.26.3 copy in `unit-tests/`. Test modules moved to
  `tests/tests/` and self-register with `ASW_REGISTER_TEST_GROUP`; their
  CMake project is `tests/cmake/` and their RAD Studio project is
  `tests/rad370/`. Building the tests requires the submodule
  (`git clone --recurse-submodules`, or `git submodule update --init`); using
  the logger itself does not.
- `TASWConsoleLog::IsColorSupported()` now reports whether stdout or stderr
  is a console or terminal that shows colors. On Linux it used to be always
  true; it's now false when both streams are redirected.
- `TASWFileLog::DeleteOldLogs` with an empty `pattern` now deletes nothing;
  it used to delete every file in the folder older than `maxAge`. To keep
  that behavior, pass `"*"`.
- The startup "Time:" line (and `GetTimeInfoString()`) now shows the local
  time with milliseconds and its UTC offset, e.g.
  `local=2026-09-28T21:02:44.123-05:00`.
- The thread id in log lines (`[T:...]`, with `LogThreadId`) and in
  `GenerateLogFileName()` names (`_TID...`) is now the OS thread id, as shown
  by debuggers, crash dumps, Process Explorer, and `top -H`, instead of a hash
  of `std::thread::id` (e.g. `[T:12608]` instead of
  `[T:15729502191765196471]`). Anything that parses these values sees
  smaller numbers.

### Removed

- `TASWConsoleLog::SetUseColor()` and `GetUseColor()`, replaced by
  `SetColorMode()`/`GetColorMode()`. Replace `SetUseColor(false)` with
  `SetColorMode(ASWLog::ColorMode::Never)`. `SetUseColor(true)` (the old
  default) becomes `ColorMode::Auto`, or `ColorMode::Always` to keep writing
  color codes to output that isn't a terminal.

### Fixed

- MinGW `-Wcast-function-type` warning in `GetOSInfoString()`, when casting
  `GetProcAddress()`'s result to the `RtlGetVersion` signature.
- `GetOSInfoString()` reporting the edition as "Unknown" on Windows 10/11 Home
  and on every Windows Server edition. The Server checks used product type
  constants that no Windows SDK defines, so they never compiled in. It now
  names the Home, Pro for Workstations, Enterprise LTSC, and Server
  Standard/Datacenter editions, and reports any other product type as
  "Server" or "Unknown" plus its product type number.
- `ResolveLogFileDir_CustomFolder` unit test failing on Linux: it used a
  backslash-separated path, which is only a separator on Windows. The
  backslash case is now checked on Windows only.
- `TASWFileLog` rotation deleting an earlier backup when it rotated twice for
  the same reason on the same day. Backups are now named
  `<stem>.<reason>.YYYY-MM-DD_HHMMSS_mmm.bak` (UTC), with `_1`, `_2`, ...
  added if that name is taken, and an existing backup is never replaced.
- Daily rolling (`EnableDailyRolling`) naming the backup for the day that had
  just started instead of the day its entries are from. Daily backups keep the
  `<stem>.daily.YYYY-MM-DD.bak` form, with `_1`, `_2`, ... added if that day's
  backup already exists. The day changes at UTC midnight, as it always has;
  the `EnableDailyRolling` comment now says so.
- Size-based rotation (`EnableRotation` with `MaxFileSizeBytes`) never
  rotating on Windows while the log was open, so the file grew without limit.
  The size is now tracked by the logger (the file's size when opened plus
  what it writes) instead of being read from the file's path, which Windows
  doesn't update while the file is open. With `AutoOpenClosePerWrite`, the
  size is read again at every write, so other processes' writes to a shared
  log still count toward the limit.
- `TASWFileLog` silently stopping for the rest of the process when the log
  file couldn't be reopened, e.g. after a rotation while another program
  briefly held the file. Later entries now retry the open once
  `CircuitBreakerResetDelay` has passed since the last failed attempt, and
  logging resumes as soon as it succeeds (entries logged while the file can't
  be opened are dropped). A size rotation that fails is retried once
  `RotationRetryDelay` has passed, instead of on every entry; entries keep
  going to the current file meanwhile.
- `LogSourceLine`/`LogMethodName` reporting a location inside
  `ASWLog_Interface.h` for every `*Fmt` call (`LogInfoFmt`, `LogFmt`,
  `LogForceRawFmt`, etc.) instead of the caller's file, line, and function.
  The format string parameter is now a `TASWFormatString`, which captures the
  caller's location; string literals, `std::string`, and `std::string_view`
  format strings convert to it automatically.
- Logging calls throwing exceptions into the application:
  - A `*Fmt` call whose format string doesn't match its arguments, or whose
    argument's formatter throws, now logs
    `[ASWLog format error: <reason>] <format string>` instead of throwing
    `std::format_error`.
  - `TASWFileLog` and `TASWConsoleLog` drop an entry they fail to write
    (e.g. out of memory) instead of throwing, and an exception while writing
    the shutdown entry in their destructors can no longer terminate the
    program.
  - `TASWMultiLog` catches an exception from one of its loggers (e.g. a
    custom `IASWLog`) and still logs to the others.
- `DeleteOldLogs` (also run by `RetentionMaxAge` after a rotation) throwing
  on file-system errors, and on file names the Windows ANSI code page can't
  represent (seen with RAD Studio's library). It now skips entries it can't
  read, and matches `pattern` against UTF-8 file names.
- `Initialize()` able to throw on Windows when the executable's path has
  characters the ANSI code page can't represent. The startup "App:" line and
  `GetApplicationInfoString()` now write the path as UTF-8.
- On Linux, reading unexpected `/proc/self/status` contents for the memory
  information could throw.
- `TASWConsoleLog` writing ANSI color codes into redirected output (files and
  pipes) and into Windows consoles that can't show them. With the default
  `ColorMode::Auto`, stdout and stderr are each colored only if they show
  colors, and not at all when the `NO_COLOR` environment variable is set
  (https://no-color.org).
- Undefined behavior when logging through `TASWFileLog::GetInstance()` or
  `TASWConsoleLog::GetInstance()` during static destruction, e.g. from another
  static object's destructor or a thread still running at exit, since the
  instance could already be destroyed. The instance is now never destroyed;
  at exit it only writes the shutdown entry and closes, in the same order as
  before. A static object constructed after the first `GetInstance()` call
  can still log from its destructor; one constructed before it has its
  entries dropped. Leak checkers that list memory still allocated at exit
  (e.g. the MSVC debug heap's report) now include the instance.
- `TASWMultiLog::Initialize()` returning false after `Close()`, so a closed
  composite couldn't be initialized again, unlike the other loggers.
  Concurrent `Initialize()` calls could also all succeed and write the
  configuration at the same time; now exactly one succeeds.
- `DeleteOldLogs` refusing any folder path of 3 characters or fewer, such
  as the relative folders `log` or `.`, which it mistook for a root folder.
  It now refuses only an actual root folder, which it also didn't recognize
  in forms such as `\\server\share\` or `C:\logs\..`.
- The startup "Time:" line (`GetTimeInfoString()`) reporting `offset_minutes`
  an hour off while daylight saving time was in effect (e.g. -360 instead of
  -300 for US Central daylight time).
- Child processes inheriting `TASWFileLog`'s open log file. On Windows, a
  child started with handle inheritance (e.g. `CreateProcess` with
  `bInheritHandles`) kept the file open and made rotation fail until it
  exited. On Linux, a program started with `fork()` and `exec()` kept the file
  open. The file is now opened non-inheritable on Windows and close-on-exec
  on Linux.
- With daily rolling (`EnableDailyRolling`), a log left over from an earlier
  UTC day (e.g. the app was restarted the next morning) was appended to, so
  its old entries ended up in the backup for the following day. `Initialize()`
  now rotates such a file to the daily backup named for the day it was last
  written.
- Two processes rotating a shared log at the same moment could overwrite one
  of the backups. Rotation now renames the log only to a name that isn't
  taken, and tries the next `_1`, `_2`, ... name if another process takes it
  first.
- With daily rolling on a log shared by several processes
  (`AutoOpenClosePerWrite`), each process rolled the log over at its first
  entry after midnight, so a later process also moved the new file (with
  today's entries from the earlier one) into a backup for the previous day.
  In that mode, the log is now rolled over only if it was last written on an
  earlier day. The `AutoOpenClosePerWrite` comment now says it's required for
  sharing a log between processes.

## [0.26.4] - 2026-09-21

### Fixed

- `Deploy.bat` not copying the `TASWConsoleLog` and `TASWMultiLog` source
  files added in 0.26.3.
- Missing closing quote in the unrecognized-platform `#error` message added
  in 0.26.3.

## [0.26.3] - 2026-09-21

### Added

- `TASWConsoleLog`, logging to the console: `Warn` and above to `stderr`,
  everything else to `stdout`, with optional per-level ANSI colors.
- `TASWMultiLog`, fanning each log call out to several `IASWLog` sinks (e.g.
  a `TASWFileLog` and a `TASWConsoleLog`). It doesn't own the sinks; the
  caller manages their lifetime.
- `TASWLogConfig::OnLogEntry`, an optional callback invoked with each
  successfully written line at or above `CallbackMinimumLevel` (default
  `Error`), e.g. for alerting or crash reporting. It runs outside the
  logger's lock, so it may log again, and exceptions it throws are
  swallowed. Supported by `TASWFileLog` and `TASWConsoleLog`.
- `TASWLogConfig::RetentionMaxAge`, making `TASWFileLog` delete this log's
  backups older than the given age after each rotation (`0`, the default,
  disables it).
- `GetMinimumLevel()` and `SetMinimumLevel()` on loggers, reading or changing
  the minimum level at runtime without locking.
- `LevelCount`, the number of `Level` values.
- Example usage of `TASWConsoleLog` and `TASWMultiLog`, and unit tests for
  them, plus a multi-threaded `TASWFileLog` test.

### Changed

- `TASWLogConfig::MinimumLevel` renamed to `InitialMinimumLevel`, since it
  now only seeds the level at `Initialize()`; use `SetMinimumLevel()` to
  change it afterward. Code setting `MinimumLevel` must be updated. Entries
  below the minimum level are now filtered before taking the logger's lock.
- `GenerateLogFileName()` takes a new leading `prefix` parameter, producing
  `[prefix_]YYYYMMDD_HHMMSS_mmm_PID_TID[_customPostfix]`; pass an empty
  string to omit either part. Existing calls must add the argument.
- An unrecognized target platform is now a compile error in
  `WriteApplicationInfo()`, instead of logging a placeholder platform name.

## [0.26.1] - 2026-09-17

Initial versioned release, switching the project to semantic versioning.
Everything already present in the logger at this point (`TASWFileLog`,
`TASWLogConfig`, log rotation and cleanup, memory/OS diagnostics, RAD Studio
and CMake example projects, unit tests, `Deploy.bat`, etc.) is treated as the
baseline and is not itemized commit-by-commit.

[Unreleased]: https://github.com/ASWSoftware/asw-log/compare/v0.75.0...HEAD
[0.75.0]: https://github.com/ASWSoftware/asw-log/compare/v0.66.0...v0.75.0
[0.66.0]: https://github.com/ASWSoftware/asw-log/compare/v0.65.0...v0.66.0
[0.65.0]: https://github.com/ASWSoftware/asw-log/compare/v0.45.0...v0.65.0
[0.45.0]: https://github.com/ASWSoftware/asw-log/compare/v0.43.0...v0.45.0
[0.43.0]: https://github.com/ASWSoftware/asw-log/compare/v0.26.4...v0.43.0
[0.26.4]: https://github.com/ASWSoftware/asw-log/compare/v0.26.3...v0.26.4
[0.26.3]: https://github.com/ASWSoftware/asw-log/compare/v0.26.1...v0.26.3
[0.26.1]: https://github.com/ASWSoftware/asw-log/releases/tag/v0.26.1
