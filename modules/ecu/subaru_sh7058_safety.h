#ifndef SUBARU_SH7058_SAFETY_H
#define SUBARU_SH7058_SAFETY_H
#include <QCryptographicHash>
#include <kernelmemorymodels.h>
#include "../checksum/checksum_ecu_subaru_denso_sh7xxx.h"

namespace SubaruSH7058Safety {
constexpr uint32_t ImageSize = 1024 * 1024;
inline bool usesModule(const QString &protocol) {
    return protocol.startsWith("sub_ecu_denso_sh7058_can") &&
        !protocol.startsWith("sub_ecu_denso_sh7058_can_diesel");
}
inline bool validMap(const flashdev_t *map) {
    if (!map || !map->name || map->mcutype != SH7058 || map->romsize != ImageSize ||
        !map->fblocks || !map->numblocks || map->numblocks > 16)
        return false;
    const QString name = QString::fromLatin1(map->name);
    if (name != "SH7058" && name != "SH7058_1block") return false;
    uint32_t end = 0;
    for (unsigned i = 0; i < map->numblocks; ++i) {
        const auto &b = map->fblocks[i];
        if (b.start != end || !b.len || b.len % 0x1000 || b.len > ImageSize - end) return false;
        end += b.len;
    }
    return end == ImageSize;
}
inline const flashdev_t *memoryMap(const QString &mcu) {
    for (const auto &map : flashdevices) {
        if (!map.name) break;
        if (mcu == QLatin1String(map.name)) return validMap(&map) ? &map : nullptr;
    }
    return nullptr;
}
inline bool validImage(const QByteArray &image, const flashdev_t *map) {
    return image.size() == int(ImageSize) && validMap(map);
}
inline bool validRange(uint32_t start, uint32_t length) {
    return length && start < ImageSize && length <= ImageSize - start;
}
// The pinned kernel selects one of these 16 physical erase sectors, even when
// an image/compare map presents the ROM as one aggregate region. Never use an
// aggregate or rearranged map as an erase plan; do not change the map itself.
inline bool validProgrammingMap(const flashdev_t *map) {
    if (!validMap(map) || map->numblocks != 16) return false;
    for (unsigned i=0; i<16; ++i)
        if (map->fblocks[i].start != fblocks_SH7058[i].start ||
            map->fblocks[i].len != fblocks_SH7058[i].len) return false;
    return true;
}
// Erase requests identify a whole mapped sector by its start address, not an
// arbitrary aligned slice. Never infer a new erase geometry from an image range.
inline bool validSector(const flashdev_t *map, uint32_t start, uint32_t length) {
    if (!validProgrammingMap(map)) return false;
    for (unsigned i = 0; i < map->numblocks; ++i)
        if (map->fblocks[i].start == start && map->fblocks[i].len == length) return true;
    return false;
}
inline bool preservesInput(const ChecksumEcuSubaruDensoSH7xxx::Result &result, const QByteArray &original) {
    return result.usable() && result.state != ChecksumEcuSubaruDensoSH7xxx::State::Corrected &&
        result.image == original;
}
inline QByteArray fingerprint(const QByteArray &image) {
    return QCryptographicHash::hash(image, QCryptographicHash::Sha256);
}
inline ChecksumEcuSubaruDensoSH7xxx::Result prepare(const QByteArray &image, const QString &mcu,
                                                  bool definition, bool checksum) {
    using C = ChecksumEcuSubaruDensoSH7xxx;
    if (!validImage(image, memoryMap(mcu))) return {C::State::Invalid, {}};
    if (!definition || !checksum) return {C::State::Unsupported, {}};
    return C::evaluate(image, 0x0FFB80, 17 * 12, 0, C::EndpointPolicy::AlignedOrLastByte);
}
}
#endif
