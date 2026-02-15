#ifndef KRAKATOA_RING_BUFFER_H
#define KRAKATOA_RING_BUFFER_H
#include <vector>
#include <cassert>
namespace utils {
    template<typename T>
    class RingBuffer {
    public:
        RingBuffer(uint32_t items = MAX_FRAMES_IN_FLIGHT) : ringBuffer(items), cursor(0) {
            assert(items > 0);
        }

        T& Next() {
            cursor++;
            if (cursor >= static_cast<int32_t>(ringBuffer.size()))
                cursor = 0;
            return ringBuffer[cursor];
        }

        /// Retorna o elemento atual sem avançar
        T& Current() {
            assert(cursor >= 0 && "Call Next() before Current()");
            return ringBuffer[cursor];
        }

        const T& Current() const {
            assert(cursor >= 0 && "Call Next() before Current()");
            return ringBuffer[cursor];
        }

        T& operator[](uint32_t index) {
            return ringBuffer[index];
        }

        const T& operator[](uint32_t index) const {
            return ringBuffer[index];
        }

        uint32_t Size() const {
            return static_cast<uint32_t>(ringBuffer.size());
        }

        uint32_t CurrentIndex() const {
            return static_cast<uint32_t>(cursor);
        }

        T* Data() {
            return ringBuffer.data();
        }

        const T* Data() const {
            return ringBuffer.data();
        }

    private:
        std::vector<T> ringBuffer;
        int32_t cursor = -1;
    };
}
#endif //KRAKATOA_RING_BUFFER_H