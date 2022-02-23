﻿/*
 * Log4z License
 * -----------
 *
 * Log4z is licensed under the terms of the MIT license reproduced below.
 * This means that Log4z is free software and can be used for both academic
 * and commercial purposes at absolutely no cost.
 *
 *
 * ===============================================================================
 *
 * Copyright (C) 2010-2013 YaweiZhang <yawei_zhang@foxmail.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ===============================================================================
 *
 * (end of COPYRIGHT)
 */

#include "log4z.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <string.h>

#include <string>
#include <vector>
#include <map>
#include <list>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>


#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN64)
#include <WinSock2.h>
#include <Windows.h>
#include <io.h>
#include <shlwapi.h>
#include <process.h>
#ifdef _MSC_VER
#pragma comment(lib, "shlwapi")
#pragma warning(disable:4996)
#endif
#else
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include<pthread.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <semaphore.h>
#endif

#if defined _MSC_VER
__declspec(thread) char g_log4zstreambuf[LOG4Z_LOG_BUF_SIZE];
#else
__thread char g_log4zstreambuf[LOG4Z_LOG_BUF_SIZE];
#endif

_ZSUMMER_BEGIN
_ZSUMMER_LOG4Z_BEGIN

class CLog4zFile
{
public:
	CLog4zFile()
	{
		m_file = NULL;
	}
	~CLog4zFile()
	{
		Close();
	}
	bool IsOpen()
	{
		if (m_file)
		{
			return true;
		}
		return false;
	}
	bool Open(const char *path, const char * mod)
	{
		if (m_file != NULL)
		{
			fclose(m_file);
			m_file = NULL;
		}
		m_file = fopen(path, mod);
		if (m_file == NULL)
		{
			return false;
		}
		return true;
	}
	void Close()
	{
		if (m_file != NULL)
		{
			fclose(m_file);
			m_file = NULL;
		}
	}
	void Write(const char * data, size_t len)
	{
		if (!m_file)
		{
			return;
		}
		size_t wlen = fwrite(data, 1, len, m_file);
		if (wlen != len)
		{
			Close();
		}
	}
	void Flush()
	{
		if (!m_file)
		{
			return;
		}
		fflush(m_file);
	}
	bool ReadLine(char *buf, int count)
	{
		if (fgets(buf, count, m_file) == NULL)
		{
			return false;
		}
		return true;
	}

	const std::string ReadContent()
	{
		std::string content;

		if (!m_file)
		{
			return content;
		}
		fseek(m_file, 0, SEEK_SET);
		int beginpos = ftell(m_file);
		fseek(m_file, 0, SEEK_END);
		int endpos = ftell(m_file);
		fseek(m_file, 0, SEEK_SET);
		int filelen = endpos - beginpos;
		if (filelen > 10*1024*1024 || filelen <= 0)
		{
			return content;
		}
		content.resize(filelen+10);
		if (fread(&content[0], 1, filelen, m_file) != (size_t)filelen)
		{
			content.clear();
			return content;
		}
		content = content.c_str();
		return content;
	}
public:
	FILE *m_file;
};


static const char *const LOG_STRING[]=
{
	"LOG_DEBUG",
	"LOG_INFO",
	"LOG_WARN",
	"LOG_ERROR",
	"LOG_ALARM",
	"LOG_FATAL",
};

struct LogData
{
	LoggerId _id;		//dest logger id
	int	_level;	//log level
	time_t _time;		//create time
	unsigned int _precise;
	char _content[LOG4Z_LOG_BUF_SIZE]; //content
};


struct LoggerInfo
{
	//! attribute
	std::string _name; // one logger one name.
	std::string _pid; //process id(handle)
	std::string _path; //path for log file.
	int  _level; //filter level
	bool _display; //display to screen
	bool _monthdir; //create directory per month
	unsigned int _limitsize; //limit file's size, unit Million byte.
	bool _enable; //logger is enable
	//! runtime info
	time_t _curFileCreateTime;//file create time
	unsigned int _curFileIndex;
	unsigned int _curWriteLen;
	CLog4zFile	_handle; //file handle.
	LoggerInfo()
	{
		SetDefaultInfo();
		_curFileCreateTime = 0;
		_curFileIndex = 0;
		_curWriteLen = 0;
	}
	void SetDefaultInfo()
	{
		_path = LOG4Z_DEFAULT_PATH;
		_level = LOG4Z_DEFAULT_LEVEL;
		_display = LOG4Z_DEFAULT_DISPLAY;
		_enable = false;
		_monthdir = LOG4Z_DEFAULT_MONTHDIR;
		_limitsize = LOG4Z_DEFAULT_LIMITSIZE;
	}
};

