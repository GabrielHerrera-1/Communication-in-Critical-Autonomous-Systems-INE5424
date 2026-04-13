#ifndef RT_PRIORITY_H
#define RT_PRIORITY_H

namespace RT_Priority {

static const int MAIN_THREAD_PRIORITY = 98;
static const int SERVICE_THREAD_PRIORITY = 99;

bool set_current_thread_fifo(int priority, const char * role);
bool set_main_thread_priority(const char * role);
bool set_service_thread_priority(const char * role);

} // namespace RT_Priority

#endif
