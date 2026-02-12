#ifndef CRISKY_VIZ_RING_BUFFER_HPP
#define CRISKY_VIZ_RING_BUFFER_HPP

#include <vector>
#include <cassert>

// Fixed-capacity circular buffer for time-series data.
// Panels read from this; gyms/games push into it each tick.
// When full, oldest values are overwritten.
template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(int capacity = 4096)
        : data_(capacity), capacity_(capacity) {}

    void push(T value) {
        data_[write_pos_] = value;
        write_pos_ = (write_pos_ + 1) % capacity_;
        if (size_ < capacity_) size_++;
    }

    void clear() {
        size_ = 0;
        write_pos_ = 0;
    }

    int size() const { return size_; }
    int capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == capacity_; }

    // Index 0 = oldest, size()-1 = newest.
    T operator[](int i) const {
        assert(i >= 0 && i < size_);
        int start = (write_pos_ - size_ + capacity_) % capacity_;
        return data_[(start + i) % capacity_];
    }

    // Most recent value.
    T back() const {
        assert(size_ > 0);
        return data_[(write_pos_ - 1 + capacity_) % capacity_];
    }

    // Copy contents to a contiguous vector (for ImPlot).
    // Returns oldest-to-newest order.
    std::vector<T> to_vector() const {
        std::vector<T> out(size_);
        for (int i = 0; i < size_; i++) {
            out[i] = (*this)[i];
        }
        return out;
    }

private:
    std::vector<T> data_;
    int capacity_;
    int size_ = 0;
    int write_pos_ = 0;
};

#endif
