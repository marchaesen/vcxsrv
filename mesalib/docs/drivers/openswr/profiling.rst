Profiling
=========

OpenSWR contains built-in profiling  which can be enabled
at build time to provide insight into performance tuning.

To enable this, uncomment the following line in ``rasterizer/core/knobs.h`` and rebuild: ::

  //#define KNOB_ENABLE_RDTSC

Running an application will result in a ``rdtsc.txt`` file being
created in current working directory.  This file contains profile
information captured between the ``KNOB_BUCKETS_START_FRAME`` and
``KNOB_BUCKETS_END_FRAME`` (see knobs section).

The resulting file will contain sections for each thread with a
hierarchical breakdown of the time spent in the various operations.
For example: ::

 Thread 0 (API)
  %Tot   %Par  Cycles     CPE        NumEvent   CPE2       NumEvent2  Bucket
   0.00   0.00 28370      2837       10         0          0          APIClearRenderTarget
   0.00  41.23 11698      1169       10         0          0          |-> APIDrawWakeAllThreads
   0.00  18.34 5202       520        10         0          0          |-> APIGetDrawContext
  98.72  98.72 12413773688 29957      414380     0          0          APIDraw
   0.36   0.36 44689364   107        414380     0          0          |-> APIDrawWakeAllThreads
  96.36  97.62 12117951562 9747       1243140    0          0          |-> APIGetDrawContext
   0.00   0.00 19904      995        20         0          0          APIStoreTiles
   0.00   7.88 1568       78         20         0          0          |-> APIDrawWakeAllThreads
   0.00  25.28 5032       251        20         0          0          |-> APIGetDrawContext
   1.28   1.28 161344902  64         2486370    0          0          APIGetDrawContext
   0.00   0.00 50368      2518       20         0          0          APISync
   0.00   2.70 1360       68         20         0          0          |-> APIDrawWakeAllThreads
   0.00  65.27 32876      1643       20         0          0          |-> APIGetDrawContext


 Thread 1 (WORKER)
  %Tot   %Par  Cycles     CPE        NumEvent   CPE2       NumEvent2  Bucket
  83.92  83.92 13198987522 96411      136902     0          0          FEProcessDraw
  24.91  29.69 3918184840 167        23410158   0          0          |-> FEFetchShader
  11.17  13.31 1756972646 75         23410158   0          0          |-> FEVertexShader
   8.89  10.59 1397902996 59         23410161   0          0          |-> FEPAAssemble
  19.06  22.71 2997794710 384        7803387    0          0          |-> FEClipTriangles
  11.67  61.21 1834958176 235        7803387    0          0              |-> FEBinTriangles
   0.00   0.00 0          0          187258     0          0                  |-> FECullZeroAreaAndBackface
   0.00   0.00 0          0          60051033   0          0                  |-> FECullBetweenCenters
   0.11   0.11 17217556   2869592    6          0          0          FEProcessStoreTiles
  15.97  15.97 2511392576 73665      34092      0          0          WorkerWorkOnFifoBE
  14.04  87.95 2208687340 9187       240408     0          0          |-> WorkerFoundWork
   0.06   0.43 9390536    13263      708        0          0              |-> BELoadTiles
   0.00   0.01 293020     182        1609       0          0              |-> BEClear
  12.63  89.94 1986508990 949        2093014    0          0              |-> BERasterizeTriangle
   2.37  18.75 372374596  177        2093014    0          0                  |-> BETriangleSetup
   0.42   3.35 66539016   31         2093014    0          0                  |-> BEStepSetup
   0.00   0.00 0          0          21766      0          0                  |-> BETrivialReject
   1.05   8.33 165410662  79         2071248    0          0                  |-> BERasterizePartial
   6.06  48.02 953847796  1260       756783     0          0                  |-> BEPixelBackend
   0.20   3.30 31521202   41         756783     0          0                      |-> BESetup
   0.16   2.69 25624304   33         756783     0          0                      |-> BEBarycentric
   0.18   2.92 27884986   36         756783     0          0                      |-> BEEarlyDepthTest
   0.19   3.20 30564174   41         744058     0          0                      |-> BEPixelShader
   0.26   4.30 41058646   55         744058     0          0                      |-> BEOutputMerger
   1.27  20.94 199750822  32         6054264    0          0                      |-> BEEndTile
   0.33   2.34 51758160   23687      2185       0          0              |-> BEStoreTiles
   0.20  60.22 31169500   28807      1082       0          0                  |-> B8G8R8A8_UNORM
   0.00   0.00 302752     302752     1          0          0          WorkerWaitForThreadEvent

