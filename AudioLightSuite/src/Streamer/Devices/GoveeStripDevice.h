#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

// -----------------------------------------------------------------------------

class CGoveeStripDevice
{
public:
    CGoveeStripDevice() = default;
    CGoveeStripDevice(_In_ const CGoveeStripDevice &) = delete;
    CGoveeStripDevice(_Inout_ CGoveeStripDevice &&) = delete;
    ~CGoveeStripDevice() noexcept;

    CGoveeStripDevice &operator=(_In_ const CGoveeStripDevice &) = delete;
    CGoveeStripDevice &operator=(_Inout_ CGoveeStripDevice &&) = delete;

    HRESULT SetupEndpoint(_In_z_ LPCWSTR szHostAndPortW) noexcept;
    BOOL SendPacket(_In_reads_bytes_(nDataSize) LPCVOID lpData, _In_ SIZE_T nDataSize) noexcept;
    BOOL IsReady() const noexcept;
    VOID Close() noexcept;

private:
    SOCKET m_hSocket{ INVALID_SOCKET };
    SOCKADDR_STORAGE m_sDestination{};
    INT m_iDestinationLen{ 0 };
};
