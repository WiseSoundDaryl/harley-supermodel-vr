/**
 ** Supermodel
 ** A Sega Model Series Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * Thread.cpp
 *
 * Standard library implementation of threading primitives.
 *
 * HarleyVR isolated backend preview, based on upstream bb6e18eb061a0b153e230d3babd22084f9ea59c8.
 * Retains the existing CThread ABI and HarleyVR timed-wait tracing extension.
 */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// These would rename the CThread methods of the same names
#undef CreateMutex
#undef CreateSemaphore
#else
#include <pthread.h>
#include <signal.h>
#endif

#include "Thread.h"

#include "Supermodel.h"
#include "LiveFrameTrace.h"

// LiveFrameTrace includes windows.h too; keep the existing factory names.
#ifdef _WIN32
#undef CreateSemaphore
#undef CreateMutex
#endif

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <system_error>

namespace
{
  const std::chrono::steady_clock::time_point s_startTime = std::chrono::steady_clock::now();

  thread_local std::string s_lastError;

#if defined(_WIN32)
  // Keep the public CMutex/CCondVar void* ABI, including recursive locking.
  // The pinned winpthreads timed-wait route validates short timeouts against
  // coarse GetTickCount64 ticks. Native Windows condition variables avoid
  // that library retry loop without changing timer resolution or polling.
  struct MutexImpl
  {
    CRITICAL_SECTION native;
    MutexImpl()
    {
      if (!InitializeCriticalSectionEx(&native, 0, 0))
        throw std::system_error(int(::GetLastError()), std::system_category(),
          "InitializeCriticalSectionEx");
    }
    ~MutexImpl() { DeleteCriticalSection(&native); }
    MutexImpl(const MutexImpl&) = delete;
    MutexImpl& operator=(const MutexImpl&) = delete;
    void lock() { EnterCriticalSection(&native); }
    void unlock() { LeaveCriticalSection(&native); }
  };

  // UINT32_MAX is a finite duration in the existing API, but Windows reserves
  // that DWORD value for INFINITE. Split only that extreme timeout, preserving
  // notification wakeups and reacquisition at every wait boundary.
  constexpr DWORD FiniteCondWaitChunk(uint64_t milliseconds)
  {
    return milliseconds >= uint64_t(INFINITE) ? INFINITE - 1 : DWORD(milliseconds);
  }

  struct CondVarImpl
  {
    CONDITION_VARIABLE native = CONDITION_VARIABLE_INIT;
    void wait(MutexImpl& mutex)
    {
      if (!SleepConditionVariableCS(&native, &mutex.native, INFINITE))
        throw std::system_error(int(::GetLastError()), std::system_category(),
          "SleepConditionVariableCS");
    }
    std::cv_status wait_for(MutexImpl& mutex, std::chrono::milliseconds duration)
    {
      uint64_t remaining = duration.count() > 0 ? uint64_t(duration.count()) : 0;
      const auto deadline = remaining >= uint64_t(INFINITE)
        ? std::chrono::steady_clock::now() + duration
        : std::chrono::steady_clock::time_point{};
      for (;;)
      {
        const DWORD chunk = FiniteCondWaitChunk(remaining);
        if (SleepConditionVariableCS(&native, &mutex.native, chunk))
          return std::cv_status::no_timeout;
        const DWORD error = ::GetLastError();
        if (error != ERROR_TIMEOUT)
          throw std::system_error(int(error), std::system_category(),
            "SleepConditionVariableCS");
        if (remaining < uint64_t(INFINITE)) return std::cv_status::timeout;
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return std::cv_status::timeout;
        remaining = uint64_t(std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count());
      }
    }
    void notify_one() { WakeConditionVariable(&native); }
    void notify_all() { WakeAllConditionVariable(&native); }
  };
#else
  // A mutex that the owner may lock again, as code using CMutex may expect
  using MutexImpl = std::recursive_mutex;

