#include <vndk/window.h>

extern "C" {

AHardwareBuffer* ANativeWindowBuffer_getHardwareBuffer(ANativeWindowBuffer* anwb) {
  return nullptr;
}

void AHardwareBuffer_acquire(AHardwareBuffer* buffer) {
}

void AHardwareBuffer_release(AHardwareBuffer* buffer) {
}

void AHardwareBuffer_describe(const AHardwareBuffer* buffer,
        AHardwareBuffer_Desc* outDesc) {
}

int AHardwareBuffer_allocate(const AHardwareBuffer_Desc* desc,
        AHardwareBuffer** outBuffer) {
   return 0;
}

const native_handle_t* AHardwareBuffer_getNativeHandle(const AHardwareBuffer* buffer) {
   return NULL;
}

}
