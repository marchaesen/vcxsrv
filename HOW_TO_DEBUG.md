# HOW TO DEBUG

### 1. When running the following command:

  `./buildall.sh 1 9 D 2>&1 | tee build_vcxsrv.log`

  The build process will generate .pdb files along with most of the internal
  debug data inside the VcXsrv directory.

### 2. Identify the module you want to debug, and use Visual Studio Community 2022.
→ Debug → Attach to Process, then select the corresponding .exe (depending on
which module of VcXsrv you are working on).
### 3. Load module symbols for accurate debugging.
### 4. Set breakpoints based on the specific tasks or behavior you want to inspect.
