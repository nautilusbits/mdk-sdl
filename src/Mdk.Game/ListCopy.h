#pragma once

#include <memory>
#include <stack>
#include <vector>

namespace Mdk::Game
{
    // A copy of a list to go through while the list changes (objects spawned or removed on
    // the way), its storage taken from a pool and given back on destruction: no list per tick.
    //   auto objects = ListCopy<std::shared_ptr<MdkObject>>::Of(runtime.Objects);
    //   for (auto& obj : objects) { ... runtime.Objects.erase(...) ... }
    template <typename T>
    class ListCopy
    {
    public:
        static ListCopy Of(const std::vector<T>& source)
        {
            auto& pool = Pool();
            std::unique_ptr<std::vector<T>> items;
            if (!pool.empty())
            {
                items = std::move(pool.top());
                pool.pop();
            }
            else
            {
                items = std::make_unique<std::vector<T>>();
            }

            items->insert(items->end(), source.begin(), source.end());
            return ListCopy(std::move(items));
        }

        ListCopy(ListCopy&&) noexcept = default;
        ListCopy& operator=(ListCopy&&) noexcept = default;
        ListCopy(const ListCopy&) = delete;
        ListCopy& operator=(const ListCopy&) = delete;

        ~ListCopy()
        {
            if (!_items)
            {
                return;
            }

            _items->clear();
            Pool().push(std::move(_items));
        }

        auto begin() const { return _items->begin(); }
        auto end() const { return _items->end(); }

    private:
        std::unique_ptr<std::vector<T>> _items;

        explicit ListCopy(std::unique_ptr<std::vector<T>> items) : _items(std::move(items)) {}

        // Per thread: parallel tests run several levels at once.
        static std::stack<std::unique_ptr<std::vector<T>>>& Pool()
        {
            thread_local std::stack<std::unique_ptr<std::vector<T>>> pool;
            return pool;
        }
    };
}
