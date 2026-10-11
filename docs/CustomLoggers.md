# Custom formatters and loggers

ASWLog can be extended without changing its sources, so a copy in a submodule or a vendored folder
stays untouched:

- To change how a line looks, write a **formatter**, and any text logger (file, console, ...) uses
  it.
- To send lines somewhere new (a socket, a GUI control, another library), write a **text logger**:
  derive from `TASWTextLogBase` and implement only the output. Levels, formatting, the startup and
  shutdown lines, asynchronous writing, the backtrace, the hooks and crash handling come with it.
- For output that isn't lines of text (e.g. passing entries on to another logging framework), derive
  from `TASWLogBase` and implement `WriteRecord()`, as `TASWMultiLog` does.

Before writing a formatter, check whether a pattern does the job, e.g.
`std::make_shared<ASWLog::TASWPatternFormatter>("{time} {level:5} {[category] }{message}")` (see
`ASWLog_Formatter.h` for the fields), or `TASWJSONFormatter` for JSON Lines.

## A formatter

A formatter derives from `IASWLogFormatter` (`ASWLog_Formatter.h`) and turns a `TASWLogRecord` into
a line, without its line ending (the logger adds `Line.Ending`, and applies `Line.Multiline`):

```cpp
#include <format>
#include <string>

#include "ASWLog_Formatter.h"

// Writes "WARN [Net] Retrying", or "INFO Started" for an entry without a category
class TShortFormatter : public ASWLog::IASWLogFormatter
{
public:
    std::string Format(const ASWLog::TASWLogRecord& record, const ASWLog::TASWLogConfig& /*config*/) const override
    {
        if (record.Category.empty())
            return std::format("{} {}", ASWLog::Level_ToString(record.LogLevel), record.Message);

        return std::format("{} [{}] {}", ASWLog::Level_ToString(record.LogLevel), record.Category, record.Message);
    }
};
```

Assign it to the config of the loggers that should use it:

```cpp
ASWLog::TASWLogConfig config;
config.Line.Formatter = std::make_shared<TShortFormatter>();
```

- The record has the entry's time (`Timestamp`, UTC), `LogLevel`, `Message`, `Category`, source
  `Location`, `ProcessId`, `ThreadId`, and its fields through
  `record.ForEachField([](const ASWLog::TASWLogField& field) {...})`. `config` is the calling
  logger's config, e.g. for `Line.TimestampZone`.
- Several loggers, and several threads, may call one formatter at once: keep `Format()` free of
  unguarded state.
- `Format()` may throw (e.g. `std::bad_alloc`); the entry is then dropped and the logger goes on.
- A `LogRaw()` entry is written as it is, without calling the formatter, unless
  `FormatsRawEntries()` returns `true`.

## A text logger

A text logger derives from `TASWTextLogBase` (`ASWLog_TextLogBase.h`). This one writes each line to
a `std::ostream`, e.g. a stream that sends to a socket:

```cpp
#include <ostream>
#include <string_view>

#include "ASWLog_TextLogBase.h"

// Writes each line to a std::ostream that outlives the logger
class TStreamLog : public ASWLog::TASWTextLogBase
{
private:
    std::ostream& m_Stream;

protected: // TASWTextLogBase hooks, called with the logger's lock held
    bool CloseUnlocked() override
    {
        m_IsOpen.store(false);
        m_IsInitialized.store(false);

        return true;
    }

    bool FlushUnlocked() override
    {
        m_Stream.flush();

        return m_IsOpen.load() && m_Stream.good();
    }

    bool InitializeUnlocked() override
    {
        return OpenUnlocked();
    }

    bool OpenUnlocked() override
    {
        m_IsOpen.store(true);
        m_IsInitialized.store(true);

        return true;
    }

    // 'line' already ends with the line ending, unless it is a LogRaw() entry written as is
    void WriteLineUnlocked(ASWLog::Level /*level*/, std::string_view line, bool /*endsLine*/) override
    {
        m_Stream << line;
    }

    std::string_view GetLoggerClassName() const noexcept override
    {
        return "TStreamLog";
    }

public:
    explicit TStreamLog(std::ostream& stream) :
        m_Stream(stream)
    {
    }

    // Writes the shutdown line and closes the output while this class's hooks still exist
    ~TStreamLog() override
    {
        Finalize();
    }
};
```

It is used like any other logger:

```cpp
TStreamLog log(stream);
log.Initialize(config);
log.LogWarnFmt("Retrying {}", host);
```

The rules for the hooks:

- **Required:** `InitializeUnlocked()` (prepare and open the output), `OpenUnlocked()` (set
  `m_IsOpen` and `m_IsInitialized`), `CloseUnlocked()` (clear them), `FlushUnlocked()` and
  `WriteLineUnlocked()`, plus `GetLoggerClassName()` (used by `GetFullVersionStr()`).
- **Call `Finalize()` in the destructor.** The base class can't call the hooks from its own
  destructor, since the derived part is gone by then.
- The hooks run with the logger's lock held, one entry at a time, so they need no locking of their
  own. They must not log to the same logger. To call back into the application (which may log),
  queue the work with `DeferUnlocked()`; to report a failure, call `ReportErrorUnlocked()`, which
  passes it to `OnError` after the lock is released.
- An exception from a hook is caught: the entry is dropped and the failure reported, so logging
  still never throws.
- **Optional hooks:** `PrepareWriteUnlocked()` (before each line, e.g. to rotate; return `false` to
  drop the line), `ReconfigureUnlocked()` (apply a new config), `GetWorkerIntervalUnlocked()` and
  `OnWorkerWakeUnlocked()` (periodic work on the logger's own thread, e.g. a timed flush), and, for
  crash handling, `FlushForCrashUnlocked()` and `WriteCrashLineDirect()`. The crash hooks may run in
  a POSIX signal handler, so they must only do async-signal-safe work (no allocation, no locks).
  Without them, a crash still flushes the logger through `FlushUnlocked()` where it can.
- A logger whose output exists only on some platforms can override `ShouldLog()` to return `false`
  elsewhere, so no line is formatted there (see `TASWDebuggerLog`).

The built-in loggers are examples to start from: `TASWDebuggerLog` and `TASWMemoryLog` are short,
and `TASWFileLog` shows rotation, the worker thread and the crash hooks.