static void SleepMillisecond(unsigned int ms);
static bool TimeToTm(const time_t & t, tm * tt);
static bool IsSameDay(time_t t1, time_t t2);



static void FixPath(std::string &path);//为路径添加"\"
static void TrimLogConfig(std::string &str, char ignore = '\0');//去掉头尾的非打印字符
static void ParseConfig(const char* file, std::map<std::string, LoggerInfo> & outInfo);


static bool IsDirectory(const char* path);
static bool CreateRecursionDir(const char* path);
void GetProcessInfo(std::string &name, std::string &pid);
static void ShowColorText(const char *text, int level = LOG_LEVEL_DEBUG);

#ifdef WIN32

zsummer::log4z::CStringStream & operator <<(zsummer::log4z::CStringStream &cs, const wchar_t * t)
{
	DWORD dwLen = WideCharToMultiByte(CP_ACP, 0, t, -1, NULL, 0, NULL, NULL);
	if (dwLen < LOG4Z_LOG_BUF_SIZE)
	{
		std::string str;
		str.resize(dwLen, '\0');
		dwLen = WideCharToMultiByte(CP_ACP, 0, t, -1, &str[0], dwLen, NULL, NULL);
		if (dwLen > 0)
		{
			cs << &str[0];
		}

	}
	return cs;
}
#endif

class CLock
{
public:
	CLock()
	{
#ifdef WIN32
		InitializeCriticalSection(&m_crit);
#else
		//m_crit = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&m_crit, &attr);
		pthread_mutexattr_destroy(&attr);
#endif
	}
	virtual ~CLock()
	{
#ifdef WIN32
		DeleteCriticalSection(&m_crit);
#else
		pthread_mutex_destroy(&m_crit);
#endif
	}

public:
	void Lock()
	{
#ifdef WIN32
		EnterCriticalSection(&m_crit);
#else
		pthread_mutex_lock(&m_crit);
#endif
	}
	void UnLock()
	{
#ifdef WIN32
		LeaveCriticalSection(&m_crit);
#else
		pthread_mutex_unlock(&m_crit);
#endif
	}
private:
#ifdef WIN32
	CRITICAL_SECTION m_crit;
#else
	pthread_mutex_t  m_crit;
#endif
};

class CAutoLock
{
public:
	explicit CAutoLock(CLock & lk):m_lock(lk){m_lock.Lock();}
	~CAutoLock(){m_lock.UnLock();}
private:
	CLock & m_lock;
};

class CSem
{
public:
	CSem()
	{
#ifdef WIN32
		m_hSem = NULL;
#else
		m_bCreate = false;
#endif
	}
	virtual ~CSem()
	{
#ifdef WIN32
		if (m_hSem != NULL)
		{
			CloseHandle(m_hSem);
			m_hSem = NULL;
		}
#else
		if (m_bCreate)
		{
			m_bCreate = false;
			sem_destroy(&m_semid);
		}
#endif
	}
public:
	bool Create(int initcount)
	{
		if (initcount < 0)
		{
			initcount = 0;
		}
#ifdef WIN32
		if (initcount > 64)
		{
			return false;
		}
		m_hSem = CreateSemaphore(NULL, initcount, 64, NULL);
		if (m_hSem == NULL)
		{
			return false;
		}
#else
		if (sem_init(&m_semid, 0, initcount) != 0)
		{
			return false;
		}
		m_bCreate = true;
#endif
		return true;

	}
	bool Wait(int timeout = 0)
	{
#ifdef WIN32
		if (timeout <= 0)
		{
			timeout = INFINITE;
		}
		if (WaitForSingleObject(m_hSem, timeout) != WAIT_OBJECT_0)
		{
			return false;
		}
#else
		if (timeout <= 0)
		{
			return (sem_wait(&m_semid) == 0);
		}
		else
		{
			timespec ts;
			ts.tv_sec = time(NULL) + timeout/1000;
			ts.tv_nsec = (timeout%1000)*1000000;
			return (sem_timedwait(&m_semid, &ts) == 0);
		}
#endif
		return true;
	}
	bool Post()
	{
#ifdef WIN32
		return ReleaseSemaphore(m_hSem, 1, NULL) ? true : false;
#else
		return (sem_post(&m_semid) == 0);
#endif
	}
private:
#ifdef WIN32
	HANDLE m_hSem;
#else
	sem_t m_semid;
	bool  m_bCreate;
#endif
};




