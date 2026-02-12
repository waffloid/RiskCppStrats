#ifndef CRISKY_SYSTEMS_BUFFER_HPP
#define CRISKY_SYSTEMS_BUFFER_HPP

#include <utility>

// Single-writer, single-reader double buffer for inter-system communication.
// write() stages a pending value; commit() publishes it; read() returns the
// last committed value.  This decouples producers from consumers and handles
// cyclic dependencies (e.g. Combat <-> Transport) cleanly.
template<typename T>
class Buffer {
public:
    Buffer() = default;
    explicit Buffer(T initial) : committed_(std::move(initial)) {}

    // Stage a value.  Does not become visible to readers until commit().
    void write(T value) {
        pending_ = std::move(value);
        has_pending_ = true;
    }

    // Read the last committed value.
    const T& read() const { return committed_; }

    // True if write() was called since the last commit().
    bool has_update() const { return has_pending_; }

    // Publish the pending value so that read() returns it.
    void commit() {
        if (has_pending_) {
            committed_ = std::move(pending_);
            has_pending_ = false;
        }
    }

private:
    T committed_{};
    T pending_{};
    bool has_pending_ = false;
};

#endif
