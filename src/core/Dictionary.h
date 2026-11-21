#pragma once

// System.Collections.Generic.Dictionary with .NET's enumeration order: entries in slot order;
// Remove frees a slot, the next Add reuses the latest freed one (LIFO), Clear starts afresh.
//   d[k] = v;  d.TryGetValue(k, v);  d.Remove(k);  for (const auto& [k, v] : d) { ... }
// (std::unordered_map iterates in hash order; code that iterates a Dictionary needs this one.)

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Mdk
{
    template <typename K, typename V, typename Hash = std::hash<K>>
    class Dictionary
    {
        struct Slot
        {
            std::pair<K, V> Pair;
            // -1 used; otherwise the next free slot + 1 (0: end of the free list).
            int NextFree = -1;
        };

    public:
        using value_type = std::pair<K, V>;

        class const_iterator
        {
        public:
            const_iterator(const std::vector<Slot>* slots, size_t index) : _slots(slots), _index(index) { Skip(); }
            const std::pair<K, V>& operator*() const { return (*_slots)[_index].Pair; }
            const std::pair<K, V>* operator->() const { return &(*_slots)[_index].Pair; }
            const_iterator& operator++() { _index++; Skip(); return *this; }
            bool operator==(const const_iterator& o) const { return _index == o._index; }
            bool operator!=(const const_iterator& o) const { return _index != o._index; }

        private:
            void Skip()
            {
                while (_index < _slots->size() && (*_slots)[_index].NextFree != -1)
                {
                    _index++;
                }
            }

            const std::vector<Slot>* _slots;
            size_t _index;
        };

        Dictionary() = default;

        Dictionary(std::initializer_list<std::pair<K, V>> items)
        {
            for (const auto& item : items)
            {
                (*this)[item.first] = item.second;
            }
        }

        // new Dictionary(other): the entries in other's order, compacted.
        Dictionary(const Dictionary& other) { CopyFrom(other); }

        Dictionary& operator=(const Dictionary& other)
        {
            if (this != &other)
            {
                Clear();
                CopyFrom(other);
            }

            return *this;
        }

        Dictionary(Dictionary&&) noexcept = default;
        Dictionary& operator=(Dictionary&&) noexcept = default;

        int Count() const { return static_cast<int>(_index.size()); }

        bool ContainsKey(const K& key) const { return _index.contains(key); }

        bool TryGetValue(const K& key, V& value) const
        {
            auto found = _index.find(key);
            if (found == _index.end())
            {
                return false;
            }

            value = _slots[found->second].Pair.second;
            return true;
        }

        // A pointer to the value, or null (no copy).
        const V* Find(const K& key) const
        {
            auto found = _index.find(key);
            return found == _index.end() ? nullptr : &_slots[found->second].Pair.second;
        }

        V* Find(const K& key)
        {
            auto found = _index.find(key);
            return found == _index.end() ? nullptr : &_slots[found->second].Pair.second;
        }

        V GetValueOrDefault(const K& key, V fallback = V{}) const
        {
            const V* value = Find(key);
            return value != nullptr ? *value : fallback;
        }

        // The C# indexer's getter throws KeyNotFoundException.
        const V& At(const K& key) const
        {
            const V* value = Find(key);
            if (value == nullptr)
            {
                throw std::out_of_range("The given key was not present in the dictionary.");
            }

            return *value;
        }

        // The C# indexer's setter: adds or replaces (in place).
        V& operator[](const K& key)
        {
            if (V* value = Find(key))
            {
                return *value;
            }

            int slot;
            if (_freeList >= 0)
            {
                slot = _freeList;
                _freeList = _slots[slot].NextFree - 1;
                _slots[slot] = Slot{{key, V{}}, -1};
            }
            else
            {
                slot = static_cast<int>(_slots.size());
                _slots.push_back(Slot{{key, V{}}, -1});
            }

            _index.emplace(key, slot);
            return _slots[slot].Pair.second;
        }

        // Add throws on a duplicate key.
        void Add(const K& key, const V& value)
        {
            if (ContainsKey(key))
            {
                throw std::invalid_argument("An item with the same key has already been added.");
            }

            (*this)[key] = value;
        }

        bool TryAdd(const K& key, const V& value)
        {
            if (ContainsKey(key))
            {
                return false;
            }

            (*this)[key] = value;
            return true;
        }

        bool Remove(const K& key)
        {
            V ignored{};
            return Remove(key, ignored);
        }

        bool Remove(const K& key, V& value)
        {
            auto found = _index.find(key);
            if (found == _index.end())
            {
                return false;
            }

            int slot = found->second;
            _index.erase(found);
            value = std::move(_slots[slot].Pair.second);
            _slots[slot].Pair = {};
            _slots[slot].NextFree = _freeList + 1;
            _freeList = slot;
            return true;
        }

        void Clear()
        {
            _slots.clear();
            _index.clear();
            _freeList = -1;
        }

        const_iterator begin() const { return {&_slots, 0}; }
        const_iterator end() const { return {&_slots, _slots.size()}; }

    private:
        void CopyFrom(const Dictionary& other)
        {
            for (const auto& [key, value] : other)
            {
                (*this)[key] = value;
            }
        }

        std::vector<Slot> _slots;
        std::unordered_map<K, int, Hash> _index;
        int _freeList = -1;
    };
}