#ifdef WIN32
static unsigned int WINAPI  ThreadProc(LPVOID lpParam);
#else
static void * ThreadProc(void * pParam);
#endif
class CThread
{
public:
	CThread(){m_hThreadID = 0;}
	virtual ~CThread(){}
public:
	bool Start()
	{
#ifdef WIN32
		unsigned long long ret = _beginthreadex(NULL, 0, ThreadProc, (void *) this, 0, NULL);

		if (ret == -1 || ret == 1  || ret == 0)
		{
			ShowColorText("log4z: create log4z thread error! \r\n", LOG_LEVEL_FATAL);
			return false;
		}
		m_hThreadID = ret;
#else
		pthread_t ptid = 0;
		int ret = pthread_create(&ptid, NULL, ThreadProc, (void*)this);
		if (ret != 0)
		{
			ShowColorText("log4z: create log4z thread error! \r\n", LOG_LEVEL_FATAL);
			return false;
		}
		m_hThreadID = ptid;

#endif
		return true;
	}
	bool Wait()
	{
#ifdef WIN32
		if (WaitForSingleObject((HANDLE)m_hThreadID, INFINITE) != WAIT_OBJECT_0)
		{
			return false;
		}
#else
		if (pthread_join((pthread_t)m_hThreadID, NULL) != 0)
		{
			return false;
		}
#endif
		return true;
	}

	virtual void Run() = 0;
	inline unsigned long long GetThreadID() {return m_hThreadID;};
private:
	unsigned long long m_hThreadID;
};

#ifdef WIN32
unsigned int WINAPI  ThreadProc(LPVOID lpParam)
{
	CThread * p = (CThread *) lpParam;
	p->Run();
	_endthreadex(0);
	return 0;
}
#else
void * ThreadProc(void * pParam)
{
	CThread * p = (CThread *) pParam;
	p->Run();
	return NULL;
}
#endif










class CLogerManager : public CThread, public ILog4zManager
{
public:
	CLogerManager()
	{
		m_bRuning = false;
		m_lastId = LOG4Z_MAIN_LOGGER_ID;
		GetProcessInfo(m_loggers[LOG4Z_MAIN_LOGGER_ID]._name, m_loggers[LOG4Z_MAIN_LOGGER_ID]._pid);
		m_ids[LOG4Z_MAIN_LOGGER_NAME] = LOG4Z_MAIN_LOGGER_ID;

		m_ullStatusTotalPushLog = 0;
		m_ullStatusTotalPopLog = 0;
		m_ullStatusTotalWriteFileCount = 0;
		m_ullStatusTotalWriteFileBytes = 0;
	}
	~CLogerManager()
	{
		Stop();
	}

	std::string GetExampleConfig()
	{
		return ""
			"[FileConfig]\n"
			"#path=./log/\n"
			"#level=DEBUG\n"
			"#display=true\n"
			"#monthdir=false\n"
			"#limit=100\n";
	}


	//! 读取配置文件并覆写
	bool Config(const char* cfgPath)
	{
		if (!m_configFile.empty())
		{
			std::cout << "log4z configure error: too many too call Config. the old config file="<< m_configFile << ", the new config file=" << cfgPath << std::endl;
			return false;
		}
		m_configFile = cfgPath;
		std::map<std::string, LoggerInfo> loggerMap;
		ParseConfig(cfgPath, loggerMap);
		for (std::map<std::string, LoggerInfo>::iterator iter = loggerMap.begin(); iter != loggerMap.end(); ++iter)
		{
			CreateLogger(iter->second._name.c_str(),
				iter->second._path.c_str(),
				iter->second._level,
				iter->second._display,
				iter->second._monthdir,
				iter->second._limitsize);
		}
		return true;
	}

	//! 覆写式创建
	virtual LoggerId CreateLogger(const char* name,const char* path,int nLevel,bool display, bool monthdir, unsigned int limitsize)
	{
		std::string _tmp;
		std::string _pid;
		GetProcessInfo(_tmp, _pid);
		if (strlen(name) == 0)
		{
			ShowColorText("log4z: create logger error, name is empty ! \r\n", LOG_LEVEL_FATAL);
			return -1;
		}
		std::string fullpath = path;
		TrimLogConfig(fullpath);
		FixPath(fullpath);

		LoggerId newID = -1;
		{
			std::map<std::string, LoggerId>::iterator iter = m_ids.find(name);
			if (iter != m_ids.end())
			{
				newID = iter->second;
			}
		}
		if (newID == -1)
		{
			if (m_lastId +1 >= LOG4Z_LOGGER_MAX)
			{
				ShowColorText("log4z: CreateLogger can not create|writeover, because loggerid need < LOGGER_MAX! \r\n", LOG_LEVEL_FATAL);
				return -1;
			}
			newID = ++ m_lastId;
			m_ids[name] = newID;
		}

		if (!fullpath.empty())
		{
			m_loggers[newID]._path = fullpath;
		}

		if (newID > LOG4Z_MAIN_LOGGER_ID)
		{
			m_loggers[newID]._name = name;
		}
		m_loggers[newID]._pid = _pid;
		m_loggers[newID]._level = nLevel;
		m_loggers[newID]._enable = true;
		m_loggers[newID]._display = display;
		m_loggers[newID]._monthdir = monthdir;
		m_loggers[newID]._limitsize = limitsize;
		if (limitsize == 0)
		{
			m_loggers[newID]._limitsize = 4000;
		}

		return newID;
	}


