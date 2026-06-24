#include "CommandLine.h"
#include <stdio.h>

// -----------------------------------------------------------------------------

static CmdLimeParameters sCmdLineParams;

// -----------------------------------------------------------------------------

HRESULT ParseCmdLimeParameters(_In_ INT argc, _In_reads_(argc) LPWSTR argvW[]) noexcept
{
    sCmdLineParams.strInputFilePathW.clear();
    sCmdLineParams.strOutputFilePathW.clear();

    if (!argvW || argc < 2)
    {
        fwprintf(stderr, L"Error 0x%08X: Missing or invalid input file.\n", (unsigned int)E_INVALIDARG);
        return E_INVALIDARG;
    }

    for (INT i = 1; i < argc; i++)
    {
        if (_wcsicmp(argvW[i], L"--input") == 0)
        {
            if (!sCmdLineParams.strInputFilePathW.empty())
            {
                fwprintf(stderr, L"Error 0x%08X: --input parameter specified multiple times.\n", (unsigned int)E_INVALIDARG);
                return E_INVALIDARG;
            }
            if (i + 1 >= argc || (!argvW[i + 1]) || argvW[i + 1][0] == 0)
            {
                fwprintf(stderr, L"Error 0x%08X: Missing or invalid input file.\n", (unsigned int)E_INVALIDARG);
                return E_INVALIDARG;
            }
            sCmdLineParams.strInputFilePathW = argvW[++i];
        }
        else if (_wcsicmp(argvW[i], L"--output") == 0)
        {
            if (!sCmdLineParams.strOutputFilePathW.empty())
            {
                fwprintf(stderr, L"Error 0x%08X: --output parameter specified multiple times.\n", (unsigned int)E_INVALIDARG);
                return E_INVALIDARG;
            }
            if (i + 1 >= argc || (!argvW[i + 1]) || argvW[i + 1][0] == 0)
            {
                fwprintf(stderr, L"Error 0x%08X: Missing or invalid output file.\n", (unsigned int)E_INVALIDARG);
                return E_INVALIDARG;
            }
            sCmdLineParams.strOutputFilePathW = argvW[++i];
        }
        else
        {
            fwprintf(stderr, L"Error 0x%08X: Unknown command line parameter.\n", (unsigned int)E_INVALIDARG);
            return E_INVALIDARG;
        }
    }

    if (sCmdLineParams.strInputFilePathW.empty())
    {
        fwprintf(stderr, L"Error 0x%08X: Missing or invalid input file.\n", (unsigned int)E_INVALIDARG);
        return E_INVALIDARG;
    }

    // Done
    return S_OK;
}

const CmdLimeParameters& GetCmdLimeParameters() noexcept
{
    return sCmdLineParams;
}
