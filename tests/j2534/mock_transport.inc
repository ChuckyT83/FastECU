// Shared deterministic mock behavior, included by both offline backends only.
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
struct MockReadResult { long status; unsigned long count, size, flags, extra; };
static long mockWriteStatus = 0, mockPeriodicStatus = 0, mockStopStatus = 0, mockIoctlStatus = 0;
static unsigned long mockWriteCount = 1, mockCalls[6] = {}, mockLastTimeout = 0;
static unsigned long mockLastSize = 0, mockLastFlags = 0, mockLastChannel = 0;
static std::vector<MockReadResult> mockReads;
static unsigned mockReadIndex = 0;
static unsigned long mockFiveSize = 2;
static int mockFivePointer = 0;
static unsigned long mockDelay = 0;
static unsigned char mockPayload[PASSTHRU_MSG_DATA_SIZE] = {};
EXPORT void PT_CALL MockTransportReset() {
    mockWriteStatus = mockPeriodicStatus = mockStopStatus = mockIoctlStatus = 0;
    mockWriteCount = 1; std::fill(mockCalls, mockCalls + 6, 0);
    mockLastTimeout = mockLastSize = mockLastFlags = mockLastChannel = 0;
    mockReads.clear(); mockReadIndex = 0; mockDelay = 0; mockFiveSize = 2; mockFivePointer = 0;
}
EXPORT void PT_CALL MockTransportDelay(unsigned long ms) { mockDelay = ms; }
EXPORT unsigned long PT_CALL MockTransportByte(int i) { return i>=0 && i<PASSTHRU_MSG_DATA_SIZE ? mockPayload[i] : 0; }
EXPORT void PT_CALL MockTransportWrite(long status, unsigned long count) { mockWriteStatus = status; mockWriteCount = count; }
EXPORT void PT_CALL MockTransportRead(long status, unsigned long count, unsigned long size, unsigned long flags, unsigned long extra) { mockReads.push_back({status,count,size,flags,extra}); }
EXPORT void PT_CALL MockTransportOther(long periodic, long stop, long ioctlStatus) { mockPeriodicStatus = periodic; mockStopStatus = stop; mockIoctlStatus = ioctlStatus; }
EXPORT void PT_CALL MockTransportFive(unsigned long size, int pointer) { mockFiveSize = size; mockFivePointer = pointer; }
EXPORT unsigned long PT_CALL MockTransportValue(int index) {
    if (index >= 0 && index < 6) return mockCalls[index];
    if (index == 6) return mockLastTimeout;
    if (index == 7) return mockLastSize;
    if (index == 8) return mockLastFlags;
    return mockLastChannel;
}
static long mockWrite(unsigned long channel, const void *data, unsigned long *count, unsigned long timeout) {
    ++mockCalls[0]; mockLastTimeout = timeout; mockLastChannel = channel;
    const auto *msg = static_cast<const PASSTHRU_MSG *>(data);
    mockLastSize = msg->DataSize; mockLastFlags = msg->TxFlags;
    std::copy(msg->Data,msg->Data + std::min<unsigned long>(msg->DataSize,PASSTHRU_MSG_DATA_SIZE),mockPayload);
    *count = mockWriteCount;
    return mockWriteStatus;
}
static long mockRead(unsigned long channel, void *data, unsigned long *count, unsigned long timeout) {
    ++mockCalls[1]; mockLastTimeout = timeout; mockLastChannel = channel;
    if (mockDelay) std::this_thread::sleep_for(std::chrono::milliseconds(mockDelay));
    if (mockReadIndex >= mockReads.size()) { *count = 0; return ERR_BUFFER_EMPTY; }
    const auto result = mockReads[mockReadIndex++];
    auto *msg = static_cast<PASSTHRU_MSG *>(data);
    *msg = {}; msg->DataSize = result.size; msg->RxStatus = result.flags; msg->ExtraDataIndex = result.extra;
    std::fill(msg->Data, msg->Data + std::min<unsigned long>(result.size, PASSTHRU_MSG_DATA_SIZE), 0x5a);
    *count = result.count;
    return result.status;
}
static long mockPeriodic(unsigned long channel, unsigned long *id) { ++mockCalls[2]; mockLastChannel = channel; *id = 0; return mockPeriodicStatus; }
static long mockStop(unsigned long channel) { ++mockCalls[3]; mockLastChannel = channel; return mockStopStatus; }
static long mockIoctl(unsigned long channel, unsigned long op, void *out) {
    ++mockCalls[4]; mockLastChannel = channel;
    if (op == FIVE_BAUD_INIT && out && !mockIoctlStatus) {
        auto *bytes = static_cast<SBYTE_ARRAY *>(out);
        const auto capacity = bytes->NumOfBytes;
        for (unsigned long i=0; i<std::min(capacity,mockFiveSize); ++i) bytes->BytePtr[i] = 0x55;
        bytes->NumOfBytes = mockFiveSize;
        if (mockFivePointer) bytes->BytePtr = nullptr;
    }
    return mockIoctlStatus;
}
static long mockVoltage(unsigned long device) { ++mockCalls[5]; mockLastChannel = device; return mockIoctlStatus; }
