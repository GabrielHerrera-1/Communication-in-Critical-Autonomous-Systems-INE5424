#ifndef RT_PRIORITY_H
#define RT_PRIORITY_H

namespace RT_Priority {

bool set_main_thread_priority(const char * role);
bool set_service_thread_priority(const char * role);

} // namespace RT_Priority

#endif
