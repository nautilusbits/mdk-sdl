#include "core/Process.h"

#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Mdk
{
    namespace
    {
        constexpr size_t ReadBuffer = 4096;
        constexpr std::intptr_t NoPipe = -1;

        // Splits bytes into lines at "\n", "\r" or "\r\n", as .NET's line reader does.
        class LineSplitter
        {
        public:
            explicit LineSplitter(Process::DataReceived& handler) : _handler(handler) {}

            void Add(const char* data, size_t size)
            {
                for (size_t i = 0; i < size; i++)
                {
                    auto c = data[i];
                    if (_afterCr && c == '\n')
                    {
                        _afterCr = false;
                        continue;
                    }

                    _afterCr = c == '\r';
                    if (c == '\n' || c == '\r')
                    {
                        Emit(_line);
                        _line.clear();
                        continue;
                    }

                    _line += c;
                }
            }

            // The last line without an end, then the end of the stream.
            void End()
            {
                if (!_line.empty())
                {
                    Emit(_line);
                }

                Emit(std::nullopt);
            }

        private:
            void Emit(const std::optional<std::string>& line)
            {
                if (_handler)
                {
                    _handler(line);
                }
            }

            Process::DataReceived& _handler;
            std::string _line;
            bool _afterCr = false;
        };

        struct ExitHandlers
        {
            std::mutex Lock;
            std::map<int, std::function<void()>> Handlers;
            int Next = 1;
        };

        ExitHandlers& Exits()
        {
            // Built before atexit is registered, so it outlives the handler run.
            static ExitHandlers exits;
            return exits;
        }

        void RunExitHandlers()
        {
            auto& exits = Exits();
            std::map<int, std::function<void()>> handlers;
            {
                std::lock_guard lock(exits.Lock);
                handlers = exits.Handlers;
            }

            for (auto& [key, handler] : handlers)
            {
                handler();
            }
        }

#if defined(_WIN32)
        std::wstring Utf16(const std::string& text)
        {
            if (text.empty())
            {
                return {};
            }

            auto length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }

        // One argument of a command line, quoted so that CommandLineToArgvW gives it back.
        void AppendArgument(std::wstring& line, const std::wstring& argument)
        {
            if (!line.empty())
            {
                line += L' ';
            }

            if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos)
            {
                line += argument;
                return;
            }

            line += L'"';
            size_t backslashes = 0;
            for (auto c : argument)
            {
                if (c == L'\\')
                {
                    backslashes++;
                    continue;
                }

                // Backslashes before a quote are doubled, the quote escaped.
                auto doubled = c == L'"' ? backslashes * 2 + 1 : backslashes;
                line.append(doubled, L'\\');
                backslashes = 0;
                line += c;
            }

            line.append(backslashes * 2, L'\\');
            line += L'"';
        }

        // A pipe whose write end the child inherits.
        bool ChildPipe(HANDLE& read, HANDLE& write)
        {
            SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            if (!CreatePipe(&read, &write, &inherit, 0))
            {
                return false;
            }

            SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
            return true;
        }

        HANDLE AsHandle(std::intptr_t value) { return reinterpret_cast<HANDLE>(value); }

        std::intptr_t AsValue(HANDLE handle) { return reinterpret_cast<std::intptr_t>(handle); }
#else
        void CloseOnExec(int fd)
        {
            fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
        }

        bool ChildPipe(int fds[2])
        {
            if (pipe(fds) != 0)
            {
                return false;
            }

            CloseOnExec(fds[0]);
            CloseOnExec(fds[1]);
            return true;
        }

        int ExitCodeOf(int status)
        {
            constexpr int SignalBase = 128;
            return WIFEXITED(status) ? WEXITSTATUS(status) : SignalBase + WTERMSIG(status);
        }
#endif
    }

    int AppDomain::AddProcessExit(std::function<void()> handler)
    {
        auto& exits = Exits();
        static const bool registered = std::atexit(RunExitHandlers) == 0;
        (void)registered;

        std::lock_guard lock(exits.Lock);
        auto key = exits.Next++;
        exits.Handlers[key] = std::move(handler);
        return key;
    }

    void AppDomain::RemoveProcessExit(int key)
    {
        auto& exits = Exits();
        std::lock_guard lock(exits.Lock);
        exits.Handlers.erase(key);
    }

    void Process::BeginOutputReadLine()
    {
        auto pipe = _output;
        _output = NoPipe;
        _outputReader = std::thread([this, pipe] { ReadLines(pipe, OutputDataReceived); });
    }

    void Process::BeginErrorReadLine()
    {
        auto pipe = _error;
        _error = NoPipe;
        _errorReader = std::thread([this, pipe] { ReadLines(pipe, ErrorDataReceived); });
    }

    bool Process::WaitForExit(int milliseconds)
    {
        Reap(milliseconds);
        return _exited;
    }

    void Process::WaitForExit()
    {
        Reap(-1);
        if (_outputReader.joinable())
        {
            _outputReader.join();
        }

        if (_errorReader.joinable())
        {
            _errorReader.join();
        }
    }

