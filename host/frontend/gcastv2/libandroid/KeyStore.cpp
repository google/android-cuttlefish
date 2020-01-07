#include <utils/KeyStore.h>

#include <unordered_map>
#include <mutex>

static std::unordered_map<std::string, std::vector<uint8_t>> gCertStore;
static std::mutex gCertStoreLock;

void setCertificateOrKey(
        const std::string &name, const void *_data, size_t size) {

    const uint8_t *data = static_cast<const uint8_t *>(_data);

    std::lock_guard autoLock(gCertStoreLock);
    gCertStore[name] = std::vector<uint8_t>(data, &data[size]);
}

bool getCertificateOrKey(
        const std::string &name, std::vector<uint8_t> *data) {
    std::lock_guard autoLock(gCertStoreLock);

    auto it = gCertStore.find(name);
    if (it == gCertStore.end()) {
        return false;
    }

    *data = it->second;

    return true;
}
