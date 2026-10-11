# Guarantees: thread safety, lifetime and crash durability

What ASWLog promises about threads, object lifetimes and crashes, and how to choose the settings that trade speed for
durability. The header comments have the details of each class and setting.

## Logging never throws

Every logging method is `noexcept`, and so is every other method of `IASWLog` except `GetFullVersionStr()`. A logger
reports a failure by returning `false` (`Initialize()`, `Reconfigure()`, `Open()`, `Close()`, `Flush()`) or by
dropping the entry, and reports its internal failures (e.g. a log file that can't be opened, written or rotated) to
`TASWLogConfig::OnError`, or to stderr if that is empty. A `*Fmt` format string that fails at run time (only possible
with `RuntimeFormat()`) writes the error in the entry's line instead of the message.

## Thread safety

- Every logger is thread-safe: any thread may log, flush, open, close or reconfigure it at any time.
- `SetMinimumLevel()`, `SetEnabled()` and the level check of a logging call are lock-free.
- `Reconfigure()` is safe while other threads log: the entries written after it returns use the new settings.
  `GetConfig()` returns an immutable snapshot that stays valid after a later `Reconfigure()`.
- A text logger writes one entry at a time under its own lock, so lines from different threads never mix.
- A category logger is lock-free (making or destroying one takes a short process-wide lock). A multi-log guards its
  list of loggers, and each of its loggers its own output.
- Scoped fields (`TASWLogScope`) belong to the thread that made them; `TASWLogScope::Capture()` copies them for
  another thread.
- Several processes can share one log file with `File.AutoOpenClosePerWrite`, which opens and closes the file for
  every entry (slower, but no process keeps it open).

## Timestamps and order

- An entry's time, process id and thread id are taken on the calling thread, during the call (after the level check,
  so a filtered call doesn't read the clock). The time is when the event happened, not when the logger got to it.
- One thread's entries are written in the order it logged them, also with asynchronous writing, which uses one queue.
- Entries that two threads log at the same moment can be written in either order, so the times in a log can step back
  slightly between lines of different threads.

## Which thread runs a callback

| Callback | Thread | Notes |
|---|---|---|
| `OnBeforeWrite` | The logging thread, before the logger takes its lock or queues the entry | Threads that log at once call it at once: keep it thread-safe and quick. It may log to another logger; an entry logged from inside it, on its thread, skips the hooks. If it throws, the entry is dropped and the exception reported. |
| `OnLogEntry` | The logging thread after the entry is written, outside the lock; with `Async.Enabled`, the logger's own thread | Exceptions are swallowed. The record and line are only valid during the call. |
| `OnError` | A thread that was using the logger (logging, `Flush()`, `Close()`, ...), outside the lock, or the logger's own thread (e.g. a failed periodic flush) | It may log, even to the failing logger: failures caused by the handler itself aren't reported again. Exceptions are swallowed. |
| `File.OnBackupCreated` | The thread whose call rotated the log (a logging call, `Initialize()`, `Reconfigure()` or `RotateLogFiles()`; with `Async.Enabled`, the logger's own thread), outside the lock | That call waits for it: hand slow work (compressing, uploading) to another thread. Exceptions are swallowed. |

## Lifetime

- **Singletons.** `GetInstance()` (file, console, debugger, syslog, Windows Event Log and null loggers) returns an
  instance that is never destroyed, so it can be used until the process ends, e.g. from another static object's
  destructor or a thread still running at exit. At exit it is finalized (shutdown line, flush, close) in static
  destruction order: a static object constructed after the first `GetInstance()` call can still log from its
  destructor. In a Windows DLL or package that is unloaded before the process ends, `Close()` a logger that uses
  `FlushMode::Periodic` (or `Async`) before unloading, since stopping its thread during the unload can deadlock.
- **Instances.** A logger you create writes its shutdown line, writes the entries still queued, flushes and closes in
  its destructor.
- **Multi-log.** `TASWMultiLog` holds its loggers by reference and doesn't own them: each must outlive the multi-log,
  or be removed from it (`RemoveLogger()`) first.
- **Category.** The logger a `TASWCategoryLog` wraps must outlive the category, and must not be a multi-log that
  holds the category.
- **Scopes.** A `TASWLogScope` must end on the thread that made it, in the reverse order of making, as local variables
  do. It copies its keys and texts, so they may be temporaries.
- **Fields of a call.** The keys and texts of an entry's fields are only read during the call, so they may be
  temporaries too; a logger that keeps the entry (asynchronous writing, the backtrace) copies them.

## Crash durability

ASWLog's first purpose is that the last entries before a crash are in the log. The defaults are the most durable;
each faster option says what it gives up.

| Setting | What reaches the file | Cost per entry |
|---|---|---|
| Default: synchronous, `File.Flush = EveryWrite` | Every entry is handed to the operating system before the call returns, so it survives an application crash. | One write and flush per entry. |
| `File.Flush = Periodic`, `Manual` or `OnNewLine` | Entries wait in the logger's buffer until a flush. Entries at or above `File.FlushImmediatelyAtLevel` (Error by default) are still flushed at once. On a crash, the crash handlers (below) flush the buffer. | Less I/O; fewer, larger writes. |
| `File.SyncToDiskAtLevel` (e.g. `Level::Error`) | Those entries are also synced to the disk, so they survive a power loss or a system crash. | A sync often takes milliseconds: use it for rare entries. |
| `Async.Enabled` | The call only formats and queues the entry; the logger's thread writes it. Entries at or above `Async.WaitAtLevel` (Error by default) wait until they are written and flushed. On a crash, the handlers give the logger's thread up to `TASWCrashHandlerOptions::WaitTimeout` (1 s) to write the queue. | The call doesn't wait for the output. |
| `Backtrace.Capacity` | The last N entries below the minimum level are kept in memory and written just before the next Error, on `DumpBacktrace()`, or on a crash. | About the cost of copying the message, for entries that would otherwise be filtered. |

**Crash handlers.** `ASWLog::InstallCrashHandlers()` (in `ASWLog_CrashHandler.h`) catches `std::terminate()`,
`abort()` and fatal faults (an access violation on Windows, SIGSEGV and the like on Linux). Every initialized text
logger is then flushed, including its buffered and queued entries, and gets a Critical "Crash: <reason>" line, before
the crash is passed on to the handler installed before (so error reports and core dumps still happen). This is best
effort, since a crashed process may be in any state; the header lists what is and isn't handled. An application with
its own crash reporter can call `ASWLog::HandleCrash()` from it instead.

### Choosing settings

| Goal | Settings |
|---|---|
| Find the cause of any crash (the default) | The defaults; call `InstallCrashHandlers()` early in `main()`. |
| Less I/O for a busy log, errors still at once | `File.Flush = FlushMode::Periodic` (every `File.FlushInterval`, 1 s by default) and `InstallCrashHandlers()`. `File.FlushImmediatelyAtLevel` stays at Error. |
| Errors that survive a power loss | `File.SyncToDiskAtLevel = Level::Error`. |
| Logging calls that don't wait for a slow disk | `Async.Enabled = true`; `Async.WaitAtLevel` stays at Error, and `InstallCrashHandlers()`. |
| The details leading up to an error, without logging them all | `InitialMinimumLevel = Level::Info` and `Backtrace.Capacity = 200` (or another count). |
| Full verbosity in a deployed build, without rebuilding | `config.ApplyEnvironment()` before `Initialize()`, then run with `ASWLOG_LEVEL=Trace` (see [Configuration.md](Configuration.md#settings-from-environment-variables)). |