	bool Start()
	{
		if (m_bRuning)
		{
			return false;
		}
		m_semaphore.Create(0);
		bool ret = CThread::Start();
		return ret && m_semaphore.Wait(3000);
	}
	bool Stop()
	{
		if (m_bRuning == true)
		{
			m_bRuning = false;
			Wait();
			return true;
		}
		return false;
	}

	bool PushLog(LoggerId id, int level, const char * log)
	{
		if (id < 0 || id >= LOG4Z_LOGGER_MAX)
		{
			return false;
		}
		if (!m_bRuning || !m_loggers[id]._enable)
		{
			return false;
		}
		if (level < m_loggers[id]._level)
		{
			return true;
		}

		LogData * pLog = new LogData;
		pLog->_id =id;
		pLog->_level = level;

		{
#ifdef WIN32
			FILETIME ft;
			GetSystemTimeAsFileTime(&ft);
			unsigned long long now = ft.dwHighDateTime;
			now <<= 32;
			now |= ft.dwLowDateTime;
			now /=10;
			now -=11644473600000000ULL;
			now /=1000;
			pLog->_time = now/1000;
			pLog->_precise = (unsigned int)(now%1000);
#else
			struct timeval tm;
			gettimeofday(&tm, NULL);
			pLog->_time = tm.tv_sec;
			pLog->_precise = tm.tv_usec/1000;
#endif
		}

		if (m_loggers[pLog->_id]._display && LOG4Z_SYNCHRONOUS_DISPLAY)
		{
			tm tt;
			if (!TimeToTm(pLog->_time, &tt))
			{
				memset(&tt, 0, sizeof(tt));
			}
			std::string text;
			sprintf(pLog->_content, "%d-%02d-%02d %02d:%02d:%02d.%03d %s ",
				tt.tm_year+1900, tt.tm_mon+1, tt.tm_mday, tt.tm_hour, tt.tm_min, tt.tm_sec, pLog->_precise,
				LOG_STRING[pLog->_level]);
			text = pLog->_content;
			text += log;
			text += " \r\n";
			ShowColorText(text.c_str(), pLog->_level);
		}

		int len = (int) strlen(log);
		if (len >= LOG4Z_LOG_BUF_SIZE)
		{
			memcpy(pLog->_content, log, LOG4Z_LOG_BUF_SIZE);
			pLog->_content[LOG4Z_LOG_BUF_SIZE-1] = '\0';
		}
		else
		{
			memcpy(pLog->_content, log, len+1);
		}
		CAutoLock l(m_lock);
		m_logs.push_back(pLog);
		m_ullStatusTotalPushLog ++;
		return true;
	}

	//! 查找ID
	virtual LoggerId FindLogger(const std::string& name)
	{
		std::map<std::string, LoggerId>::iterator iter;
		iter = m_ids.find(name);
		if (iter != m_ids.end())
		{
			return iter->second;
		}
		return LOG4Z_INVALID_LOGGER_ID;
	}

