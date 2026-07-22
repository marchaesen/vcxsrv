/*
 * winvsock.c - Hyper-V vsock (AF_HYPERV) auto-configuration for WSL2
 *
 * The xtrans "hyperv" transport binds HV_GUID_WILDCARD by default, which
 * WSL2's utility VM never matches (microsoft/WSL issue #5751): the listener
 * must be bound to the WSL2 VM's exact id. That id changes at every WSL VM
 * boot, so this file
 *
 *  - winVsockPreInit: called from OsVendorPreInit (before the listeners are
 *    created). With -vsock, detects the WSL2 VM id unprivileged via
 *    'wsl.exe -- wslinfo --vm-id' and binds it. Without -vsock (and without
 *    an explicit -vmid/-listen hyperv), disables the hyperv listener so it
 *    is not bound to an unusable wildcard address by default.
 *
 *  - winVsockStartWatcher: called from InitOutput (serverGeneration 1).
 *    Starts a worker thread that polls the VM id (~2 s, without ever
 *    starting a stopped WSL VM) plus a main-thread OsTimer that rebinds the
 *    listener when the id changes (WSL restart), or creates it late when
 *    WSL2 only starts after the server.
 *
 * The worker thread never calls into the X server; it only hands a new VM
 * id to the main thread through a small locked state block. All X side
 * effects happen on the main thread via HyperVRebindListener() in
 * os/connection.c.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "win.h"
#include "winglobals.h"

#include "os.h"
#include "dix/dix_priv.h"       /* display */

#ifdef HYPERV

#include <tlhelp32.h>
#include <pthread.h>
#include <ctype.h>

#define XSERV_t
#define TRANS_SERVER
#include <X11/Xtrans/Xtrans.h>

/* implemented in os/connection.c */
extern int HyperVRebindListener(void);

#define WIN_VSOCK_POLL_MS   2000    /* worker poll interval */
#define WIN_VSOCK_TIMER_MS  1000    /* main-thread timer interval */
#define WIN_VSOCK_CMD_MS    5000    /* wsl.exe spawn timeout */
#define WIN_VSOCK_GUID_LEN  40      /* 36 chars + braces + NUL, rounded up */

/* shared state, protected by g_vsockLock */
static CRITICAL_SECTION g_vsockLock;
static BOOL g_vsockLockInited = FALSE;
static char g_vsockBoundGuid[WIN_VSOCK_GUID_LEN];   /* currently bound, "" if none */
static char g_vsockPendingGuid[WIN_VSOCK_GUID_LEN]; /* latest id seen by worker */
static BOOL g_vsockDirty = FALSE;                   /* pending needs binding */

static BOOL g_vsockAuto = FALSE;          /* -vsock auto-detection active */
static BOOL g_vsockWatcherStarted = FALSE;

/*
 * Look for an option in argv, optionally with a specific following argument.
 */
static BOOL
winArgvHas(int argc, char *argv[], const char *opt, const char *optArg)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], opt) == 0) {
            if (!optArg)
                return TRUE;
            if (i + 1 < argc && strcmp(argv[i + 1], optArg) == 0)
                return TRUE;
        }
    }
    return FALSE;
}

/*
 * Run a console command without flashing a window and capture its (small)
 * output. Returns TRUE if the process exited within timeoutMs.
 */
static BOOL
winWslRunCapture(const char *cmdLine, char *out, size_t outLen,
                 DWORD timeoutMs, DWORD *pExitCode)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE hOutR = NULL, hOutW = NULL, hNulR = NULL;
    char cmdBuf[256];
    DWORD wr, total = 0, exitCode = 1;
    BOOL ok = FALSE;

    if (outLen == 0)
        return FALSE;
    out[0] = '\0';

    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&hOutR, &hOutW, &sa, 0))
        return FALSE;
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);

    hNulR = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        &sa, OPEN_EXISTING, 0, NULL);
    if (hNulR == INVALID_HANDLE_VALUE) {
        CloseHandle(hOutR);
        CloseHandle(hOutW);
        return FALSE;
    }

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hNulR;
    si.hStdOutput = hOutW;
    si.hStdError = hOutW;
    ZeroMemory(&pi, sizeof(pi));

    snprintf(cmdBuf, sizeof(cmdBuf), "%s", cmdLine);

    if (!CreateProcessA(NULL, cmdBuf, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        CloseHandle(hOutR);
        CloseHandle(hOutW);
        CloseHandle(hNulR);
        return FALSE;
    }
    /* close our copies so ReadFile below sees EOF when the child exits */
    CloseHandle(hOutW);
    CloseHandle(hNulR);

    wr = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (wr == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
    }
    else if (wr == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
        /* output is tiny (far below the pipe buffer), so draining only
         * after the process exited cannot deadlock */
        for (;;) {
            DWORD rd = 0;

            if (total >= outLen - 1)
                break;
            if (!ReadFile(hOutR, out + total, (DWORD)(outLen - 1 - total),
                          &rd, NULL) || rd == 0)
                break;
            total += rd;
        }
        out[total] = '\0';
        ok = TRUE;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hOutR);

    if (pExitCode)
        *pExitCode = exitCode;
    return ok;
}

