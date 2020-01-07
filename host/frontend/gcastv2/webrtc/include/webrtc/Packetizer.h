#pragma once

#include <utils/Errors.h>
#include <stdint.h>

#include <memory>
#include <vector>

struct RTPSender;

struct Packetizer {
    explicit Packetizer() = default;
    virtual ~Packetizer() = default;

    virtual void run() = 0;
    virtual uint32_t rtpNow() const = 0;
    virtual android::status_t requestIDRFrame() = 0;

    void queueRTPDatagram(std::vector<uint8_t> *packet);

    void addSender(std::shared_ptr<RTPSender> sender);

private:
    std::vector<std::weak_ptr<RTPSender>> mSenders;
};
