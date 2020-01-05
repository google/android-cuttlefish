#include <webrtc/Packetizer.h>

#include <webrtc/RTPSender.h>

void Packetizer::queueRTPDatagram(std::vector<uint8_t> *packet) {
    auto it = mSenders.begin(); 
    while (it != mSenders.end()) {
        auto sender = it->lock();
        if (!sender) {
            it = mSenders.erase(it);
            continue;
        }

        sender->queueRTPDatagram(packet);
        ++it;
    }
}

void Packetizer::addSender(std::shared_ptr<RTPSender> sender) {
    mSenders.push_back(sender);
}

