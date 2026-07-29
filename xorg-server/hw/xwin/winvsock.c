/*
 * winvsock.c - Hyper-V vsock (AF_HYPERV) auto-configuration for WSL2
 *
 * The xtrans "hyperv" transport binds HV_GUID_WILDCARD by default, which
 * WSL2's utility VM never matches (microsoft/WSL issue #5751): the listener
 * must be bound to the WSL2 VM's exact id. That id changes at every WSL VM
 * boot, so this file
 *
 *  - winVsockPreInit: called from OsVendorPreInit (before the listeners are
 *    created). With -wslvsock, detects the WSL2 VM id unprivileged via
 *    'wsl.exe -- wslinfo --vm-id' and binds it. Without -wslvsock (and
 *    without an explicit -vmid/-listen hyperv), disables the hyperv listener
 *    so it is not bound to an unusable wildcard address by default.
 *
 *  - winVsockStartWatcher: called from InitOutput (serverGeneration 1).
 *    Starts a worker thread that watches the WSL2 VM instance (every
 *    0.5 s, cheaply, via the vmmemWSL process id + creation time, without
 *    ever starting a stopped VM) and only queries the VM id when the
 *    instance changed. Every wslinfo query goes through a distro that is
 *    already Running (via 'wsl.exe -l -v' first), so neither startup nor
 *    polling ever boots a stopped distro. A main-thread OsTimer rebinds
 *    the listener when the id changes (WSL restart), or creates it late
 *    when WSL2 only starts after the server.
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

#define WIN_VSOCK_POLL_MS   500     /* worker poll interval (vmmemWSL probe) */
#define WIN_VSOCK_STATE_MS  2000    /* worker interval for 'wsl -l -v' checks */
#define WIN_VSOCK_TIMER_MS  500     /* main-thread timer interval */
#define WIN_VSOCK_CMD_MS    5000    /* wsl.exe spawn timeout */
#define WIN_VSOCK_GUID_LEN  40      /* 36 chars + braces + NUL, rounded up */
#define WIN_VSOCK_DISTRO_LEN 64     /* max distro name length we handle */

/* shared state, protected by g_vsockLock */
static CRITICAL_SECTION g_vsockLock;
static BOOL g_vsockLockInited = FALSE;
static char g_vsockBoundGuid[WIN_VSOCK_GUID_LEN];   /* currently bound, "" if none */
static char g_vsockPendingGuid[WIN_VSOCK_GUID_LEN]; /* latest id seen by worker */
static BOOL g_vsockDirty = FALSE;                   /* pending needs binding */

static BOOL g_vsockAuto = FALSE;          /* -wslvsock auto-detection active */
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
 * Identifies the currently running WSL2 utility VM instance. The VM id
 * (GUID) changes whenever the VM restarts, but querying it requires
 * spawning wsl.exe; the instance below is free to obtain and is enough to
 * tell "same VM as last poll" from "VM (re)started".
 */
typedef struct {
    DWORD pid;
    ULONGLONG creationTime;     /* 0 when unavailable */
} winWslVmInfo;

/*
 * Is the WSL2 utility VM running? Its memory process shows up as "vmmemWSL"
 * ("vmmem" on older Windows). Checking process names never starts the VM,
 * unlike running a command inside WSL would. On success also returns the
 * process id and creation time of that process: a VM restart replaces it,
 * so callers can detect restarts without spawning anything.
 */
static BOOL
winWslVmInstance(winWslVmInfo *info)
{
    HANDLE snap, hProc;
    PROCESSENTRY32W pe;
    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    ULARGE_INTEGER u;
    BOOL found = FALSE;

    info->pid = 0;
    info->creationTime = 0;

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
    if (!found)
        return FALSE;

    info->pid = pe.th32ProcessID;

    /* Best effort: the creation time distinguishes a restarted VM even if
     * the pid got recycled. Unavailable -> stays 0 -> caller has to query
     * the VM id on every poll, as before. */
    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info->pid);
    if (hProc) {
        if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
            u.LowPart = ftCreate.dwLowDateTime;
            u.HighPart = ftCreate.dwHighDateTime;
            info->creationTime = u.QuadPart;
        }
        CloseHandle(hProc);
    }
    return TRUE;
}

/*
 * Decode captured wsl.exe output: tolerates plain ASCII and UTF-16LE
 * (no BOM), trims trailing whitespace (CR/LF).
 */
