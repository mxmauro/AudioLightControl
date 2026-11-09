#include "GoveeStripDevice.h"

// -----------------------------------------------------------------------------

static HRESULT ResolveEndpoint(_In_z_ LPCWSTR szHostAndPortW, _Out_ SOCKADDR_STORAGE &sDestination, _Out_ INT &iDestinationLen) noexcept;

// -----------------------------------------------------------------------------

CGoveeStripDevice::~CGoveeStripDevice() noexcept
{
    Close();
}

HRESULT CGoveeStripDevice::SetupEndpoint(_In_z_ LPCWSTR szHostAndPortW) noexcept
{
    SOCKADDR_STORAGE sDestination{};
    INT iDestinationLen = 0;
    SOCKET hSocket;
    HRESULT hRes;

    hRes = ResolveEndpoint(szHostAndPortW, sDestination, iDestinationLen);
    if (FAILED(hRes))
    {
        return hRes;
    }
    hSocket = ::socket(sDestination.ss_family, SOCK_DGRAM, IPPROTO_UDP);
    if (hSocket == INVALID_SOCKET)
    {
        return HRESULT_FROM_WIN32(WSAGetLastError());
    }
    Close();
    m_hSocket = hSocket;
    m_sDestination = sDestination;
    m_iDestinationLen = iDestinationLen;
    return S_OK;
}

BOOL CGoveeStripDevice::SendPacket(_In_reads_bytes_(nDataSize) LPCVOID lpData, _In_ SIZE_T nDataSize) noexcept
{
    int iBytesSent;

    if ((!lpData) || (!IsReady()) || nDataSize == 0 || nDataSize > static_cast<SIZE_T>(INT_MAX))
    {
        return FALSE;
    }
    iBytesSent = ::sendto(m_hSocket, reinterpret_cast<const char *>(lpData), static_cast<int>(nDataSize), 0,
                          reinterpret_cast<const SOCKADDR *>(&m_sDestination), m_iDestinationLen);
    return (iBytesSent == static_cast<int>(nDataSize)) ? TRUE : FALSE;
}

BOOL CGoveeStripDevice::IsReady() const noexcept
{
    return m_hSocket != INVALID_SOCKET ? TRUE : FALSE;
}

VOID CGoveeStripDevice::Close() noexcept
{
    if (m_hSocket != INVALID_SOCKET)
    {
        ::closesocket(m_hSocket);
        m_hSocket = INVALID_SOCKET;
    }
    m_sDestination = {};
    m_iDestinationLen = 0;
}

// -----------------------------------------------------------------------------

static HRESULT ResolveEndpoint(_In_z_ LPCWSTR szHostAndPortW, _Out_ SOCKADDR_STORAGE &sDestination, _Out_ INT &iDestinationLen) noexcept
{
    WCHAR szHostW[256];
    WCHAR szPortW[6];
    ADDRINFOW sHints{};
    PADDRINFOW lpAddrInfoW = nullptr;
    LPCWSTR szPortStartW;
    SIZE_T nHostLen;
    SIZE_T nPortLen;
    INT iResult;

    if ((!szHostAndPortW) || *szHostAndPortW == 0)
    {
        return E_INVALIDARG;
    }
    if (szHostAndPortW[0] == L'[')
    {
        LPCWSTR szClosingBracketW = wcschr(szHostAndPortW, L']');

        if ((!szClosingBracketW) || szClosingBracketW[1] != L':')
        {
            return E_INVALIDARG;
        }
        nHostLen = static_cast<SIZE_T>(szClosingBracketW - szHostAndPortW - 1);
        szPortStartW = szClosingBracketW + 2;
    }
    else
    {
        LPCWSTR szColonW = wcsrchr(szHostAndPortW, L':');

        if (!szColonW)
        {
            return E_INVALIDARG;
        }
        nHostLen = static_cast<SIZE_T>(szColonW - szHostAndPortW);
        szPortStartW = szColonW + 1;
    }
    nPortLen = wcslen(szPortStartW);
    if (nHostLen == 0 || nHostLen >= sizeof(szHostW) / sizeof(szHostW[0]) || nPortLen == 0 ||
        nPortLen >= sizeof(szPortW) / sizeof(szPortW[0]))
    {
        return E_INVALIDARG;
    }
    wcsncpy_s(szHostW, szHostAndPortW + (szHostAndPortW[0] == L'[' ? 1 : 0), nHostLen);
    wcsncpy_s(szPortW, szPortStartW, nPortLen);
    sHints.ai_family = AF_UNSPEC;
    sHints.ai_socktype = SOCK_DGRAM;
    sHints.ai_protocol = IPPROTO_UDP;
    iResult = ::GetAddrInfoW(szHostW, szPortW, &sHints, &lpAddrInfoW);
    if (iResult != 0 || !lpAddrInfoW || lpAddrInfoW->ai_addrlen > sizeof(sDestination))
    {
        if (lpAddrInfoW)
        {
            ::FreeAddrInfoW(lpAddrInfoW);
        }
        return HRESULT_FROM_WIN32(WSAHOST_NOT_FOUND);
    }
    memcpy(&sDestination, lpAddrInfoW->ai_addr, lpAddrInfoW->ai_addrlen);
    iDestinationLen = static_cast<INT>(lpAddrInfoW->ai_addrlen);
    ::FreeAddrInfoW(lpAddrInfoW);
    return S_OK;
}