#if defined(_WIN32)
    std::unique_ptr<Process> Process::Start(const ProcessStartInfo& info)
    {
        std::wstring line;
        AppendArgument(line, Utf16(info.FileName));
        for (auto& argument : info.ArgumentList)
        {
            AppendArgument(line, Utf16(argument));
        }

        HANDLE outputRead = nullptr, outputWrite = nullptr, errorRead = nullptr, errorWrite = nullptr;
        if (info.RedirectStandardOutput && !ChildPipe(outputRead, outputWrite))
        {
            return nullptr;
        }

        if (info.RedirectStandardError && !ChildPipe(errorRead, errorWrite))
        {
            return nullptr;
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = outputWrite != nullptr ? outputWrite : GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = errorWrite != nullptr ? errorWrite : GetStdHandle(STD_ERROR_HANDLE);

        // Suspended until it's in the job, so that what it starts is in the job too.
        DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | (info.CreateNoWindow ? CREATE_NO_WINDOW : 0);
        auto folder = Utf16(info.WorkingDirectory);
        PROCESS_INFORMATION started{};
        auto ok = CreateProcessW(Utf16(info.FileName).c_str(), line.data(), nullptr, nullptr, TRUE, flags, nullptr,
                                 folder.empty() ? nullptr : folder.c_str(), &startup, &started);
        for (auto handle : {outputWrite, errorWrite})
        {
            if (handle != nullptr)
            {
                CloseHandle(handle);
            }
        }

        if (!ok)
        {
            for (auto handle : {outputRead, errorRead})
            {
                if (handle != nullptr)
                {
                    CloseHandle(handle);
                }
            }

            return nullptr;
        }

        std::unique_ptr<Process> process(new Process());
        process->_process = AsValue(started.hProcess);
        process->_job = AsValue(CreateJobObjectW(nullptr, nullptr));
        if (process->_job != 0)
        {
            AssignProcessToJobObject(AsHandle(process->_job), started.hProcess);
        }

        ResumeThread(started.hThread);
        CloseHandle(started.hThread);
        process->_output = outputRead != nullptr ? AsValue(outputRead) : NoPipe;
        process->_error = errorRead != nullptr ? AsValue(errorRead) : NoPipe;
        return process;
    }

    Process::~Process()
    {
        if (_outputReader.joinable())
        {
            _outputReader.join();
        }

        if (_errorReader.joinable())
        {
            _errorReader.join();
        }

        for (auto pipe : {_output, _error})
        {
            if (pipe != NoPipe)
            {
                CloseHandle(AsHandle(pipe));
            }
        }

        if (_job != 0)
        {
            CloseHandle(AsHandle(_job));
        }

        CloseHandle(AsHandle(_process));
    }

    void Process::ReadLines(std::intptr_t pipe, DataReceived& handler)
    {
        LineSplitter lines(handler);
        char buffer[ReadBuffer];
        DWORD read = 0;
        while (ReadFile(AsHandle(pipe), buffer, sizeof(buffer), &read, nullptr) && read > 0)
        {
            lines.Add(buffer, read);
        }

        lines.End();
        CloseHandle(AsHandle(pipe));
    }

    bool Process::HasExited()
    {
        Reap(0);
        return _exited;
    }

    void Process::Reap(int milliseconds)
    {
        if (_exited)
        {
            return;
        }

        auto wait = milliseconds < 0 ? INFINITE : static_cast<DWORD>(milliseconds);
        if (WaitForSingleObject(AsHandle(_process), wait) != WAIT_OBJECT_0)
        {
            return;
        }

        DWORD code = 0;
        GetExitCodeProcess(AsHandle(_process), &code);
        _exitCode = static_cast<int>(code);
        _exited = true;
    }

    void Process::Kill(ProcessTree tree)
    {
        if (HasExited())
        {
            return;
        }

        // .NET's exit code of a killed process.
        constexpr UINT Killed = static_cast<UINT>(-1);
        if (tree == ProcessTree::Entire && _job != 0)
        {
            TerminateJobObject(AsHandle(_job), Killed);
            return;
        }

        TerminateProcess(AsHandle(_process), Killed);
    }
#else
    std::unique_ptr<Process> Process::Start(const ProcessStartInfo& info)
    {
        // Everything the child needs is made before fork: it may only call async-signal-safe functions.
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(info.FileName.c_str()));
        for (auto& argument : info.ArgumentList)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }

        argv.push_back(nullptr);
        auto searchPath = info.FileName.find('/') == std::string::npos;
        auto folder = info.WorkingDirectory.empty() ? nullptr : info.WorkingDirectory.c_str();

        int output[2] = {-1, -1}, error[2] = {-1, -1}, failure[2] = {-1, -1};
        auto closeAll = [&]
        {
            for (auto fd : {output[0], output[1], error[0], error[1], failure[0], failure[1]})
            {
                if (fd >= 0)
                {
                    close(fd);
                }
            }
        };

        // The failure pipe carries exec's errno; it closes unread when exec works.
        if ((info.RedirectStandardOutput && !ChildPipe(output)) || (info.RedirectStandardError && !ChildPipe(error)) || !ChildPipe(failure))
        {
            closeAll();
            return nullptr;
        }

        auto pid = fork();
        if (pid < 0)
        {
            closeAll();
            return nullptr;
        }

        if (pid == 0)
        {
            // Its own process group: Kill(Entire) reaches what it starts.
            setpgid(0, 0);
            if (output[1] >= 0)
            {
                dup2(output[1], STDOUT_FILENO);
            }

            if (error[1] >= 0)
            {
                dup2(error[1], STDERR_FILENO);
            }

            if (folder == nullptr || chdir(folder) == 0)
            {
                if (searchPath)
                {
                    execvp(argv[0], argv.data());
                }
                else
                {
                    execv(argv[0], argv.data());
                }
            }

            int code = errno;
            auto written = write(failure[1], &code, sizeof(code));
            (void)written;
            constexpr int ExecFailed = 127;
            _exit(ExecFailed);
        }

        setpgid(pid, pid);
        for (auto* fd : {&output[1], &error[1], &failure[1]})
        {
            if (*fd >= 0)
            {
                close(*fd);
                *fd = -1;
            }
        }

        int code = 0;
        ssize_t read;
        do
        {
            read = ::read(failure[0], &code, sizeof(code));
        } while (read < 0 && errno == EINTR);
        close(failure[0]);
        failure[0] = -1;
        if (read > 0)
        {
            waitpid(pid, nullptr, 0);
            closeAll();
            return nullptr;
        }

        std::unique_ptr<Process> process(new Process());
        process->_process = pid;
        process->_output = output[0];
        process->_error = error[0];
        return process;
    }

    Process::~Process()
    {
        if (_outputReader.joinable())
        {
            _outputReader.join();
        }

        if (_errorReader.joinable())
        {
            _errorReader.join();
        }

        for (auto pipe : {_output, _error})
        {
            if (pipe != NoPipe)
            {
                close(static_cast<int>(pipe));
            }
        }

        // A child still running is reaped by no one now; like .NET, it isn't killed.
    }

    void Process::ReadLines(std::intptr_t pipe, DataReceived& handler)
    {
        auto fd = static_cast<int>(pipe);
        LineSplitter lines(handler);
        char buffer[ReadBuffer];
        while (true)
        {
            auto read = ::read(fd, buffer, sizeof(buffer));
            if (read < 0 && errno == EINTR)
            {
                continue;
            }

            if (read <= 0)
            {
                break;
            }

            lines.Add(buffer, static_cast<size_t>(read));
        }

        lines.End();
        close(fd);
    }

    bool Process::HasExited()
    {
        Reap(0);
        return _exited;
    }

    void Process::Reap(int milliseconds)
    {
        constexpr auto Poll = std::chrono::milliseconds(2);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        auto pid = static_cast<pid_t>(_process);
        while (!_exited)
        {
            int status = 0;
            auto done = waitpid(pid, &status, milliseconds < 0 ? 0 : WNOHANG);
            if (done == pid)
            {
                _exitCode = ExitCodeOf(status);
                _exited = true;
                return;
            }

            if (done < 0 && errno != EINTR)
            {
                return;
            }

            if (milliseconds >= 0 && std::chrono::steady_clock::now() >= deadline)
            {
                return;
            }

            if (milliseconds >= 0)
            {
                std::this_thread::sleep_for(Poll);
            }
        }
    }

    void Process::Kill(ProcessTree tree)
    {
        if (HasExited())
        {
            return;
        }

        auto pid = static_cast<pid_t>(_process);
        kill(tree == ProcessTree::Entire ? -pid : pid, SIGKILL);
    }
#endif
}
