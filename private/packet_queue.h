/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <memory>
#include <deque>

extern "C" {
#include <libavformat/avformat.h>
}

#include "../utils.h"

#include "av_smart_pointers.h"
#include "utils.h"

namespace Avalanche {

class PacketQueue {
    typedef std::deque<std::shared_ptr<AVPacket>> PacketHolder;

public:
    PacketQueue() {
    }

    PacketQueue(const PacketHolder &packets) :
        m_packets(packets) {
    }

    ~PacketQueue() {
        clear();
    }

    bool add(AVPacket *packet) {
        AVPacket *copy_packet = av_packet_alloc();
        if (!copy_packet) {
            log(LOG_ERROR, "Error allocating packet\n");
            return false;
        }
        av_init_packet(copy_packet);
        av_packet_ref(copy_packet, packet);

        m_packets.push_back(std::shared_ptr<AVPacket>(copy_packet, AVPacketUnreferDeleter()));
        return true;
    }

    bool isEmpty() {
        return m_packets.empty();
    }

    std::shared_ptr<AVPacket> getFirst() {
        return m_packets.front();
    }

    std::shared_ptr<AVPacket> removeFirst() {
        auto retval = m_packets.front();
        m_packets.pop_front();
        return retval;
    }

    void clear() {
        m_packets.clear();
    }

    // returns a PacketQueue with everything after pts; they're all removed from this
    PacketQueue removeAfterVideoPts(int video_stream_index, int64_t pts) {
        // first, set partition_it to the last packet less than or equal to pts
        PacketHolder::iterator partition_it = m_packets.end();
        for (auto it = m_packets.begin(); it != m_packets.end(); ++it) {
            if ((*it)->stream_index != video_stream_index) {
                continue;
            }
            if ((*it)->pts <= pts) {
                partition_it = it;
            }
        }

        // now, bump partition_it to the next packet, and cut there to the end
        if (partition_it != m_packets.end()) {
            partition_it++;
        }
        PacketQueue after_pts_packet_queue(PacketHolder(partition_it, m_packets.end()));

        m_packets.erase(partition_it, m_packets.end());

        return after_pts_packet_queue;
    }

    std::shared_ptr<AVPacket> getFirstPacketByStreamIndex(int video_stream_index) {
        for (auto it = m_packets.begin(); it != m_packets.end(); ++it) {
            if ((*it)->stream_index != video_stream_index) {
                continue;
            }
            return *it;
        }
        return nullptr;
    }

    std::shared_ptr<AVPacket> getLastPacketByStreamIndex(int video_stream_index) {
        for (auto it = m_packets.rbegin(); it != m_packets.rend(); ++it) {
            if ((*it)->stream_index != video_stream_index) {
                continue;
            }
            return *it;
        }
        return nullptr;
    }

    PacketHolder::iterator begin() {
        return m_packets.begin();
    }

    PacketHolder::iterator end() {
        return m_packets.end();
    }

    size_t size() {
        return m_packets.size();
    }

private:
    PacketHolder m_packets;
};

}