	bool SetLoggerLevel(LoggerId nLoggerID, int nLevel)
	{
		if (nLoggerID <0 || nLoggerID >= LOG4Z_LOGGER_MAX || nLevel < LOG_LEVEL_DEBUG || nLevel >LOG_LEVEL_FATAL) return false;
		m_loggers[nLoggerID]._level = nLevel;
		return true;
	}
	bool SetLoggerDisplay(LoggerId nLoggerID, bool enable)
	{
		if (nLoggerID <0 || nLoggerID >= LOG4Z_LOGGER_MAX) return false;
		m_loggers[nLoggerID]._display = enable;
		return true;
	}
	bool SetLoggerPath(LoggerId nLoggerID, const char* path)
	{
		if (nLoggerID <0 || nLoggerID >= LOG4Z_LOGGER_MAX) return false;
		m_loggers[nLoggerID]._path = path;
		return true;
	}
	bool SetLoggerMonthdir(LoggerId nLoggerID, bool use)
	{
		if (nLoggerID <0 || nLoggerID >= LOG4Z_LOGGER_MAX) return false;
		m_loggers[nLoggerID]._monthdir = use;
		return true;
	}
	bool SetLoggerLimitSize(LoggerId nLoggerID, unsigned int limitsize)
	{
		if (nLoggerID <0 || nLoggerID >= LOG4Z_LOGGER_MAX) return false;
		if (limitsize == 0 ) {limitsize = (unsigned int)-1;}
		m_loggers[nLoggerID]._limitsize = limitsize;
		return true;
	}
	bool UpdateConfig()
	{
		if (m_configFile.empty())
		{
			return false;
		}
		std::map<std::string, LoggerInfo> loggerMap;
		ParseConfig(m_configFile.c_str(), loggerMap);
		for (std::map<std::string, LoggerInfo>::iterator iter = loggerMap.begin(); iter != loggerMap.end(); ++iter)
		{
			LoggerId id = FindLogger(iter->first);
			if (id != LOG4Z_INVALID_LOGGER_ID)
			{
				SetLoggerDisplay(id, iter->second._display);
				SetLoggerLevel(id, iter->second._level);
				SetLoggerMonthdir(id, iter->second._monthdir);
				SetLoggerLimitSize(id, iter->second._limitsize);
			}
		}
		return true;
	}
	unsigned long long GetStatusTotalWriteCount()
	{
		return m_ullStatusTotalWriteFileCount;
	}
	unsigned long long GetStatusTotalWriteBytes()
	{
		return m_ullStatusTotalWriteFileBytes;
	}
	unsigned long long GetStatusWaitingCount()
	{
		return m_ullStatusTotalPushLog - m_ullStatusTotalPopLog;
	}
	unsigned int GetStatusActiveLoggers()
	{
		unsigned int actives = 0;
		for (int i=0; i<LOG4Z_LOGGER_MAX; i++)
		{
			if (m_loggers[i]._enable)
			{
				actives ++;
			}
		}
		return actives;
	}


protected:


	bool OpenLogger(LoggerId id)
	{
		if (id < 0 || id >m_lastId)
		{
			ShowColorText("log4z: OpenLogger can not open, invalide logger id! \r\n", LOG_LEVEL_FATAL);
			return false;
		}
		LoggerInfo * pLogger = &m_loggers[id];
		if (pLogger->_handle.IsOpen())
		{
			pLogger->_handle.Close();
		}

		tm t;
		TimeToTm(pLogger->_curFileCreateTime, &t);
		std::string path = pLogger->_path;
		char buf[100]={0};
		if (pLogger->_monthdir)
		{
			sprintf(buf, "%04d_%02d/", t.tm_year+1900, t.tm_mon+1);
			path += buf;
		}

		if (!IsDirectory(path.c_str()))
		{
			CreateRecursionDir(path.c_str());
		}

		sprintf(buf, "%s_%04d%02d%02d_%02d%02d%02d_%03d.log",
			pLogger->_name.c_str(),  t.tm_year+1900, t.tm_mon+1, t.tm_mday,
			t.tm_hour, t.tm_min, t.tm_sec, pLogger->_curFileIndex);
		path += buf;
		pLogger->_handle.Open(path.c_str(), "ab");
		return pLogger->_handle.IsOpen();
	}

