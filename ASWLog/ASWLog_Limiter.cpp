/* **************************************************************************
ASWLog_Limiter.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "ASWLog_Limiter.h"
//---------------------------------------------------------------------------

namespace ASWLog
{

/////////////////////////////////////////////////////////////////////////////
// TASWLogLimiter
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
bool TASWLogLimiter::Allow() noexcept
{
    std::uint64_t refusedCount = 0;

    return Allow(refusedCount);
}

//---------------------------------------------------------------------------
bool TASWLogLimiter::Allow(std::uint64_t& refusedCount) noexcept
{
    // Only EveryInterval() needs the time
    return Allow(refusedCount, m_Kind == Kind::EveryInterval ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point());
}

//---------------------------------------------------------------------------
bool TASWLogLimiter::Allow(std::uint64_t& refusedCount, std::chrono::steady_clock::time_point now) noexcept
{
    refusedCount = 0;

    switch (m_Kind)
    {
        case Kind::Once:
            // Without a write once it has allowed its call, so the calls after it don't contend for the cache line
            return m_CallCount.load(std::memory_order_relaxed) == 0 && m_CallCount.exchange(1, std::memory_order_relaxed) == 0;

        case Kind::EveryN:
        {
            const std::uint64_t call = m_CallCount.fetch_add(1, std::memory_order_relaxed);

            if (call % m_N != 0)
                return false;

            refusedCount = call > 0 ? m_N - 1 : 0;

            return true;
        }

        case Kind::EveryInterval:
        {
            if (m_IntervalTicks <= 0)
                return true;

            const auto nowTicks = now.time_since_epoch().count();
            auto nextAllowed = m_NextAllowedTicks.load(std::memory_order_relaxed);
            constexpr auto MaxTicks = std::numeric_limits<std::chrono::steady_clock::rep>::max();
            // The time from which the call after this one is allowed, at most the clock's end
            const auto newNextAllowed = nowTicks > 0 && m_IntervalTicks > MaxTicks - nowTicks ? MaxTicks : nowTicks + m_IntervalTicks;

            // Of the calls that see the interval over, only the one that moves the next allowed time on is allowed
            if (nowTicks < nextAllowed || !m_NextAllowedTicks.compare_exchange_strong(nextAllowed, newNextAllowed, std::memory_order_relaxed))
            {
                m_RefusedCount.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            refusedCount = m_RefusedCount.exchange(0, std::memory_order_relaxed);

            return true;
        }
    }

    return false;
}

//---------------------------------------------------------------------------

} // namespace ASWLog
