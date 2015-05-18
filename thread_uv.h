/*****************************************
* @file     thread_uv.h
* @brief    对libuv下的线程与锁进行封装
* @details
* @author   phata,wqvbjhc@gmail.com
* @date     2014-10-27
* @mod      2015-03-24  phata  uv_err_name,uv_strerror返回NULL时直接转string会崩溃，先判断
******************************************/
#ifndef COMMON_THREAD_UV_H
#define COMMON_THREAD_UV_H
#include <string>
#include "uv.h"

//对Android平台，也认为是linux
#ifdef ANDROID
#define __linux__ ANDROID
#endif
//包含头文件
#if defined (WIN32) || defined(_WIN32)
#include <windows.h>
#endif
#ifdef __linux__
#include <pthread.h>
#include <unistd.h>
#endif

//函数
#if defined (WIN32) || defined(_WIN32)
#define uv_thread_close(t) (CloseHandle(t)!=FALSE)
#define uv_thread_sleep(ms) Sleep(ms);//睡眠ms毫秒
#define uv_thread_id GetCurrentThreadId//得到当前线程句柄

#elif defined(__linux__)
#define uv_thread_close(t) ()
#define uv_thread_sleep(ms) usleep((ms) * 1000)
#define uv_thread_id pthread_self//得到当前线程句柄

#else
#error "no supported os"
#endif

/*****************************
* @brief   获取libuv错误码对应的错误信息
* @param   errcode     --libuv函数错误码(不等于0的返回值)
* @return  std::string --返回的详细错误说明
******************************/
inline std::string GetUVError(int errcode)
{
    if (0 == errcode) {
        return "";
    }
    std::string err;
    auto tmpChar = uv_err_name(errcode);
    if (tmpChar) {
        err = tmpChar;
        err += ":";
    }else{
		err = "unknown system errcode "+std::to_string((long long)errcode);
		err += ":";
	}
    tmpChar = uv_strerror(errcode);
    if (tmpChar) {
        err += tmpChar;
    }
    return std::move(err);
}

//互斥量，配合CUVAutoLock使用更方便
class CUVMutex
{
public:
    explicit CUVMutex()
    {
        uv_mutex_init(&mut_);
    }
    ~CUVMutex(void)
    {
        uv_mutex_destroy(&mut_);
    }
    void Lock()
    {
        uv_mutex_lock(&mut_);
    }
    void UnLock()
    {
        uv_mutex_unlock(&mut_);
    }
    bool TryLock()
    {
        return uv_mutex_trylock(&mut_) == 0;
    }
private:
    uv_mutex_t mut_;
    friend class CUVCond;
    friend class CUVAutoLock;
private://private中，禁止复制和赋值
    CUVMutex(const CUVMutex&);//不实现
    CUVMutex& operator =(const CUVMutex&);//不实现
};

class CUVAutoLock
{
public:
    explicit CUVAutoLock(uv_mutex_t* mut): mut_(mut)
    {
        uv_mutex_lock(mut_);
    }
    explicit CUVAutoLock(CUVMutex* mut): mut_(&mut->mut_)
    {
        uv_mutex_lock(mut_);
    }
    ~CUVAutoLock(void)
    {
        uv_mutex_unlock(mut_);
    }
private:
    uv_mutex_t* mut_;
private://private中，禁止复制和赋值
    CUVAutoLock(const CUVAutoLock&);//不实现
    CUVAutoLock& operator =(const CUVAutoLock&);//不实现
};

