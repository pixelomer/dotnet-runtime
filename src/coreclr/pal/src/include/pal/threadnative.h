#pragma once
#include <pthread.h>
bool NativeGetCurrentStackBounds(void** low, void** high);
bool NativeSetThreadPriority(pthread_t thread, int relativePriority);
