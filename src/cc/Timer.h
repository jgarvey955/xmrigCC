/* XMRigCC
 * Copyright 2017-     BenDr0id    <https://github.com/BenDr0id>, <ben@graef.in>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TIMER_H__
#define __TIMER_H__

#include <iostream>
#include <chrono>
#include <functional>
#include <thread>
#include <condition_variable>
#include <mutex>

class Timer
{
public:
  Timer() {}

  Timer(std::function<void(void)> func, uint64_t interval)
  {
    m_func = func;
    m_interval = interval;
  }

  ~Timer()
  {
    stop();
  }

public:

  void start()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) { return; }
    m_running = true;
    m_thread = std::thread([&]()
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      while (!m_condition.wait_for(lock, std::chrono::milliseconds(m_interval), [&]() { return !m_running; }))
      {
        const auto callback = m_func;
        lock.unlock();
        callback();
        lock.lock();
      }
    });
  }

  void stop()
  {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_running = false;
    }
    m_condition.notify_all();

    if (m_thread.joinable())
    {
      m_thread.join();
    }
  }

  void setFunction(std::function<void(void)> func)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_func = func;
  }

  void setInterval(uint64_t interval)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_interval = interval;
  }

  bool isRunning()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running;
  }

  uint64_t getInterval()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_interval;
  }

private:
  std::function<void(void)> m_func;
  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_condition;

  uint64_t m_interval = 0;
  bool m_running = false;
};

#endif //__TIMER_H__
