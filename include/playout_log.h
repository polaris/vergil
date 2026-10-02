#ifndef PLAYOUT_LOG_H
#define PLAYOUT_LOG_H

#include <chrono>
#include <iomanip>
#include <ostream>

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>

#include "spsc_ring.h"

// One row per callback that plays: when its first frame is heard, by this
// receiver's own clock and in master time, and how far off the loop sees it.
//
// The frame heard has presentation time heardMaster - error / senderRate. On
// a single host local time is master time, so
//
//   heardLocal - (heardMaster - error / senderRate)
//
// is the receiver's true timing error, clock-sync error included; between two
// receivers on one host, the difference of theirs is their offset, bar their
// output latencies.
struct PlayoutRecord {
    double heardLocal = 0.0;    // seconds since the steady epoch
    double heardMaster = 0.0;   // seconds since the master's steady epoch
    double error = 0.0;         // frames, the rate loop's
    double correction = 0.0;    // of the resampling ratio, relative to nominal
};

// Audio thread -> control thread. About 5 s of 240-frame callbacks.
using PlayoutQueue = SpscRing<PlayoutRecord, 1024>;

// Writes the queue to a CSV file, on the control thread: the audio thread
// never touches the file.
class PlayoutLogWriter {
public:
    static constexpr auto kInterval = std::chrono::milliseconds(250);

    PlayoutLogWriter(asio::io_context& control, PlayoutQueue& queue, std::ostream& out)
    : timer_{control}
    , queue_{queue}
    , out_{out} {
    }

    PlayoutLogWriter(const PlayoutLogWriter&) = delete;
    PlayoutLogWriter& operator=(const PlayoutLogWriter&) = delete;

    void start() {
        out_ << "heard_local_s,heard_master_s,error_frames,correction_ppm\n";
        out_ << std::fixed;
        schedule();
    }

    // On the control thread, or after it has stopped running; once the audio
    // thread is done, so the rest of the queue is written too.
    void stop() {
        stopping_ = true;
        timer_.cancel();
        drain();
        out_.flush();
    }

private:
    void schedule() {
        timer_.expires_after(kInterval);
        timer_.async_wait([this](const asio::error_code& ec) {
            if (stopping_ || ec) {
                return;
            }
            drain();
            schedule();
        });
    }

    void drain() {
        for (PlayoutRecord r; queue_.pop(r); ) {
            out_ << std::setprecision(9) << r.heardLocal << ',' << r.heardMaster << ','
                 << std::setprecision(3) << r.error << ','
                 << std::setprecision(3) << r.correction * 1e6 << '\n';
        }
    }

    asio::steady_timer timer_;
    PlayoutQueue& queue_;
    std::ostream& out_;
    bool stopping_ = false;
};

#endif  // PLAYOUT_LOG_H
