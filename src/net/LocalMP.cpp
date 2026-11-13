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

#include <cstring>
#include <chrono>

#include "LocalMP.h"

using namespace melonDS;
using namespace melonDS::Platform;

using Platform::Log;
using Platform::LogLevel;

namespace melonDS
{
static bool ValidInstance(int inst)
{
    return inst >= 0 && inst < kMaxLocalMPInstances;
}

static u16 FrameControl(const u8* packet, int len)
{
    return packet && len >= 14 ? u16(packet[12]) | (u16(packet[13]) << 8) : 0xFFFF;
}

static void ResetSemaphore(Semaphore* semaphore)
{
    while (Semaphore_TryWait(semaphore, 0)) {}
}

LocalMP::LocalMP() noexcept :
    MPQueueLock(Mutex_Create()),
    MPReplyQueue(std::make_unique<u8[]>(kMaxLocalMPInstances * kReplyQueueSize))
{
    memset(MPPacketQueue, 0, kPacketQueueSize);
    memset(&MPStatus, 0, sizeof(MPStatus));
    memset(PacketReadOffset, 0, sizeof(PacketReadOffset));
    memset(ReplyReadOffset, 0, sizeof(ReplyReadOffset));

    LastHostIDs.fill(-1);

    // Each radio has a packet semaphore and its own parent's reply semaphore.
    for (int i = 0; i < kMaxLocalMPInstances * 2; i++)
    {
        SemPool[i] = Semaphore_Create();
    }

    Log(LogLevel::Info, "MP comm init OK\n");
}

LocalMP::~LocalMP() noexcept
{
    for (int i = 0; i < kMaxLocalMPInstances * 2; i++)
    {
        Semaphore_Free(SemPool[i]);
        SemPool[i] = nullptr;
    }

    Mutex_Free(MPQueueLock);
}

void LocalMP::Begin(int inst)
{
    if (!ValidInstance(inst)) return;
    Mutex_Lock(MPQueueLock);
    PacketReadOffset[inst] = MPStatus.PacketWriteOffset;
    PacketPendingBytes[inst] = 0;
    ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset[inst];
    LastHostIDs[inst] = -1;
    Associated[inst] = false;
    ResetSemaphore(SemPool[inst]);
    ResetSemaphore(SemPool[kMaxLocalMPInstances + inst]);
    MPStatus.ConnectedBitmask.Set(inst);
    Mutex_Unlock(MPQueueLock);
}

void LocalMP::End(int inst)
{
    if (!ValidInstance(inst)) return;
    Mutex_Lock(MPQueueLock);
    MPStatus.ConnectedBitmask.Reset(inst);
    LastHostIDs[inst] = -1;
    Associated[inst] = false;
    Mutex_Unlock(MPQueueLock);
}

void LocalMP::FIFORead(int inst, int fifo, void* buf, int len) noexcept
{
    u8* data;

    u32 offset, datalen;
    if (fifo == 0)
    {
        offset = PacketReadOffset[inst];
        data = MPPacketQueue;
        datalen = kPacketQueueSize;
    }
    else
    {
        offset = ReplyReadOffset[inst];
        data = MPReplyQueue.get() + inst * kReplyQueueSize;
        datalen = kReplyQueueSize;
    }

    if ((offset + len) >= datalen)
    {
        u32 part1 = datalen - offset;
        memcpy(buf, &data[offset], part1);
        memcpy(&((u8*)buf)[part1], data, len - part1);
        offset = len - part1;
    }
    else
    {
        memcpy(buf, &data[offset], len);
        offset += len;
    }

    if (fifo == 0)
    {
        PacketReadOffset[inst] = offset;
        PacketPendingBytes[inst] -= len;
    }
    else           ReplyReadOffset[inst] = offset;
}

void LocalMP::FIFOWrite(int inst, int fifo, void* buf, int len) noexcept
{
    u8* data;

    u32 offset, datalen;
    if (fifo == 0)
    {
        offset = MPStatus.PacketWriteOffset;
        data = MPPacketQueue;
        datalen = kPacketQueueSize;
    }
    else
    {
        offset = MPStatus.ReplyWriteOffset[inst];
        data = MPReplyQueue.get() + inst * kReplyQueueSize;
        datalen = kReplyQueueSize;
    }

    if ((offset + len) >= datalen)
    {
        u32 part1 = datalen - offset;
        memcpy(&data[offset], buf, part1);
        memcpy(data, &((u8*)buf)[part1], len - part1);
        offset = len - part1;
    }
    else
    {
        memcpy(&data[offset], buf, len);
        offset += len;
    }

    if (fifo == 0) MPStatus.PacketWriteOffset = offset;
    else           MPStatus.ReplyWriteOffset[inst] = offset;
}

int LocalMP::SendPacketGeneric(int inst, u32 type, u8* packet, int len, u64 timestamp) noexcept
{
    if (!ValidInstance(inst) || len < 0 || (u32)len > kMaxFrameSize || (len && !packet))
    {
        Log(LogLevel::Warn, "wifi: attempting to send frame too big (len=%d max=%d)\n", len, kMaxFrameSize);
        return 0;
    }

    Mutex_Lock(MPQueueLock);

    const auto mask = MPStatus.ConnectedBitmask;
    const u32 packetType = type & 0xFFFF;
    const int host = LastHostIDs[inst];
    if (packetType == 2 && !mask.Test(host))
    {
        Mutex_Unlock(MPQueueLock);
        return 0;
    }

    if (packet && len >= 36 && ((FrameControl(packet, len) >> 2) & 3) != 1)
        memcpy(LocalMACs[inst].data(), packet + 22, 6);
    if (packetType == 0 && len >= 36)
    {
        const u16 fc = FrameControl(packet, len) & 0x00FC;
        if (fc == 0 || fc == 0x20 || fc == 0xB0 || fc == 0xA0 || fc == 0xC0)
        {
            Associated[inst] = false;
            LastHostIDs[inst] = -1;
        }
    }

    // Recover only the lagging reader before unread data is overwritten.
    const u32 frameBytes = sizeof(MPPacketHeader) + len;
    if (packetType != 2)
    {
        for (int i = 0; i < kMaxLocalMPInstances; ++i)
        {
            if (!mask.Test(i)) continue;
            if (PacketPendingBytes[i] + frameBytes > kPacketQueueSize)
            {
                PacketReadOffset[i] = MPStatus.PacketWriteOffset;
                PacketPendingBytes[i] = 0;
                ResetSemaphore(SemPool[i]);
            }
            PacketPendingBytes[i] += frameBytes;
        }
    }

    MPPacketHeader pktheader;
    pktheader.Magic = 0x4946494E;
    pktheader.SenderID = inst;
    pktheader.Type = type;
    pktheader.Length = len;
    pktheader.Timestamp = timestamp;

    type &= 0xFFFF;
    int nfifo = (type == 2) ? 1 : 0;
    const int destination = type == 2 ? host : inst;
    FIFOWrite(destination, nfifo, &pktheader, sizeof(pktheader));
    if (len)
        FIFOWrite(destination, nfifo, packet, len);

    if (type == 1)
    {
        LastHostIDs[inst] = -1;
        Associated[inst] = false;
        ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset[inst];
        ResetSemaphore(SemPool[kMaxLocalMPInstances + inst]);
    }

    if (type == 2)
    {
        Semaphore_Post(SemPool[kMaxLocalMPInstances + host]);
    }
    else
    {
        for (int i = 0; i < kMaxLocalMPInstances; i++)
        {
            if (mask.Test(i))
                Semaphore_Post(SemPool[i]);
        }
    }

    // Posting and queue writes share the lock, including backlog resets.
    Mutex_Unlock(MPQueueLock);
    return len;
}

int LocalMP::RecvPacketGeneric(int inst, u8* packet, bool block, u64* timestamp) noexcept
{
    if (!ValidInstance(inst) || !packet) return 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(RecvTimeout);
    for (;;)
    {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if ((block && remaining <= 0) || !Semaphore_TryWait(SemPool[inst], block ? (int)remaining : 0))
        {
            return 0;
        }

        Mutex_Lock(MPQueueLock);

        // A waiter can have taken a token just before a backlog reset.
        if (!PacketPendingBytes[inst])
        {
            Mutex_Unlock(MPQueueLock);
            continue;
        }

        MPPacketHeader pktheader = {};
        if (PacketPendingBytes[inst] >= sizeof(pktheader))
            FIFORead(inst, 0, &pktheader, sizeof(pktheader));

        if (pktheader.Magic != 0x4946494E || pktheader.Length > kMaxFrameSize ||
            pktheader.Length > PacketPendingBytes[inst] || pktheader.SenderID >= kMaxLocalMPInstances)
        {
            Log(LogLevel::Warn, "PACKET FIFO OVERFLOW\n");
            PacketReadOffset[inst] = MPStatus.PacketWriteOffset;
            PacketPendingBytes[inst] = 0;
            ResetSemaphore(SemPool[inst]);
            Mutex_Unlock(MPQueueLock);
            return 0;
        }

        if (pktheader.SenderID == (u32)inst)
        {
            // skip this packet
            PacketReadOffset[inst] += pktheader.Length;
            PacketPendingBytes[inst] -= pktheader.Length;
            if (PacketReadOffset[inst] >= kPacketQueueSize)
                PacketReadOffset[inst] -= kPacketQueueSize;

            Mutex_Unlock(MPQueueLock);
            continue;
        }

        if (pktheader.Length)
        {
            FIFORead(inst, 0, packet, pktheader.Length);

        }

        const bool hasMAC = pktheader.Length >= 36;
        if (pktheader.Length >= 42 && pktheader.Timestamp && (FrameControl(packet, pktheader.Length) & 0x00FF) == 0x10 &&
            memcmp(packet + 16, LocalMACs[inst].data(), 6) == 0 &&
            packet[38] == 0 && packet[39] == 0 && (packet[40] || packet[41]))
        {
            Associated[inst] = true;
            memcpy(HostMACs[inst].data(), packet + 22, 6);
            LastHostIDs[inst] = pktheader.SenderID;
        }

        const bool foreign = Associated[inst] && (!hasMAC || memcmp(packet + 22, HostMACs[inst].data(), 6) != 0);
        if (foreign && (pktheader.Type == 1 || pktheader.Type == 3))
        {
            Mutex_Unlock(MPQueueLock);
            continue;
        }
        if (!foreign && ((Associated[inst] && hasMAC) || pktheader.Type == 1))
            LastHostIDs[inst] = pktheader.SenderID;

        // Keep other BSSs visible without adopting their parent clocks.
        if (timestamp) *timestamp = foreign ? 0 : pktheader.Timestamp;
        if ((FrameControl(packet, pktheader.Length) & 0x00FC) == 0xC0 &&
            hasMAC && memcmp(packet + 16, LocalMACs[inst].data(), 6) == 0)
        {
            Associated[inst] = false;
            LastHostIDs[inst] = -1;
        }
        Mutex_Unlock(MPQueueLock);
        return pktheader.Length;
    }
}

int LocalMP::SendPacket(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 0, packet, len, timestamp);
}

