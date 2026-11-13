// SPDX-License-Identifier: GPL-3.0-or-later
#include "IRTransport.h"
#include "IRSerial.h"
#include "InstanceMask.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>
#include <enet/enet.h>

namespace melonDS
{
namespace
{
constexpr int PacketLimit = 0x88;
constexpr std::size_t QueueLimit = 32;
using Clock = std::chrono::steady_clock;
struct Endpoint { std::deque<std::vector<char>> Packets; };
struct LocalBus
{
    std::mutex Mutex;
    std::array<Endpoint*, kMaxLocalInstances> Slots {};
};
LocalBus& Bus() { static LocalBus bus; return bus; }
bool Ready(ENetSocket socket, enet_uint32 event)
{
    auto events = event;
    return enet_socket_wait(socket, &events, 0) == 0 && (events & event);
}
ENetBuffer Buffer(void* data, std::size_t size)
{
    ENetBuffer result {};
    result.data = data;
    result.dataLength = size;
    return result;
}
}

struct IRTransport::State
{
    explicit State(const IRSettings& settings) : Settings(settings)
    {
        if (Settings.Mode == IRMode::TCP || Settings.Mode == IRMode::ENet)
            Initialized = enet_initialize() == 0;
        else if (Settings.Mode == IRMode::Serial)
            Serial = std::make_unique<IRSerial>(Settings.SerialPath);
        else if (Settings.Mode == IRMode::Local)
        {
            auto& bus = Bus();
            std::lock_guard lock(bus.Mutex);
            for (int i = 0; i < kMaxLocalInstances; ++i)
            {
                if (Settings.LocalSlot >= 0 && Settings.LocalSlot != i) continue;
                if (bus.Slots[i]) continue;
                Slot = i;
                bus.Slots[i] = &Local;
                break;
            }
        }
    }
    ~State()
    {
        if (Peer) enet_peer_disconnect_now(Peer, 0);
        if (Host) enet_host_destroy(Host);
        if (Socket != ENET_SOCKET_NULL) enet_socket_destroy(Socket);
        if (Listener != ENET_SOCKET_NULL) enet_socket_destroy(Listener);
        if (Initialized) enet_deinitialize();
        if (Slot >= 0)
        {
            auto& bus = Bus();
            std::lock_guard lock(bus.Mutex);
            bus.Slots[Slot] = nullptr;
            if (auto* peer = bus.Slots[Slot ^ 1]) peer->Packets.clear();
        }
    }
    void Disconnect()
    {
        if (Socket != ENET_SOCKET_NULL) enet_socket_destroy(Socket);
        Socket = ENET_SOCKET_NULL;
        Connecting = false;
        Pending.clear();
        RetryAt = Clock::now() + std::chrono::seconds(1);
    }
    void PollTCP()
    {
        if (!Initialized) return;
        const auto now = Clock::now();
        if (Settings.Server)
        {
            if (Listener == ENET_SOCKET_NULL)
            {
                if (now < RetryAt) return;
                RetryAt = now + std::chrono::seconds(1);
                ENetAddress address {ENET_HOST_ANY, Settings.SelfPort};
                Listener = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
                if (Listener == ENET_SOCKET_NULL) return;
                if (enet_socket_set_option(Listener, ENET_SOCKOPT_NONBLOCK, 1) < 0 ||
                    enet_socket_bind(Listener, &address) < 0 || enet_socket_listen(Listener, 1) < 0)
                {
                    enet_socket_destroy(Listener);
                    Listener = ENET_SOCKET_NULL;
                    return;
                }
            }
            if (Socket == ENET_SOCKET_NULL && Ready(Listener, ENET_SOCKET_WAIT_RECEIVE))
            {
                Socket = enet_socket_accept(Listener, nullptr);
                if (Socket != ENET_SOCKET_NULL)
                {
                    if (enet_socket_set_option(Socket, ENET_SOCKOPT_NONBLOCK, 1) < 0)
                    { Disconnect(); return; }
                    enet_socket_set_option(Socket, ENET_SOCKOPT_NODELAY, 1);
                }
            }
        }
        else
        {
            if (Socket == ENET_SOCKET_NULL)
            {
                if (now < RetryAt) return;
                RetryAt = now + std::chrono::seconds(1);
                ENetAddress address {};
                address.port = Settings.HostPort;
                if (enet_address_set_host_ip(&address, Settings.HostIP.c_str()) < 0) return;
                Socket = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
                if (Socket == ENET_SOCKET_NULL) return;
                if (enet_socket_set_option(Socket, ENET_SOCKOPT_NONBLOCK, 1) < 0 ||
                    enet_socket_connect(Socket, &address) < 0)
                { Disconnect(); return; }
                enet_socket_set_option(Socket, ENET_SOCKOPT_NODELAY, 1);
                Connecting = true;
                ConnectDeadline = now + std::chrono::seconds(3);
            }
            if (Connecting)
            {
                int error = 0;
                if (enet_socket_get_option(Socket, ENET_SOCKOPT_ERROR, &error) < 0 || error || now > ConnectDeadline)
                { Disconnect(); return; }
                if (!Ready(Socket, ENET_SOCKET_WAIT_SEND)) return;
                Connecting = false;
            }
        }
        Flush();
    }
    void Flush()
    {
        if (Socket == ENET_SOCKET_NULL || Connecting || Pending.empty()) return;
        auto buffer = Buffer(Pending.data(), Pending.size());
        const int written = enet_socket_send(Socket, nullptr, &buffer, 1);
        if (written < 0) Disconnect();
        else if (written > 0) Pending.erase(Pending.begin(), Pending.begin() + written);
    }
    void PollENet()
    {
        if (!Initialized) return;
        const auto now = Clock::now();
        if (!Host)
        {
            if (now < RetryAt) return;
            RetryAt = now + std::chrono::seconds(1);
            ENetAddress address {ENET_HOST_ANY, Settings.SelfPort};
            Host = enet_host_create(Settings.Server ? &address : nullptr, 1, 2, 0, 0);
            if (!Host) return;
        }
        if (!Settings.Server && !Peer && now >= RetryAt)
        {
            RetryAt = now + std::chrono::seconds(1);
            ENetAddress address {};
            address.port = Settings.HostPort;
            if (enet_address_set_host_ip(&address, Settings.HostIP.c_str()) < 0) return;
            Peer = enet_host_connect(Host, &address, 2, 0);
            ConnectDeadline = now + std::chrono::seconds(3);
        }
        ENetEvent event {};
        // Bound work per emulation poll, including traffic from a noisy peer.
        for (int i = 0; i < 64 && enet_host_service(Host, &event, 0) > 0; ++i)
        {
            if (event.type == ENET_EVENT_TYPE_CONNECT)
            {
                Peer = event.peer;
                Connected = true;
                Local.Packets.clear();
            }
            else if (event.type == ENET_EVENT_TYPE_RECEIVE)
            {
                if (event.peer == Peer && event.packet->dataLength <= PacketLimit && Local.Packets.size() < QueueLimit)
                    Local.Packets.emplace_back(event.packet->data, event.packet->data + event.packet->dataLength);
                enet_packet_destroy(event.packet);
            }
            else if (event.type == ENET_EVENT_TYPE_DISCONNECT && event.peer == Peer)
            {
                Peer = nullptr;
                Connected = false;
                Local.Packets.clear();
                RetryAt = now + std::chrono::seconds(1);
            }
        }
        if (!Settings.Server && Peer && !Connected && now > ConnectDeadline)
        {
            enet_peer_reset(Peer);
            Peer = nullptr;
            RetryAt = now + std::chrono::seconds(1);
        }
    }
    u8 Pop(char* data, int capacity)
    {
        if (Local.Packets.empty()) return 0;
        const auto& packet = Local.Packets.front();
        const int count = std::min<int>(capacity, packet.size());
        std::memcpy(data, packet.data(), count);
        Local.Packets.pop_front();
        return static_cast<u8>(count);
    }
    IRSettings Settings;
    bool Initialized = false;
    bool Connecting = false;
    bool Connected = false;
    ENetSocket Listener = ENET_SOCKET_NULL;
    ENetSocket Socket = ENET_SOCKET_NULL;
    ENetHost* Host = nullptr;
    ENetPeer* Peer = nullptr;
    Clock::time_point RetryAt {}, ConnectDeadline {};
    std::vector<char> Pending;
    std::unique_ptr<IRSerial> Serial;
    Endpoint Local;
    int Slot = -1;
    std::mutex Mutex;
};

IRTransport::IRTransport(const IRSettings& settings) : state(std::make_unique<State>(settings)) {}
IRTransport::~IRTransport() = default;

u8 IRTransport::Send(const char* data, int length)
{
    if (!data || length <= 0 || length > PacketLimit) return 0;
    auto& s = *state;
    std::lock_guard stateLock(s.Mutex);
    if (s.Settings.Mode == IRMode::Local)
    {
        auto& bus = Bus();
        std::lock_guard lock(bus.Mutex);
        if (s.Slot < 0) return 0;
        auto* peer = bus.Slots[s.Slot ^ 1];
        if (!peer || peer->Packets.size() >= QueueLimit) return 0;
        peer->Packets.emplace_back(data, data + length);
        return static_cast<u8>(length);
    }
    if (s.Settings.Mode == IRMode::Serial) return s.Serial->Send(data, length);
    if (s.Settings.Mode == IRMode::ENet)
    {
        s.PollENet();
        if (!s.Peer || !s.Connected) return 0;
        auto* packet = enet_packet_create(data, length, ENET_PACKET_FLAG_UNSEQUENCED);
        if (!packet) return 0;
        if (enet_peer_send(s.Peer, 0, packet) < 0) { enet_packet_destroy(packet); return 0; }
        enet_host_flush(s.Host);
        return static_cast<u8>(length);
    }
    s.PollTCP();
    if (s.Socket == ENET_SOCKET_NULL || s.Connecting || s.Pending.size() + length > PacketLimit * QueueLimit) return 0;
    s.Pending.insert(s.Pending.end(), data, data + length);
    s.Flush();
    return s.Socket == ENET_SOCKET_NULL ? 0 : static_cast<u8>(length);
}

u8 IRTransport::Receive(char* data, int capacity)
{
    if (!data || capacity <= 0) return 0;
    capacity = std::min(capacity, 255);
    auto& s = *state;
    std::lock_guard stateLock(s.Mutex);
    if (s.Settings.Mode == IRMode::Local)
    {
        auto& bus = Bus();
        std::lock_guard lock(bus.Mutex);
        return s.Pop(data, capacity);
    }
    if (s.Settings.Mode == IRMode::Serial) return s.Serial->Receive(data, capacity);
    if (s.Settings.Mode == IRMode::ENet) { s.PollENet(); return s.Pop(data, capacity); }
    s.PollTCP();
    if (s.Socket == ENET_SOCKET_NULL || s.Connecting || !Ready(s.Socket, ENET_SOCKET_WAIT_RECEIVE)) return 0;
    auto buffer = Buffer(data, capacity);
    const int count = enet_socket_receive(s.Socket, nullptr, &buffer, 1);
    if (count <= 0) { s.Disconnect(); return 0; }
    return static_cast<u8>(count);
}
}
