
#pragma once

#include <map>
#include <list>
#include <functional>
#include <map>
#include <cstdint>

namespace mcucoro
{
    using lambada = std::function<void()>;

    class executor
    {
    public:
        // run for ever
        void run();
        // returns if no more active task.
        void poll();
        void poll_one();

        void post(lambada fn);
        void post_from_isr(lambada fn);

        static executor& system_executor();

        void add_timed_sleeper(uint32_t ms, lambada fn);

    protected:
        void clean_sleepers();

    private:
        std::list<lambada> active_tasks;

        std::multimap<uint32_t, lambada> sleepers;
    };

    // delay ms and execute fn
    static inline void delay_ms(int ms, lambada fn)
    {
        executor::system_executor().add_timed_sleeper(ms, fn);
        // 
    }

    static inline void yield(lambada fn)
    {
        executor::system_executor().post(fn);
    }

    static inline void post_from_isr(lambada fn)
    {
        executor::system_executor().post_from_isr(std::move(fn));
    }

    static inline void post(lambada fn)
    {
        executor::system_executor().post(std::move(fn));
    }

} // namespace mcu
