#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

#pragma comment(lib, "Ws2_32.lib")

// -----------------------------------------------------------------------------

class CWinsockStart
{
public:
    CWinsockStart() noexcept
    {
        WSADATA sWsaData;

        m_hRes = HRESULT_FROM_WIN32((DWORD)::WSAStartup(MAKEWORD(2, 2), &sWsaData));
    }

    CWinsockStart(_In_ const CWinsockStart &) = delete;
    CWinsockStart(_Inout_ CWinsockStart &&) = delete;

    ~CWinsockStart() noexcept
    {
        if (SUCCEEDED(m_hRes))
        {
            ::WSACleanup();
        }
    }

    CWinsockStart &operator=(_In_ const CWinsockStart &) = delete;
    CWinsockStart &operator=(_Inout_ CWinsockStart &&) = delete;

    HRESULT HResult() const noexcept
    {
        return m_hRes;
    }

    BOOL Succeeded() const noexcept
    {
        return (SUCCEEDED(m_hRes)) ? TRUE : FALSE;
    }

private:
    HRESULT m_hRes{ E_FAIL };
};
