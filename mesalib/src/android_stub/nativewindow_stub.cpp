#include <vndk/window.h>

extern "C" {

AHardwareBuffer* ANativeWindowBuffer_getHardwareBuffer(ANativeWindowBuffer* anwb) {
  return nullptr;
}

void AHardwareBuffer_acquire(AHardwareBuffer* buffer) {
}

void AHardwareBuffer_release(AHardwareBuffer* buffer) {
}

}