	bool PopLog(LogData *& log)
	{
		CAutoLock l(m_lock);
		if (m_logs.empty())
		{
			return false;
		}
		log = m_logs.front();
		m_logs.pop_front();
		return true;
	}
	virtual void Run()
	{
		m_bRuning = true;
		m_loggers[LOG4Z_MAIN_LOGGER_ID]._enable = true;
		PushLog(0, LOG_LEVEL_ALARM, "-----------------  log thread runing...   ----------------------------");
		for (int i=0; i<LOG4Z_LOGGER_MAX; i++)
		{
			if (m_loggers[i]._enable)
			{
				std::stringstream ss;
				ss <<" logger id=" <<i
				   <<" path=" <<m_loggers[i]._path
				   <<" name=" <<m_loggers[i]._name
				   <<" level=" << m_loggers[i]._level
				   <<" display=" << m_loggers[i]._display;
				PushLog(0, LOG_LEVEL_ALARM, ss.str().c_str());
			}
		}

		m_semaphore.Post();


		LogData * pLog = NULL;
		char *pWriteBuf = new char[LOG4Z_LOG_BUF_SIZE + 512];
		int needFlush[LOG4Z_LOGGER_MAX] = {0};
		while (true)
		{
			while(PopLog(pLog))
			{
				//
				m_ullStatusTotalPopLog ++;
				//discard
				LoggerInfo & curLogger = m_loggers[pLog->_id];
				if (!curLogger._enable || pLog->_level <curLogger._level  )
				{
					delete pLog;
					pLog = NULL;
					continue;
				}

				//update file
				if (LOG4Z_WRITE_TO_FILE)
				{
					bool sameday = IsSameDay(pLog->_time, curLogger._curFileCreateTime);
					bool needChageFile = curLogger._curWriteLen > curLogger._limitsize*1024*1024;
					if (!curLogger._handle.IsOpen()
						|| !sameday
						|| needChageFile)
					{
						if (!sameday)
						{
							curLogger._curFileIndex = 0;
							curLogger._curWriteLen = 0;
						}
						else if ( needChageFile)
						{
							curLogger._curFileIndex ++;
							curLogger._curWriteLen = 0;
						}
						curLogger._curFileCreateTime = pLog->_time;
						if (!OpenLogger(pLog->_id))
						{
							curLogger._enable = false;
							delete pLog;
							pLog = NULL;
							ShowColorText("log4z: Run can not update file, open file false! \r\n", LOG_LEVEL_FATAL);
							continue;
						}
					}
				}

				//record
				tm tt;
				if (!TimeToTm(pLog->_time, &tt))
				{
					memset(&tt, 0, sizeof(tt));
				}
				sprintf(pWriteBuf, "%d-%02d-%02d %02d:%02d:%02d.%03d %s %s \r\n",
					tt.tm_year+1900, tt.tm_mon+1, tt.tm_mday, tt.tm_hour, tt.tm_min, tt.tm_sec, pLog->_precise,
					LOG_STRING[pLog->_level], pLog->_content);

				if (LOG4Z_WRITE_TO_FILE)
				{
					size_t writeLen = strlen(pWriteBuf);
					curLogger._handle.Write(pWriteBuf, writeLen);
					curLogger._curWriteLen += (unsigned int)writeLen;
					needFlush[pLog->_id] ++;
					m_ullStatusTotalWriteFileCount++;
					m_ullStatusTotalWriteFileBytes += writeLen;
				}
				else
				{
					size_t writeLen = strlen(pWriteBuf);
					m_ullStatusTotalWriteFileCount++;
					m_ullStatusTotalWriteFileBytes += writeLen;
				}

				if (curLogger._display && !LOG4Z_SYNCHRONOUS_DISPLAY)
				{
					ShowColorText(pWriteBuf, pLog->_level);
				}



				delete pLog;
				pLog = NULL;
			}

			for (int i=0; i<LOG4Z_LOGGER_MAX; i++)
			{
				if (m_loggers[i]._enable && needFlush[i] > 0)
				{
					m_loggers[i]._handle.Flush();
					needFlush[i] = 0;
				}
			}

			//! quit
			if (!m_bRuning && m_logs.empty())
			{
				break;
			}
			//! delay.
			SleepMillisecond(100);
		}

		for (int i=0; i<LOG4Z_LOGGER_MAX; i++)
		{
			if (m_loggers[i]._enable)
			{
				m_loggers[i]._enable = false;
				m_loggers[i]._handle.Close();
			}
		}
		delete []pWriteBuf;
		pWriteBuf = NULL;

	}

private:

	//! thread status.
	bool		m_bRuning;
	//! wait thread started.
	CSem		m_semaphore;

	//! config file name
	std::string m_configFile;

	//! logger id manager.
	std::map<std::string, LoggerId> m_ids; //[logger name]:[logger id]
	LoggerId	m_lastId; // the last used id of m_loggers
	LoggerInfo m_loggers[LOG4Z_LOGGER_MAX];

	//! log queue
	std::list<LogData *> m_logs;
	CLock	m_lock;

	//status statistics
	//write file
	unsigned long long m_ullStatusTotalWriteFileCount;
	unsigned long long m_ullStatusTotalWriteFileBytes;

	//Log queue statistics
	unsigned long long m_ullStatusTotalPushLog;
	unsigned long long m_ullStatusTotalPopLog;

};




void SleepMillisecond(unsigned int ms)
{
#ifdef WIN32
	::Sleep(ms);
#else
	usleep(1000*ms);
#endif
}

bool TimeToTm(const time_t &t, tm * tt)
{
#ifdef WIN32
    *tt = *localtime(&t);
    return true;
#else
	if (localtime_r(&t, tt) != NULL)
	{
		return true;
	}
	return false;
#endif
}

bool IsSameDay(time_t t1, time_t t2)
{
	tm tm1;
	tm tm2;
	TimeToTm(t1, &tm1);
	TimeToTm(t2, &tm2);
	if ( tm1.tm_year == tm2.tm_year
		&& tm1.tm_yday == tm2.tm_yday)
	{
		return true;
	}
	return false;
}


