#pragma once

// System.Threading.CancellationToken / CancellationTokenSource: one shared flag, set once.
//   CancellationTokenSource cancel;  worker(cancel.Token());  cancel.Cancel();
//   CancellationToken::None() never cancels.

#include <atomic>
#include <memory>

#include "core/DotNet.h"

namespace Mdk
{
    class CancellationToken
    {
    public:
        CancellationToken() = default;

        static CancellationToken None() { return {}; }

        bool IsCancellationRequested() const { return _flag != nullptr && _flag->load(); }

        void ThrowIfCancellationRequested() const
        {
            if (!IsCancellationRequested())
            {
                return;
            }

            throw OperationCanceledException("The operation was canceled.");
        }

    private:
        friend class CancellationTokenSource;

        explicit CancellationToken(std::shared_ptr<std::atomic<bool>> flag) : _flag(std::move(flag)) {}

        std::shared_ptr<std::atomic<bool>> _flag;
    };

    class CancellationTokenSource
    {
    public:
        void Cancel() { _flag->store(true); }

        bool IsCancellationRequested() const { return _flag->load(); }

        CancellationToken Token() const { return CancellationToken(_flag); }

    private:
        std::shared_ptr<std::atomic<bool>> _flag = std::make_shared<std::atomic<bool>>(false);
    };
}
