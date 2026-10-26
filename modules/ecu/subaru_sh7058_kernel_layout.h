#ifndef SUBARU_SH7058_KERNEL_LAYOUT_H
#define SUBARU_SH7058_KERNEL_LAYOUT_H
#include <cstdint>
#include <limits>

namespace SubaruSH7058 {
// Host memory layout only. This does not establish bootloader permissions,
// response semantics, or permission to submit any command.
class KernelLayout {
public:
    enum { BlockSize = 128 };
    struct Block {
        uint32_t address = 0;
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    static KernelLayout create(int64_t bytes, uint32_t destination,
                               uint32_t ramStart, uint32_t ramLength) {
        KernelLayout result;
        const uint64_t addressSpace = uint64_t(1) << 32;
        const uint64_t ramEnd = uint64_t(ramStart) + ramLength;
        if (bytes < 4 || bytes > (std::numeric_limits<int>::max)() ||
            !ramLength || ramEnd > addressSpace || destination < ramStart || destination % 4)
            return result;
        const uint64_t padded = (uint64_t(bytes) + BlockSize - 1) & ~uint64_t(BlockSize - 1);
        // QByteArray uses int lengths in the supported Qt 5 builds.
        if (padded > uint64_t((std::numeric_limits<int>::max)()) ||
            uint64_t(destination) + padded > ramEnd)
            return result;
        result.destination_ = destination;
        result.size_ = uint32_t(padded);
        return result;
    }
    bool valid() const { return size_ != 0; }
    uint32_t preparedSize() const { return size_; }
    uint32_t blockCount() const { return size_ / BlockSize; }
    uint32_t destination() const { return destination_; }
    bool block(uint32_t index, Block &out) const {
        out = Block();
        if (!valid() || index >= blockCount()) return false;
        out.offset = index * BlockSize;
        out.address = destination_ + out.offset;
        out.length = BlockSize;
        return true;
    }
private:
    uint32_t destination_ = 0;
    uint32_t size_ = 0;
};
}
#endif