void FixPath(std::string &path)
{
	if (path.empty())
	{
		return;
	}
	for (std::string::iterator iter = path.begin(); iter != path.end(); ++iter)
	{
		if (*iter == '\\')
		{
			*iter = '/';
		}
	}
	if (path.at(path.length()-1) != '/')
	{
		path += "/";
	}
}
static void TrimLogConfig(std::string &str, char ignore)
{
	if (str.empty())
	{
		return;
	}
	size_t endPos = str.length();
	int posBegin = (int)endPos;
	int posEnd = -1;

	for (size_t i = 0; i<str.length(); i++)
	{
		char ch = str[i];
		if (ch != '\r' && ch != '\n' && ch != ' ' && ch != '\t' && ch != ignore)
		{
			posBegin = (int)i;
			break;
		}
	}
	for (size_t i = endPos; i> 0; i--)
	{
		char ch = str[i-1];
		if (ch != '\r' && ch != '\n' && ch != ' ' && ch != '\t' && ch != ignore)
		{
			posEnd = (int)i-1;
			break;
		}
	}
	if (posBegin <= posEnd)
	{
		str = str.substr(posBegin, posEnd-posBegin+1);
	}
	else
	{
		str.clear();
	}
}

static void ParseConfig(const char* file, std::map<std::string, LoggerInfo> & outInfo)
{
	//! read file content
	{
		CLog4zFile f;
		f.Open(file, "r");

		if (f.IsOpen())
		{
			std::string curLoggerName;
			int curLineNum = 0;
			char buf[500];
			std::string line;
			std::string key;
			std::string value;
			do
			{
				memset(buf, 0, 500);
				if (!f.ReadLine(buf, 500-1))
				{
					break;
				}
				line = buf;
				curLineNum++;
				TrimLogConfig(line);

				if (line.empty())
				{
					continue;
				}
				if (*(line.begin()) == '#')
				{
					continue;
				}
				if (*(line.begin()) == '[')
				{
					TrimLogConfig(line, '[');
					TrimLogConfig(line, ']');
					curLoggerName = line;
					{
						std::string tmpstr = line;
						std::transform(tmpstr.begin(), tmpstr.end(), tmpstr.begin(), ::tolower);
						if (tmpstr == "main")
						{
							curLoggerName = "Main";
						}
					}
					std::map<std::string, LoggerInfo>::iterator iter = outInfo.find(curLoggerName);
					if (iter == outInfo.end())
					{
						LoggerInfo li;
						li.SetDefaultInfo();
						li._name = curLoggerName;
						outInfo.insert(std::make_pair(li._name, li));
					}
					else
					{
						std::cout << "log4z configure warning: dumplicate logger name:["<< curLoggerName << "] at line:" << curLineNum << std::endl;
					}
					continue;
				}
				size_t pos = line.find_first_of('=');
				if (pos == std::string::npos)
				{
					std::cout << "log4z configure warning: unresolved line:["<< line << "] at line:" << curLineNum << std::endl;
					continue;
				}
				key = line.substr(0, pos);
				value = line.substr(pos+1);
				TrimLogConfig(key);
				TrimLogConfig(value);
				std::map<std::string, LoggerInfo>::iterator iter = outInfo.find(curLoggerName);
				if (iter == outInfo.end())
				{
					std::cout << "log4z configure warning: not found current logger name:["<< curLoggerName << "] at line:" << curLineNum
						<< ", key=" <<key << ", value=" << value << std::endl;
					continue;
				}
				std::transform(key.begin(), key.end(), key.begin(), ::tolower);
				//! path
				if (key == "path")
				{
					iter->second._path = value;
					continue;
				}
				std::transform(value.begin(), value.end(), value.begin(), ::tolower);
				//! level
				if (key == "level")
				{
					if (value == "debug" || value == "all")
					{
						iter->second._level = LOG_LEVEL_DEBUG;
					}
					else if (value == "info")
					{
						iter->second._level = LOG_LEVEL_INFO;
					}
					else if (value == "warn" || value == "warning")
					{
						iter->second._level = LOG_LEVEL_WARN;
					}
					else if (value == "error")
					{
						iter->second._level = LOG_LEVEL_ERROR;
					}
					else if (value == "alarm")
					{
						iter->second._level = LOG_LEVEL_WARN;
					}
					else if (value == "fatal")
					{
						iter->second._level = LOG_LEVEL_FATAL;
					}
				}
				//! display
				else if (key == "display")
				{
					if (value == "false" || value == "0")
					{
						iter->second._display = false;
					}
					else
					{
						iter->second._display = true;
					}
				}
				//! monthdir
				else if (key == "monthdir")
				{
					if (value == "false" || value == "0")
					{
						iter->second._monthdir = false;
					}
					else
					{
						iter->second._monthdir = true;
					}
				}
				//! limit file size
				else if (key == "limitsize")
				{
					iter->second._limitsize = atoi(value.c_str());
				}

			} while (1);
		}
	}
}