/*
 * Is the WSL2 utility VM running? Its memory process shows up as "vmmemWSL"
 * ("vmmem" on older Windows). Checking process names never starts the VM,
 * unlike running a command inside WSL would.
 */
static BOOL
winWslVmRunning(void)
{
    HANDLE snap;
    PROCESSENTRY32W pe;
    BOOL found = FALSE;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"vmmemWSL") == 0 ||
                _wcsicmp(pe.szExeFile, L"vmmem") == 0) {
                found = TRUE;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

/*
 * Validate a 36-character GUID string (xxxxxxxx-xxxx-...-xxxxxxxxxxxx).
 * Doubles as the failure detector: WSL1, ancient WSL versions and machines
 * without WSL all fail to produce this, so they fall through to the TCP
 * fallback paths.
 */
static BOOL
winIsGuidStr(const char *s)
{
    int i;

    if (strlen(s) != 36)
        return FALSE;
    for (i = 0; i < 36; i++) {
        char c = s[i];

        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return FALSE;
        }
        else if (!isxdigit((unsigned char)c)) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * Query the WSL2 VM id via the default distro. Only call this when the VM
 * is already running, or wsl.exe would boot it.
 */
static BOOL
winWslQueryVmId(char guid[37])
{
    char raw[256], decoded[256];
    DWORD exitCode = 1;
    size_t i, j, len;

    if (!winWslRunCapture("wsl.exe -- wslinfo --vm-id", raw, sizeof(raw),
                          WIN_VSOCK_CMD_MS, &exitCode))
        return FALSE;
    if (exitCode != 0)
        return FALSE;

    /* wsl.exe output encoding varies: plain ASCII here, UTF-16LE (no BOM)
     * for e.g. --list. Tolerate both. */
    if (raw[0] != '\0' && raw[1] == '\0') {
        /* looks like UTF-16LE */
        for (i = 0, j = 0; raw[i] != '\0' && j < sizeof(decoded) - 1; i += 2)
            decoded[j++] = raw[i];
        decoded[j] = '\0';
    }
    else {
        snprintf(decoded, sizeof(decoded), "%s", raw);
    }

    /* trim trailing whitespace (CR/LF) */
    len = strlen(decoded);
    while (len > 0 && isspace((unsigned char)decoded[len - 1]))
        decoded[--len] = '\0';

    if (!winIsGuidStr(decoded))
        return FALSE;

    memcpy(guid, decoded, 37);
    return TRUE;
}

/*
 * Worker thread: poll the VM id. Never touches the X server, only hands
 * new ids to the main thread via the shared state block.
 */
static void *
winVsockWorkerProc(void *arg)
{
    char guid[37];

    (void)arg;
    for (;;) {
        Sleep(WIN_VSOCK_POLL_MS);

        /* skip without spawning anything while the VM is down, and never
         * boot a stopped VM */
        if (!winWslVmRunning())
            continue;
        if (!winWslQueryVmId(guid))
            continue;

        EnterCriticalSection(&g_vsockLock);
        if (_stricmp(guid, g_vsockBoundGuid) != 0) {
            snprintf(g_vsockPendingGuid, sizeof(g_vsockPendingGuid),
                     "%s", guid);
            g_vsockDirty = TRUE;
        }
        LeaveCriticalSection(&g_vsockLock);
    }
    return NULL;
}

/*
 * Main-thread timer: apply VM id changes posted by the worker.
 */
static CARD32
winVsockTimerProc(OsTimerPtr timer, CARD32 now, void *arg)
{
    char guid[WIN_VSOCK_GUID_LEN];
    BOOL doRebind = FALSE;

    (void)now;
    (void)arg;

    EnterCriticalSection(&g_vsockLock);
    if (g_vsockDirty) {
        snprintf(guid, sizeof(guid), "%s", g_vsockPendingGuid);
        doRebind = TRUE;
    }
    LeaveCriticalSection(&g_vsockLock);

    if (doRebind) {
        char braced[WIN_VSOCK_GUID_LEN];

        snprintf(braced, sizeof(braced), "{%s}", guid);
        _XSERVTransSetHyperVVmId(braced);

        if (HyperVRebindListener() == 0) {
            BOOL had;

            EnterCriticalSection(&g_vsockLock);
            had = (g_vsockBoundGuid[0] != '\0');
            snprintf(g_vsockBoundGuid, sizeof(g_vsockBoundGuid), "%s", guid);
            g_vsockDirty = FALSE;
            LeaveCriticalSection(&g_vsockLock);

            ErrorF("winVsock: %s WSL2 VM %s\n",
                   had ? "vsock listener rebound to" : "vsock listener bound to",
                   guid);
        }
        /* on failure g_vsockDirty stays set: retried on the next tick */
    }

    /* re-arm */
    TimerSet(timer, 0, WIN_VSOCK_TIMER_MS, winVsockTimerProc, NULL);
    return 0;
}

/*
 * Start the watcher (worker thread + main-thread timer). Called once per
 * server generation from InitOutput; only the first generation starts it.
 */
void
winVsockStartWatcher(void)
{
    pthread_t tid;

    if (serverGeneration != 1 || !g_vsockAuto || g_vsockWatcherStarted)
        return;

    if (!g_vsockLockInited) {
        InitializeCriticalSection(&g_vsockLock);
        g_vsockLockInited = TRUE;
    }

    if (pthread_create(&tid, NULL, winVsockWorkerProc, NULL)) {
        ErrorF("winVsock: failed to start watcher thread, "
               "vsock will not follow WSL2 VM restarts\n");
        return;
    }
    pthread_detach(tid);

    TimerSet(NULL, 0, WIN_VSOCK_TIMER_MS, winVsockTimerProc, NULL);
    g_vsockWatcherStarted = TRUE;
}

/*
 * Configure the hyperv transport before the listeners are created.
 * Called from OsVendorPreInit.
 */
void
winVsockPreInit(int argc, char *argv[])
{
    char guid[37], braced[WIN_VSOCK_GUID_LEN];

    if (!g_fVsock) {
        /* default: don't bind an unusable wildcard listener; explicit
         * -vmid or -listen hyperv means the user manages it themselves */
        if (!winArgvHas(argc, argv, "-listen", "hyperv") &&
            !winArgvHas(argc, argv, "-vmid", NULL))
            _XSERVTransNoListen("hyperv");
        return;
    }

    /* -vsock: explicit manual options take precedence */
    if (winArgvHas(argc, argv, "-vmid", NULL)) {
        ErrorF("winVsock: -vmid specified, skipping WSL2 auto-detection\n");
        return;
    }
    if (winArgvHas(argc, argv, "-nolisten", "hyperv"))
        return;

    g_vsockAuto = TRUE;

    if (winWslVmRunning() && winWslQueryVmId(guid)) {
        snprintf(braced, sizeof(braced), "{%s}", guid);
        _XSERVTransSetHyperVVmId(braced);

        if (!g_vsockLockInited) {
            InitializeCriticalSection(&g_vsockLock);
            g_vsockLockInited = TRUE;
        }
        EnterCriticalSection(&g_vsockLock);
        snprintf(g_vsockBoundGuid, sizeof(g_vsockBoundGuid), "%s", guid);
        LeaveCriticalSection(&g_vsockLock);

        ErrorF("winVsock: WSL2 VM %s detected, vsock listener enabled\n",
               guid);
    }
    else {
        _XSERVTransNoListen("hyperv");
        ErrorF("winVsock: no running WSL2 VM detected, "
               "vsock listener disabled for now (TCP unaffected)\n");
    }
}

#else                           /* !HYPERV */

void
winVsockPreInit(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
}

void
winVsockStartWatcher(void)
{
}

#endif                          /* HYPERV */
