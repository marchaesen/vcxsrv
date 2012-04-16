
/* data returned for monitor information */
struct GetMonitorInfoData {
    int requestedMonitor;
    int monitorNum;
    Bool bUserSpecifiedMonitor;
    Bool bMonitorSpecifiedExists;
    int monitorOffsetX;
    int monitorOffsetY;
    int monitorHeight;
    int monitorWidth;
};

Bool QueryMonitor(int index, struct GetMonitorInfoData *data);