bool IsDirectory(const char* path)
{
#ifdef WIN32
	return PathIsDirectoryA(path) ? true : false;
#else
	DIR * pdir = opendir(path);
	if (pdir == NULL)
	{
		return false;
	}
	else
	{
		closedir(pdir);
		pdir = NULL;
		return true;
	}
#endif
}



bool CreateRecursionDir(const char* path)
{
	std::string fullpath = path;
	if (fullpath.length() == 0) return true;
	FixPath(fullpath);

	std::string::size_type pos = fullpath.find('/');
	std::string cur;
	while (pos != std::string::npos)
	{
		cur = fullpath.substr(0, pos-0);
		if (cur.length() > 0 && !IsDirectory(cur.c_str()))
		{
			bool ret = false;
#ifdef WIN32
			ret = CreateDirectoryA(cur.c_str(), NULL) ? true : false;
#else
			ret = (mkdir(cur.c_str(), S_IRWXU|S_IRWXG|S_IRWXO) == 0);
#endif
			if (!ret)
			{
				return false;
			}
		}
		pos = fullpath.find('/', pos+1);
	}

	return true;
}

void GetProcessInfo(std::string &name, std::string &pid)
{
	name = "MainLog";
	pid = "0";
#ifdef WIN32

	char buf[260] = {0};
	if (GetModuleFileNameA(NULL, buf, 259) > 0)
	{
		name = buf;
	}
	std::string::size_type pos = name.rfind("\\");
	if (pos != std::string::npos)
	{
		name = name.substr(pos+1, std::string::npos);
	}
	pos = name.rfind(".");
	if (pos != std::string::npos)
	{
		name = name.substr(0, pos-0);
	}
	DWORD pidd = GetCurrentProcessId();
	sprintf(buf, "%06d", pidd);
	pid = buf;
#else
	pid_t id = getpid();
	char buf[260];
	sprintf(buf, "/proc/%d/cmdline", (int)id);
	CLog4zFile i;
	i.Open(buf, "r");
	if (!i.IsOpen())
	{
		return ;
	}
	if (i.ReadLine(buf, 259))
	{
		name = buf;
	}
	i.Close();

	std::string::size_type pos = name.rfind("/");
	if (pos != std::string::npos)
	{
		name = name.substr(pos+1, std::string::npos);
	}
	sprintf(buf, "%06d", id);
	pid = buf;
#endif
}



#ifdef WIN32
CLock gs_ShowColorTextLock;
const static WORD cs_sColor[LOG_LEVEL_FATAL+1] = {
	0,
	FOREGROUND_BLUE|FOREGROUND_GREEN,
	FOREGROUND_GREEN|FOREGROUND_RED,
	FOREGROUND_RED,
	FOREGROUND_GREEN,
	FOREGROUND_RED|FOREGROUND_BLUE};
#else

const static char cs_strColor[LOG_LEVEL_FATAL+1][50] = {
	"\e[0m",
	"\e[34m\e[1m",//hight blue
	"\e[33m", //yellow
	"\e[31m", //red
	"\e[32m", //green
	"\e[35m"};
#endif

void ShowColorText(const char *text, int level)
{
	if (level < LOG_LEVEL_DEBUG || level > LOG_LEVEL_FATAL)
    {
	    printf("%s", text);
	    return;
    }

	if (level == LOG_LEVEL_DEBUG)
    {
	    printf("%s", text);
	    return;
    }

#ifndef WIN32
	printf("%s%s\e[0m", cs_strColor[level], text);
#else
	HANDLE hStd = ::GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStd == INVALID_HANDLE_VALUE)
    {
	    printf("%s", text);
	    return;
    }
	CONSOLE_SCREEN_BUFFER_INFO oldInfo;
	if (!GetConsoleScreenBufferInfo(hStd, &oldInfo))
    {
	    printf("%s", text);
	    return;
    }

	{
		CAutoLock l(gs_ShowColorTextLock);
		SetConsoleTextAttribute(hStd, cs_sColor[level]);
		printf("%s", text);
		SetConsoleTextAttribute(hStd, oldInfo.wAttributes);
	}


#endif

	return;
}

ILog4zManager* ILog4zManager::m_instance = new CLogerManager;
ILog4zManager * ILog4zManager::GetInstance()
{
	//static CLogerManager m;
	//return &m;
	return m_instance;
}



_ZSUMMER_LOG4Z_END
_ZSUMMER_END