  // The condition variable that works with such a mutex
  using CondVarImpl = std::condition_variable_any;
#endif

  // C++17 has no semaphore
  struct SemaphoreImpl
  {
    std::mutex mutex;
    std::condition_variable available;
    UINT32 value;

    explicit SemaphoreImpl(UINT32 initVal) : value(initVal) {}
  };

  struct ThreadImpl
  {
    std::thread thread;
    std::shared_ptr<int> status;  // shared with the thread, which may outlive this if not waited on
  };

  void BlockAsynchronousSignals()
  {
    // Emulation threads must not be interrupted to handle these: they are left
    // to the main thread
#if !defined(_WIN32)
    static const int signals[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGALRM, SIGTERM, SIGCHLD, SIGWINCH, SIGVTALRM, SIGPROF };
    sigset_t mask;
    sigemptyset(&mask);
    for (int sig : signals)
      sigaddset(&mask, sig);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);
#endif
  }

  // Runs an operation on a primitive and returns whether it succeeded,
  // recording the error if it did not
  template <typename Operation>
  bool Try(Operation operation)
  {
    try
    {
      operation();
      return true;
    }
    catch (const std::exception &e)
    {
      s_lastError = e.what();
      return false;
    }
  }
}

void CThread::Sleep(UINT32 ms)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

UINT32 CThread::GetTicks()
{
  return UINT32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_startTime).count());
}