static void
winWslDecodeOutput(const char *raw, char *out, size_t outLen)
{
    size_t i, j, len;

    /* wsl.exe output encoding varies: plain ASCII for wslinfo, UTF-16LE
     * (no BOM) for e.g. --list. */
    if (raw[0] != '\0' && raw[1] == '\0') {
        for (i = 0, j = 0; raw[i] != '\0' && j < outLen - 1; i += 2)
            out[j++] = raw[i];
        out[j] = '\0';
    }
    else {
        snprintf(out, outLen, "%s", raw);
    }

    len = strlen(out);
    while (len > 0 && isspace((unsigned char)out[len - 1]))
        out[--len] = '\0';
}

/*
 * Name of the first WSL2 distro in the "Running" state, or FALSE if none.
 * 'wsl.exe -l -v' is a pure service-side query: it creates no process
 * inside any distro, so it neither disturbs a running instance nor boots
 * a stopped one. WSL1 distros (VERSION=1) are skipped: they have no VM
 * and cannot serve the vsock query.
 */
static BOOL
winWslFirstRunningDistro(char *name, size_t nameLen)
{
    char raw[2048], decoded[2048];
    DWORD exitCode = 1;
    char *p;

    if (!winWslRunCapture("wsl.exe -l -v", raw, sizeof(raw),
                          WIN_VSOCK_CMD_MS, &exitCode))
        return FALSE;
    if (exitCode != 0)
        return FALSE;

    winWslDecodeOutput(raw, decoded, sizeof(decoded));

    /* Table format: a header line ("NAME STATE VERSION"), then one line
     * per distro ("[*] <name> <STATE> <version>"). Distro names may
     * contain spaces, so parse from the end of each line: the last token
     * is the version number, the one before it the state word. The header
     * is excluded naturally ("VERSION" is not a number). */
    p = decoded;
    while (*p) {
        char *s, *eol, *end, *ver, *state;

        eol = strchr(p, '\n');
        if (eol)
            *eol = '\0';

        s = p;
        while (*s == ' ' || *s == '*')
            s++;

        /* trim trailing whitespace of the line (stray CR etc.) */
        end = s + strlen(s);
        while (end > s && isspace((unsigned char)end[-1]))
            *--end = '\0';

        ver = strrchr(s, ' ');
        if (ver && ver[1] != '\0') {
            BOOL allDigits = TRUE;
            char *v;
            int wslVer;

            for (v = ver + 1; *v; v++)
                if (!isdigit((unsigned char)*v))
                    allDigits = FALSE;
            if (allDigits) {
                wslVer = atoi(ver + 1);
                *ver = '\0';
                /* the cut leaves the column padding behind "STATE" as
                 * trailing spaces; trim it before looking for the state
                 * word, or strrchr finds padding instead of "Running" */
                end = s + strlen(s);
                while (end > s && isspace((unsigned char)end[-1]))
                    *--end = '\0';
                state = strrchr(s, ' ');
                /* only WSL2 distros can serve the vsock query; a WSL1
                 * distro (VERSION=1) has no VM id and querying it would
                 * spawn wslinfo into it for nothing */
                if (wslVer == 2 && state &&
                    _stricmp(state + 1, "Running") == 0) {
                    *state = '\0';
                    end = s + strlen(s);
                    while (end > s && isspace((unsigned char)end[-1]))
                        *--end = '\0';
                    if (*s) {
                        snprintf(name, nameLen, "%s", s);
                        return TRUE;
                    }
                }
            }
        }

        if (!eol)
            break;
        p = eol + 1;
    }
    return FALSE;
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
/*
 * Query the WSL2 VM id by running wslinfo inside an already-running
 * distro, so the query itself never boots a distro. Callers must pass a
 * distro that is currently Running (see winWslFirstRunningDistro).
 */
static BOOL
winWslQueryVmId(char guid[37], const char *distro)
{
    char raw[256], decoded[256], cmd[320];
    DWORD exitCode = 1;

    /* NOTE: no quotes around the distro name: wsl.exe's own argument
     * parser takes the -d value literally, so -d "name" fails with
     * WSL_E_DISTRO_NOT_FOUND. Distro names with spaces are not handled
     * (query fails conservatively). */
    snprintf(cmd, sizeof(cmd), "wsl.exe -d %s -- wslinfo --vm-id", distro);

    if (!winWslRunCapture(cmd, raw, sizeof(raw), WIN_VSOCK_CMD_MS,
                          &exitCode))
        return FALSE;
    if (exitCode != 0)
        return FALSE;

    winWslDecodeOutput(raw, decoded, sizeof(decoded));

    if (!winIsGuidStr(decoded))
        return FALSE;

    memcpy(guid, decoded, 37);
    return TRUE;
}

/*
 * Worker thread: watch the VM instance, and only when it changed query the
 * VM id (spawning wsl.exe). Never touches the X server, only hands new ids
 * to the main thread via the shared state block.
 */
static void *
winVsockWorkerProc(void *arg)
{
    winWslVmInfo lastVm = { 0, 0 }, curVm;
    char guid[37], distro[WIN_VSOCK_DISTRO_LEN];
    int stateTick = 0;

    (void)arg;
    for (;;) {
        Sleep(WIN_VSOCK_POLL_MS);

        if (!winWslVmInstance(&curVm)) {
            /* VM is down: the next appearance counts as a new instance */
            lastVm.pid = 0;
            lastVm.creationTime = 0;
            continue;
        }

        if (!(curVm.creationTime != 0 &&
              curVm.pid == lastVm.pid &&
              curVm.creationTime == lastVm.creationTime)) {
            /* New VM instance (or first poll, or instance data
             * unavailable): fetch the VM id, but only through a distro
             * that is already Running - querying into a stopped distro
             * would boot it. If none is running (VM still booting),
             * retry next poll without recording the instance. */
            if (!winWslFirstRunningDistro(distro, sizeof(distro)) ||
                !winWslQueryVmId(guid, distro))
                continue;
            lastVm = curVm;

            EnterCriticalSection(&g_vsockLock);
            if (_stricmp(guid, g_vsockBoundGuid) != 0) {
                snprintf(g_vsockPendingGuid, sizeof(g_vsockPendingGuid),
                         "%s", guid);
                g_vsockDirty = TRUE;
            }
            LeaveCriticalSection(&g_vsockLock);
        }
        else {
            /* Same VM instance: while unbound (e.g. no distro was running
             * at startup), check every WIN_VSOCK_STATE_MS whether a
             * distro has come up, then bind late. */
            BOOL unbound;

            stateTick += WIN_VSOCK_POLL_MS;
            if (stateTick < WIN_VSOCK_STATE_MS)
                continue;
            stateTick = 0;

            EnterCriticalSection(&g_vsockLock);
            unbound = (g_vsockBoundGuid[0] == '\0');
            LeaveCriticalSection(&g_vsockLock);

            if (unbound &&
                winWslFirstRunningDistro(distro, sizeof(distro)) &&
                winWslQueryVmId(guid, distro)) {
                EnterCriticalSection(&g_vsockLock);
                if (_stricmp(guid, g_vsockBoundGuid) != 0) {
                    snprintf(g_vsockPendingGuid, sizeof(g_vsockPendingGuid),
                             "%s", guid);
                    g_vsockDirty = TRUE;
                }
                LeaveCriticalSection(&g_vsockLock);
            }
        }
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
    winWslVmInfo vmInfo;
    char guid[37], braced[WIN_VSOCK_GUID_LEN], distro[WIN_VSOCK_DISTRO_LEN];

    if (!g_fWslVsock) {
        /* default: don't bind an unusable wildcard listener; explicit
         * -vmid or -listen hyperv means the user manages it themselves */
        if (!winArgvHas(argc, argv, "-listen", "hyperv") &&
            !winArgvHas(argc, argv, "-vmid", NULL))
            _XSERVTransNoListen("hyperv");
        return;
    }

    /* -wslvsock: explicit manual options take precedence */
    if (winArgvHas(argc, argv, "-vmid", NULL)) {
        ErrorF("winVsock: -vmid specified, skipping WSL2 auto-detection\n");
        return;
    }
    if (winArgvHas(argc, argv, "-nolisten", "hyperv"))
        return;

    g_vsockAuto = TRUE;

    /* Bind only if the VM is up AND a distro is already Running: querying
     * wslinfo in a stopped distro would boot it. If none is running, stay
     * unbound - the watcher binds late as soon as one comes up. */
    {
        BOOL vmUp = winWslVmInstance(&vmInfo);
        BOOL haveDistro = vmUp ? winWslFirstRunningDistro(distro, sizeof(distro)) : FALSE;
        BOOL haveGuid = haveDistro ? winWslQueryVmId(guid, distro) : FALSE;

        if (vmUp && haveDistro && haveGuid) {
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
        ErrorF("winVsock: no running WSL2 distro detected, "
               "vsock listener disabled for now (TCP unaffected)\n");
    }
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
