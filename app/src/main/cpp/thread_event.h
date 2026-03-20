#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <mutex>
#include <functional>

namespace threading {

    using EventId = uint32_t;

    struct Event {
        EventId id;
        std::vector<uint8_t> payload;  // owns the data (copied on post)

        template<typename T>
        const T* As() const {
            return reinterpret_cast<const T*>(payload.data());
        }
    };

    // Simple thread-safe event queue: one thread posts, another drains.
    class EventQueue {
    public:
        void Post(EventId id, const void* data = nullptr, size_t size = 0) {
            Event e;
            e.id = id;
            if (data && size > 0) {
                e.payload.assign(
                    static_cast<const uint8_t*>(data),
                    static_cast<const uint8_t*>(data) + size);
            }
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(e));
        }

        // Drains all pending events, calls handler for each.
        void Drain(const std::function<void(const Event&)>& handler) {
            std::vector<Event> batch;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                batch.swap(queue_);
            }
            for (const auto& e : batch) {
                handler(e);
            }
        }

        bool Empty() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.empty();
        }

    private:
        mutable std::mutex mutex_;
        std::vector<Event> queue_;
    };
}
