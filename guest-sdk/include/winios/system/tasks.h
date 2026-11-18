#ifndef WINIOS_GUEST_SDK_TASKS_H
#define WINIOS_GUEST_SDK_TASKS_H

#include "winios/winios_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Category C ABI v2.2 Task Management & Application Switching APIs
 */

int winios_task_enumerate(winios_task_entry_t* out_tasks, uint32_t max_count, uint32_t* out_count);
int winios_task_switch(uint32_t target_pid);
int winios_task_terminate(uint32_t target_pid, int graceful);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_TASKS_H
