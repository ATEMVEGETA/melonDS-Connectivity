/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef LOCALMP_H
#define LOCALMP_H

#include "types.h"
#include "Platform.h"
#include "MPInterface.h"
#include "InstanceMask.h"

#include <array>
#include <memory>

namespace melonDS
{
constexpr int kMaxLocalMPInstances = kMaxLocalInstances;

struct MPStatusData
{
    InstanceMask ConnectedBitmask; // local radios, not guest AIDs
    u32 PacketWriteOffset;
    u32 ReplyWriteOffset[kMaxLocalMPInstances];
};

constexpr u32 kPacketQueueSize = 0x10000;
constexpr u32 kReplyQueueSize = 0x10000;
constexpr u32 kMaxFrameSize = 0x948;

class LocalMP : public MPInterface
{
public:
    LocalMP() noexcept;
    LocalMP(const LocalMP&) = delete;
    LocalMP& operator=(const LocalMP&) = delete;
    LocalMP(LocalMP&& other) = delete;
    LocalMP& operator=(LocalMP&& other) = delete;
    ~LocalMP() noexcept;

    void Process() {}

    void Begin(int inst);
    void End(int inst);

    int SendPacket(int inst, u8* data, int len, u64 timestamp);
    int RecvPacket(int inst, u8* data, u64* timestamp);
    int SendCmd(int inst, u8* data, int len, u64 timestamp);
    int SendReply(int inst, u8* data, int len, u64 timestamp, u16 aid);
    int SendAck(int inst, u8* data, int len, u64 timestamp);
    int RecvHostPacket(int inst, u8* data, u64* timestamp);
    u16 RecvReplies(int inst, u8* data, u64 timestamp, u16 aidmask);

private:
    void FIFORead(int inst, int fifo, void* buf, int len) noexcept;
    void FIFOWrite(int inst, int fifo, void* buf, int len) noexcept;
    int SendPacketGeneric(int inst, u32 type, u8* packet, int len, u64 timestamp) noexcept;
    int RecvPacketGeneric(int inst, u8* packet, bool block, u64* timestamp) noexcept;

    Platform::Mutex* MPQueueLock;
    MPStatusData MPStatus {};
    u8 MPPacketQueue[kPacketQueueSize] {};
    std::unique_ptr<u8[]> MPReplyQueue;
    u32 PacketReadOffset[kMaxLocalMPInstances] {};
    u32 PacketPendingBytes[kMaxLocalMPInstances] {};
    u32 ReplyReadOffset[kMaxLocalMPInstances] {};

    std::array<int, kMaxLocalMPInstances> LastHostIDs;
    std::array<std::array<u8, 6>, kMaxLocalMPInstances> LocalMACs {};
    std::array<std::array<u8, 6>, kMaxLocalMPInstances> HostMACs {};
    std::array<bool, kMaxLocalMPInstances> Associated {};
    Platform::Semaphore* SemPool[kMaxLocalMPInstances * 2] {};
};
}

#endif // LOCALMP_H