static void SetCurrentThreadName(const std::string &name)
{
#if defined(_WIN32)
  // Not available before Windows 10
  using SetThreadDescriptionFunc = HRESULT (WINAPI *)(HANDLE, PCWSTR);
  static const SetThreadDescriptionFunc setThreadDescription = reinterpret_cast<SetThreadDescriptionFunc>(
    reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
  if (setThreadDescription != nullptr)
  {
    std::wstring wideName(name.begin(), name.end());  // names are ASCII
    setThreadDescription(GetCurrentThread(), wideName.c_str());
  }
#elif defined(__APPLE__)
  pthread_setname_np(name.c_str());
#elif defined(__linux__)
  // Limited to 16 bytes, including the terminator
  pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
#else
  (void) name;
#endif
}

CThread *CThread::CreateThread(const std::string &name, ThreadStart start, void *startParam)
{
  std::unique_ptr<ThreadImpl> impl(new ThreadImpl());
  impl->status = std::make_shared<int>(0);
  std::shared_ptr<int> status = impl->status;
  bool created = Try([&]
  {
    impl->thread = std::thread([=]
    {
      BlockAsynchronousSignals();
      SetCurrentThreadName(name);
      *status = start(startParam);
    });
  });
  if (!created)
    return NULL;
  return new CThread(name, impl.release());
}

CSemaphore *CThread::CreateSemaphore(UINT32 initVal)
{
  SemaphoreImpl *impl = NULL;
  if (!Try([&] { impl = new SemaphoreImpl(initVal); }))
    return NULL;
  return new CSemaphore(impl);
}

CCondVar *CThread::CreateCondVar()
{
  CondVarImpl *impl = NULL;
  if (!Try([&] { impl = new CondVarImpl(); }))
    return NULL;
  return new CCondVar(impl);
}

CMutex *CThread::CreateMutex()
{
  MutexImpl *impl = NULL;
  if (!Try([&] { impl = new MutexImpl(); }))
    return NULL;
  return new CMutex(impl);
}

const char *CThread::GetLastError()
{
  return s_lastError.c_str();
}

CThread::CThread(const std::string &name, void *impl)
  : m_name(name),
    m_impl(impl)
{
  //
}

CThread::~CThread()
{
  // User should have called Wait() before thread object is destroyed
  if (nullptr != m_impl)
  {
    ErrorLog("Runaway thread error. A thread was not properly halted: %s\n", GetName().c_str());
    ThreadImpl *impl = (ThreadImpl *) m_impl;
    impl->thread.detach();  // destroying a running std::thread would terminate the program
    delete impl;
  }
}

const std::string &CThread::GetName() const
{
  return m_name;
}

UINT32 CThread::GetId()
{
  std::thread::id id = m_impl != NULL ? ((ThreadImpl *) m_impl)->thread.get_id() : std::this_thread::get_id();
  return UINT32(std::hash<std::thread::id>()(id));
}

int CThread::Wait()
{
  if (m_impl == NULL)
    return -1;
  ThreadImpl *impl = (ThreadImpl *) m_impl;
  if (!Try([&] { impl->thread.join(); }))
    return -1;
  int status = *impl->status;
  delete impl;
  m_impl = NULL;
  return status;
}

CSemaphore::CSemaphore(void *impl) : m_impl(impl)
{
  //
}

CSemaphore::~CSemaphore()
{
  delete (SemaphoreImpl *) m_impl;
}

UINT32 CSemaphore::GetValue()
{
  SemaphoreImpl *impl = (SemaphoreImpl *) m_impl;
  std::lock_guard<std::mutex> lock(impl->mutex);
  return impl->value;
}

bool CSemaphore::Wait()
{
  SemaphoreImpl *impl = (SemaphoreImpl *) m_impl;
  return Try([&]
  {
    std::unique_lock<std::mutex> lock(impl->mutex);
    impl->available.wait(lock, [&] { return impl->value > 0; });
    impl->value--;
  });
}

bool CSemaphore::Post()
{
  SemaphoreImpl *impl = (SemaphoreImpl *) m_impl;
  return Try([&]
  {
    // Notifying with the lock held means a waiter can't see the new value,
    // return and have the semaphore destroyed while this is still using it
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->value++;
    impl->available.notify_one();
  });
}

CCondVar::CCondVar(void *impl) : m_impl(impl)
{
  //
}

CCondVar::~CCondVar()
{
  delete (CondVarImpl *) m_impl;
}

bool CCondVar::Wait(CMutex *mutex)
{
  return Try([&] { ((CondVarImpl *) m_impl)->wait(*(MutexImpl *) mutex->m_impl); });
}

bool CCondVar::WaitTimeout(CMutex *mutex, UINT32 ms)
{
  const uint64_t traceBegin = LiveFrameTrace::OwnerNow();
  // Preserve the existing trace result convention: 0 notification/spurious wake,
  // 1 timeout, -1 exception. Both ordinary outcomes reacquire the caller's lock.
  int result = -1;
  const bool success = Try([&]
  {
    const auto status = ((CondVarImpl *) m_impl)->wait_for(
      *(MutexImpl *) mutex->m_impl, std::chrono::milliseconds(ms));
    result = status == std::cv_status::timeout ? 1 : 0;
  });
  LiveFrameTrace::SlowOwnerEvent("owner.condition_wait", traceBegin,
    int64_t(reinterpret_cast<uintptr_t>(m_impl)), ms, result,
    int64_t(reinterpret_cast<uintptr_t>(mutex->m_impl)));
  return success;
}

bool CCondVar::Signal()
{
  ((CondVarImpl *) m_impl)->notify_one();
  return true;
}

bool CCondVar::SignalAll()
{
  ((CondVarImpl *) m_impl)->notify_all();
  return true;
}

CMutex::CMutex(void *impl) : m_impl(impl)
{
  //
}

CMutex::~CMutex()
{
  delete (MutexImpl *) m_impl;
}

bool CMutex::Lock()
{
  const uint64_t traceBegin = LiveFrameTrace::OwnerNow();
  const bool success = Try([&] { ((MutexImpl *) m_impl)->lock(); });
  LiveFrameTrace::SlowOwnerEvent("owner.mutex_lock", traceBegin,
    int64_t(reinterpret_cast<uintptr_t>(m_impl)), success ? 0 : -1);
  return success;
}

bool CMutex::Unlock()
{
  ((MutexImpl *) m_impl)->unlock();
  return true;
}

