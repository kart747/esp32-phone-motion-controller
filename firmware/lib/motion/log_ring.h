// log_ring - fixed-capacity ring of short text lines, single producer and
// single consumer on different tasks.
//
// Diagnostics must never stall packet processing, so pushing is non-blocking:
// when the ring is full the newest line is dropped and counted, and the caller
// can report the loss instead of blocking.
//
// FreeRTOS runs the two tasks on the same core (TASK_CORE_APP) but they are
// still preemptively scheduled, so the head/tail/count fields are shared state:
// every read-modify-write is guarded by a short critical section. Without that,
// a preemption between the bounds check and the index update can corrupt the
// FIFO. The critical section is a few instructions, so it cannot meaningfully
// delay the receive task.
//
// The mutex is injected as a policy so this header stays Arduino-free and can
// be unit-tested on the laptop:
//   * NativeLogRingLock - no-op, used by tests that drive one side at a time.
//   * the firmware supplies a portMUX_TYPE critical section.
template <size_t CAPACITY, size_t LINE, typename Lock>
class LogRing {
public:
    // Returns false if the line was dropped (full) or too long.
    bool push(const char *line) {
        if (!line || strlen(line) >= LINE) {
            Lock::enter();
            dropped_++;
            Lock::exit();
            return false;
        }
        bool ok = false;
        Lock::enter();
        if (count_ == CAPACITY) {
            dropped_++;
        } else {
            strcpy(buf_[head_], line);
            head_ = (head_ + 1) % CAPACITY;
            count_++;
            ok = true;
        }
        Lock::exit();
        return ok;
    }

    // Copies the oldest line into `out` (size >= LINE) and returns true.
    bool pop(char *out) {
        bool ok = false;
        Lock::enter();
        if (count_ > 0) {
            strcpy(out, buf_[tail_]);
            tail_ = (tail_ + 1) % CAPACITY;
            count_--;
            ok = true;
        }
        Lock::exit();
        return ok;
    }

    size_t size() {
        Lock::enter();
        const size_t n = count_;
        Lock::exit();
        return n;
    }
    bool empty() { return size() == 0; }

    // Dropped lines are reported and cleared together: the caller periodically
    // logs "N records dropped" so a loss is never silent.
    uint32_t takeDropped() {
        Lock::enter();
        const uint32_t n = dropped_;
        dropped_ = 0;
        Lock::exit();
        return n;
    }

private:
    char     buf_[CAPACITY][LINE] = {};
    size_t   head_ = 0, tail_ = 0, count_ = 0;
    uint32_t dropped_ = 0;
};

// Single-threaded policy for host tests and for code that already holds the lock.
struct NullLogRingLock {
    static void enter() {}
    static void exit() {}
};