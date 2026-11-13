// SPDX-License-Identifier: GPL-3.0-or-later
#include "IRSerial.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace melonDS
{
struct IRSerial::State
{
    explicit State(const std::string& path) : Path(path) {}
    ~State() { Close(); }
    void Close()
    {
#ifdef _WIN32
        if (Handle != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(Handle, nullptr);
            DWORD ignored;
            // The OVERLAPPED buffers must outlive canceled driver operations.
            if (Reading) GetOverlappedResult(Handle, &ReadOp, &ignored, TRUE);
            if (Writing) GetOverlappedResult(Handle, &WriteOp, &ignored, TRUE);
            CloseHandle(Handle);
            Handle = INVALID_HANDLE_VALUE;
        }
        if (ReadOp.hEvent) CloseHandle(ReadOp.hEvent);
        if (WriteOp.hEvent) CloseHandle(WriteOp.hEvent);
        ReadOp = {};
        WriteOp = {};
        Reading = Writing = false;
#else
        if (Handle >= 0) close(Handle);
        Handle = -1;
#endif
        Outgoing.clear();
        Offset = 0;
    }
    bool Open()
    {
#ifdef _WIN32
        if (Handle != INVALID_HANDLE_VALUE) return true;
#else
        if (Handle >= 0) return true;
#endif
        auto now = std::chrono::steady_clock::now();
        if (Path.empty() || now < RetryAt) return false;
        RetryAt = now + std::chrono::seconds(1);
#ifdef _WIN32
        auto path = Path;
        if (path.size() > 3 && _strnicmp(path.c_str(), "COM", 3) == 0) path = "\\\\.\\" + path;
        Handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (Handle == INVALID_HANDLE_VALUE) return false;
        DCB dcb {};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(Handle, &dcb)) { Close(); return false; }
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fTXContinueOnXoff = TRUE;
        dcb.fOutX = dcb.fInX = dcb.fErrorChar = dcb.fNull = FALSE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fAbortOnError = FALSE;
        COMMTIMEOUTS timeouts {};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.WriteTotalTimeoutConstant = 100;
        ReadOp.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        WriteOp.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!ReadOp.hEvent || !WriteOp.hEvent || !SetCommState(Handle, &dcb) ||
            !SetCommTimeouts(Handle, &timeouts)) { Close(); return false; }
        PurgeComm(Handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
#else
        Handle = open(Path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (Handle < 0) return false;
        termios settings {};
        if (tcgetattr(Handle, &settings) < 0) { Close(); return false; }
        cfmakeraw(&settings);
        cfsetispeed(&settings, B115200);
        cfsetospeed(&settings, B115200);
        settings.c_cflag = (settings.c_cflag & ~(CSIZE | PARENB | CSTOPB | CRTSCTS)) | CS8 | CLOCAL | CREAD;
        settings.c_cc[VMIN] = settings.c_cc[VTIME] = 0;
        if (tcsetattr(Handle, TCSANOW, &settings) < 0) { Close(); return false; }
        int flags = TIOCM_DTR;
        ioctl(Handle, TIOCMBIS, &flags);
        flags = TIOCM_RTS;
        ioctl(Handle, TIOCMBIC, &flags);
        tcflush(Handle, TCIOFLUSH);
#endif
        return true;
    }
    void Flush()
    {
        for (int i = 0; i < 8 && !Outgoing.empty(); ++i)
        {
            auto& packet = Outgoing.front();
#ifdef _WIN32
            DWORD written = 0;
            if (Writing)
            {
                if (!GetOverlappedResult(Handle, &WriteOp, &written, FALSE))
                {
                    if (GetLastError() == ERROR_IO_INCOMPLETE) return;
                    Writing = false;
                    Close();
                    return;
                }
                Writing = false;
            }
            else
            {
                ResetEvent(WriteOp.hEvent);
                if (!WriteFile(Handle, packet.data() + Offset, packet.size() - Offset, &written, &WriteOp))
                {
                    if (GetLastError() == ERROR_IO_PENDING) { Writing = true; return; }
                    Close();
                    return;
                }
            }
#else
            auto written = write(Handle, packet.data() + Offset, packet.size() - Offset);
            if (written < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) Close();
                return;
            }
#endif
            Offset += written;
            if (Offset == packet.size()) { Outgoing.pop_front(); Offset = 0; }
            if (!written) return;
        }
    }
    std::string Path;
    std::chrono::steady_clock::time_point RetryAt {};
    std::deque<std::vector<char>> Outgoing;
    std::size_t Offset = 0;
#ifdef _WIN32
    HANDLE Handle = INVALID_HANDLE_VALUE;
    OVERLAPPED ReadOp {}, WriteOp {};
    std::array<char, 255> Input {};
    bool Reading = false, Writing = false;
#else
    int Handle = -1;
#endif
};

IRSerial::IRSerial(const std::string& path) : state(std::make_unique<State>(path)) {}
IRSerial::~IRSerial() = default;
u8 IRSerial::Send(const char* data, int length)
{
    auto& s = *state;
    if (!data || length <= 0 || length > 0x88 || !s.Open()) return 0;
    s.Flush();
    if (!s.Open() || s.Outgoing.size() >= 32) return 0;
    s.Outgoing.emplace_back(data, data + length);
    s.Flush();
    return static_cast<u8>(length);
}
u8 IRSerial::Receive(char* data, int capacity)
{
    auto& s = *state;
    if (!data || capacity <= 0 || !s.Open()) return 0;
    s.Flush();
    if (!s.Open()) return 0;
    capacity = std::min(capacity, 255);
#ifdef _WIN32
    DWORD received = 0;
    if (s.Reading)
    {
        if (!GetOverlappedResult(s.Handle, &s.ReadOp, &received, FALSE))
        {
            if (GetLastError() == ERROR_IO_INCOMPLETE) return 0;
            s.Reading = false;
            s.Close();
            return 0;
        }
        s.Reading = false;
    }
    else
    {
        ResetEvent(s.ReadOp.hEvent);
        if (!ReadFile(s.Handle, s.Input.data(), capacity, &received, &s.ReadOp))
        {
            if (GetLastError() == ERROR_IO_PENDING) { s.Reading = true; return 0; }
            s.Close();
            return 0;
        }
    }
    received = std::min<DWORD>(received, capacity);
    std::memcpy(data, s.Input.data(), received);
#else
    auto received = read(s.Handle, data, capacity);
    if (received < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) s.Close();
        return 0;
    }
#endif
    return static_cast<u8>(received);
}
}
