#pragma once

// System.Diagnostics.Process, the part that runs a tool: arguments as a list, a working folder,
// standard output and error read line by line on their own threads, a timed wait, a kill of the
// whole tree (POSIX: the child's process group; Windows: a job object).
//   auto p = Process::Start(info);  p->OutputDataReceived = ...;  p->BeginOutputReadLine();
//   while (!p->WaitForExit(200)) { ... }  p->WaitForExit();  p->ExitCode();
// And AppDomain.ProcessExit: callbacks run when the program exits normally (atexit).

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Mdk
{
    struct ProcessStartInfo
    {
        std::string FileName;
        std::vector<std::string> ArgumentList;
        // Empty: the caller's.
        std::string WorkingDirectory;
        bool RedirectStandardOutput = false;
        bool RedirectStandardError = false;
        bool CreateNoWindow = false;
    };

    // What Kill stops: the process alone, or it and the processes it started.
    enum class ProcessTree { Root, Entire };

    class Process
    {
    public:
        // A line of output; nullopt once the stream ends (DataReceivedEventArgs.Data == null).
        using DataReceived = std::function<void(const std::optional<std::string>& data)>;

        // Null when it couldn't start (a missing or non-executable file).
        static std::unique_ptr<Process> Start(const ProcessStartInfo& info);

        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;
        // Waits for the readers (not for the process) and releases the handles.
        ~Process();

        DataReceived OutputDataReceived;
        DataReceived ErrorDataReceived;

        void BeginOutputReadLine();
        void BeginErrorReadLine();

        // True when it exited within the time.
        bool WaitForExit(int milliseconds);
        // Until it exits and its redirected output has been read to the end.
        void WaitForExit();

        void Kill(ProcessTree tree);

        // Its exit code (POSIX: 128 + signal when killed), once it has exited.
        int ExitCode() const { return _exitCode; }

    private:
        Process() = default;

        bool HasExited();
        void Reap(int milliseconds);
        void ReadLines(std::intptr_t pipe, DataReceived& handler);

        // POSIX: the pid (and process group). Windows: process and job handles.
        std::intptr_t _process = 0;
        std::intptr_t _job = 0;
        std::intptr_t _output = -1;
        std::intptr_t _error = -1;
        std::thread _outputReader;
        std::thread _errorReader;
        bool _exited = false;
        int _exitCode = 0;
    };

    struct AppDomain
    {
        // AppDomain.CurrentDomain.ProcessExit += handler; returns its key for the -=.
        static int AddProcessExit(std::function<void()> handler);
        static void RemoveProcessExit(int key);
    };
}