int LocalMP::RecvPacket(int inst, u8* packet, u64* timestamp)
{
    return RecvPacketGeneric(inst, packet, false, timestamp);
}

int LocalMP::SendCmd(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 1, packet, len, timestamp);
}

int LocalMP::SendReply(int inst, u8* packet, int len, u64 timestamp, u16 aid)
{
    return SendPacketGeneric(inst, 2 | (aid<<16), packet, len, timestamp);
}

int LocalMP::SendAck(int inst, u8* packet, int len, u64 timestamp)
{
    return SendPacketGeneric(inst, 3, packet, len, timestamp);
}

int LocalMP::RecvHostPacket(int inst, u8* packet, u64* timestamp)
{
    if (!ValidInstance(inst)) return 0;
    Mutex_Lock(MPQueueLock);
    const int host = LastHostIDs[inst];
    const bool lostHost = host >= 0 && !MPStatus.ConnectedBitmask.Test(host);
    Mutex_Unlock(MPQueueLock);
    if (lostHost) return -1;

    return RecvPacketGeneric(inst, packet, true, timestamp);
}

u16 LocalMP::RecvReplies(int inst, u8* packets, u64 timestamp, u16 aidmask)
{
    if (!ValidInstance(inst) || !packets) return 0;
    u16 ret = 0;
    InstanceMask myinstmask {};
    myinstmask.Set(inst);

    Mutex_Lock(MPQueueLock);
    const auto curinstmask = MPStatus.ConnectedBitmask;
    Mutex_Unlock(MPQueueLock);

    // if all clients have left: return early
    if (myinstmask.Contains(curinstmask))
        return 0;

    for (;;)
    {
        if (!Semaphore_TryWait(SemPool[kMaxLocalMPInstances + inst], RecvTimeout))
        {
            // no more replies available
            return ret;
        }

        Mutex_Lock(MPQueueLock);

        MPPacketHeader pktheader = {};
        FIFORead(inst, 1, &pktheader, sizeof(pktheader));

        if (pktheader.Magic != 0x4946494E || pktheader.Length > 1024 || pktheader.SenderID >= kMaxLocalMPInstances)
        {
            Log(LogLevel::Warn, "REPLY FIFO OVERFLOW\n");
            ReplyReadOffset[inst] = MPStatus.ReplyWriteOffset[inst];
            ResetSemaphore(SemPool[kMaxLocalMPInstances + inst]);
            Mutex_Unlock(MPQueueLock);
            return 0;
        }

        const u32 aid = pktheader.Type >> 16;
        if (pktheader.SenderID == (u32)inst ||
            (timestamp > 32 && pktheader.Timestamp < timestamp - 32) ||
            (pktheader.Length && (aid == 0 || aid > 15)))
        {
            // skip this packet
            ReplyReadOffset[inst] += pktheader.Length;
            if (ReplyReadOffset[inst] >= kReplyQueueSize)
                ReplyReadOffset[inst] -= kReplyQueueSize;

            Mutex_Unlock(MPQueueLock);
            continue;
        }

        if (pktheader.Length)
        {
            FIFORead(inst, 1, &packets[(aid-1)*1024], pktheader.Length);
            ret |= (1 << aid);
        }

        myinstmask.Set(pktheader.SenderID);
        if (myinstmask.Contains(curinstmask) ||
            ((ret & aidmask) == aidmask))
        {
            // all the clients have sent their reply

            Mutex_Unlock(MPQueueLock);
            return ret;
        }

        Mutex_Unlock(MPQueueLock);
    }
}

}