//条件变量
class CUVCond
{
public:
    explicit CUVCond()
    {
        uv_cond_init(&cond_);
    }
    ~CUVCond(void)
    {
        uv_cond_destroy(&cond_);
    }
    void Signal()
    {
        uv_cond_signal(&cond_);
    }
    void BroadCast()
    {
        uv_cond_broadcast(&cond_);
    }
    void Wait(CUVMutex* mutex)
    {
        uv_cond_wait(&cond_, &mutex->mut_);
    }
    void Wait(uv_mutex_t* mutex)
    {
        uv_cond_wait(&cond_, mutex);
    }
    int Wait(CUVMutex* mutex, uint64_t timeout)
    {
        return uv_cond_timedwait(&cond_, &mutex->mut_, timeout);
    }
    int Wait(uv_mutex_t* mutex, uint64_t timeout)
    {
        return uv_cond_timedwait(&cond_, mutex, timeout);
    }
private:
    uv_cond_t cond_;
private://private中，禁止复制和赋值
    CUVCond(const CUVCond&);//不实现
    CUVCond& operator =(const CUVCond&);//不实现
};

//信号量
class CUVSem
{
public:
    explicit CUVSem(int initValue = 0)
    {
        uv_sem_init(&sem_, initValue);
    }
    ~CUVSem(void)
    {
        uv_sem_destroy(&sem_);
    }
    void Post()
    {
        uv_sem_post(&sem_);
    }
    void Wait()
    {
        uv_sem_wait(&sem_);
    }
    bool TryWait()
    {
        return uv_sem_trywait(&sem_) == 0;
    }
private:
    uv_sem_t sem_;
private://private中，禁止复制和赋值
    CUVSem(const CUVSem&);//不实现
    CUVSem& operator =(const CUVSem&);//不实现
};

//读写锁
class CUVRWLock
{
public:
    explicit CUVRWLock()
    {
        uv_rwlock_init(&rwlock_);
    }
    ~CUVRWLock(void)
    {
        uv_rwlock_destroy(&rwlock_);
    }
    void ReadLock()
    {
        uv_rwlock_rdlock(&rwlock_);
    }
    void ReadUnLock()
    {
        uv_rwlock_rdunlock(&rwlock_);
    }
    bool ReadTryLock()
    {
        return uv_rwlock_tryrdlock(&rwlock_) == 0;
    }
    void WriteLock()
    {
        uv_rwlock_wrlock(&rwlock_);
    }
    void WriteUnLock()
    {
        uv_rwlock_wrunlock(&rwlock_);
    }
    bool WriteTryLock()
    {
        return uv_rwlock_trywrlock(&rwlock_) == 0;
    }
private:
    uv_rwlock_t rwlock_;
private://private中，禁止复制和赋值
    CUVRWLock(const CUVRWLock&);//不实现
    CUVRWLock& operator =(const CUVRWLock&);//不实现
};


class CUVBarrier
{
public:
    explicit CUVBarrier(int count)
    {
        uv_barrier_init(&barrier_, count);
    }
    ~CUVBarrier(void)
    {
        uv_barrier_destroy(&barrier_);
    }
    void Wait()
    {
        uv_barrier_wait(&barrier_);
    }
private:
    uv_barrier_t barrier_;
private://private中，禁止复制和赋值
    CUVBarrier(const CUVBarrier&);//不实现
    CUVBarrier& operator =(const CUVBarrier&);//不实现
};

typedef void (*entry)(void* arg);
class CUVThread
{
public:
    explicit CUVThread(entry fun, void* arg)
        : fun_(fun), arg_(arg), isrunning_(false)
    {

    }
    ~CUVThread(void)
    {
        if (isrunning_) {
            uv_thread_join(&thread_);
        }
        isrunning_ = false;
    }
    void Start()
    {
        if (isrunning_) {
            return;
        }
        uv_thread_create(&thread_, fun_, arg_);
        isrunning_ = true;
    }
    void Stop()
    {
        if (!isrunning_) {
            return;
        }
        uv_thread_join(&thread_);
        isrunning_ = false;
    }
    void Sleep(int64_t millsec)
    {
        uv_thread_sleep(millsec);
    }
    int GetThreadID(void) const
    {
        return uv_thread_id();
    }
    bool IsRunning(void) const
    {
        return isrunning_;
    }
private:
    uv_thread_t thread_;
    entry fun_;
    void* arg_;
    bool isrunning_;
};
#endif //COMMON_THREAD_UV_H
