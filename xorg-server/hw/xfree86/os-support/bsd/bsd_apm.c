#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "os.h"
#include "xf86.h"
#include "xf86Priv.h"
#define XF86_OS_PRIVS
#include "xf86_OSproc.h"
#include "xf86_OSlib.h"

#include <machine/apmvar.h>

#define APM_DEVICE "/dev/apm"

static void *APMihPtr = NULL;
static void bsdCloseAPM(void);

static struct {
    u_int apmBsd;
    pmEvent xf86;
} bsdToXF86Array[] = {
    {APM_STANDBY_REQ, XF86_APM_SYS_STANDBY},
    {APM_SUSPEND_REQ, XF86_APM_SYS_SUSPEND},
    {APM_NORMAL_RESUME, XF86_APM_NORMAL_RESUME},
    {APM_CRIT_RESUME, XF86_APM_CRITICAL_RESUME},
    {APM_BATTERY_LOW, XF86_APM_LOW_BATTERY},
    {APM_POWER_CHANGE, XF86_APM_POWER_STATUS_CHANGE},
    {APM_UPDATE_TIME, XF86_APM_UPDATE_TIME},
    {APM_CRIT_SUSPEND_REQ, XF86_APM_CRITICAL_SUSPEND},
    {APM_USER_STANDBY_REQ, XF86_APM_USER_STANDBY},
    {APM_USER_SUSPEND_REQ, XF86_APM_USER_SUSPEND},
    {APM_SYS_STANDBY_RESUME, XF86_APM_STANDBY_RESUME},
#ifdef APM_CAPABILITY_CHANGE
    {APM_CAPABILITY_CHANGE, XF86_APM_CAPABILITY_CHANGED},
#endif
};

#define numApmEvents (sizeof(bsdToXF86Array) / sizeof(bsdToXF86Array[0]))

static pmEvent
bsdToXF86(int type)
{
    int i;

    for (i = 0; i < numApmEvents; i++) {
        if (type == bsdToXF86Array[i].apmBsd) {
            return bsdToXF86Array[i].xf86;
        }
    }
    return XF86_APM_UNKNOWN;
}

/*
 * APM events can be requested direclty from /dev/apm
 */
static int
bsdPMGetEventFromOS(int fd, pmEvent * events, int num)
{
    struct apm_event_info bsdEvent;
    int i;

    for (i = 0; i < num; i++) {

        if (ioctl(fd, APM_IOC_NEXTEVENT, &bsdEvent) < 0) {
            if (errno != EAGAIN) {
                xf86Msg(X_WARNING, "bsdPMGetEventFromOS: APM_IOC_NEXTEVENT"
                        " %s\n", strerror(errno));
            }
            break;
        }
        events[i] = bsdToXF86(bsdEvent.type);
    }
    return i;
}

/*
 * XXX This won't work on /dev/apm !
 *     We should either use /dev/apm_ctl (and kill apmd(8))
 *     or talk to apmd (but its protocol is not publically available)...
 */
static pmWait
bsdPMConfirmEventToOs(int fd, pmEvent event)
{
    switch (event) {
    case XF86_APM_SYS_STANDBY:
    case XF86_APM_USER_STANDBY:
        if (ioctl(fd, APM_IOC_STANDBY, NULL) == 0)
            return PM_WAIT;     /* should we stop the Xserver in standby, too? */
        else
            return PM_NONE;
    case XF86_APM_SYS_SUSPEND:
    case XF86_APM_CRITICAL_SUSPEND:
    case XF86_APM_USER_SUSPEND:
        if (ioctl(fd, APM_IOC_SUSPEND, NULL) == 0)
            return PM_WAIT;
        else
            return PM_NONE;
    case XF86_APM_STANDBY_RESUME:
    case XF86_APM_NORMAL_RESUME:
    case XF86_APM_CRITICAL_RESUME:
    case XF86_APM_STANDBY_FAILED:
    case XF86_APM_SUSPEND_FAILED:
        return PM_CONTINUE;
    default:
        return PM_NONE;
    }
}

PMClose
xf86OSPMOpen(void)
{
    int fd;

    if (APMihPtr || !xf86Info.pmFlag) {
        return NULL;
    }

    if ((fd = open(APM_DEVICE, O_RDWR)) == -1) {
        return NULL;
    }
    xf86PMGetEventFromOs = bsdPMGetEventFromOS;
    xf86PMConfirmEventToOs = bsdPMConfirmEventToOs;
    APMihPtr = xf86AddGeneralHandler(fd, xf86HandlePMEvents, NULL);
    return bsdCloseAPM;
}

static void
bsdCloseAPM(void)
{
    int fd;

    if (APMihPtr) {
        fd = xf86RemoveGeneralHandler(APMihPtr);
        close(fd);
        APMihPtr = NULL;
    }
}
