/*
 * Types.h
 *
 *  Created on: Mar 2, 2011
 *      Author: christen
 */
#ifndef __SNAPSHOTFS_TYPES_H
#define __SNAPSHOTFS_TYPES_H

#include <syslog.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdint.h>


#define SYSLOG(format, args...) \
	if(g_config.debug) \
	{ \
	   FILE *file = fopen(g_config.logFile, "a+"); \
	   if (!file) file = stdout; \
	   fprintf(file, "tid=%u %s %s: " format "\n", (uint)pthread_self(), __FUNCTION__, g_config.mountPoint, args); \
	   fclose(file); \
	}


#define SYSLOG_ERROR(format, args...) \
	{ \
	   FILE *file = fopen(g_config.logFile, "a+"); \
	   if (!file) file = stderr; \
	   fprintf(file,"tid=%u ##error## %s:%d ", (uint)pthread_self(),  __FUNCTION__, __LINE__); \
	   fprintf(file, "%s: " format "\n", g_config.mountPoint, args); \
	   fclose(file); \
	}
#define SYSLOG_DELETE() \
	{ \
       unlink(g_config.logFile); \
	}

typedef struct Config
{
	char logFile[NAME_MAX];
	char cacheDir[NAME_MAX];
	char mountPoint[NAME_MAX];
	char rmtUser[NAME_MAX];
	char rmtHost[NAME_MAX];
	char rmtDir[NAME_MAX];
	char pass[NAME_MAX]; 		// TODO encrypt password
	bool refreshDirEntries; 	// refresh Directory entries
	bool refreshOpenedFiles; 	// refresh open files
	bool readonly;				// read-only file system
	bool singleThread;
	bool debug;
	bool offline;
	uint32_t port;
} Config_t;

extern Config g_config;

#define SNAPSHOTFS_REFRESH_START_TIME_FILE ".%_snapshotfs_refresh_start_time"
//#define SNAPSHOTFS_STAT_PREFIX "._snapshotfs_"
//#define SNAPSHOTFS_STAT_PREFIX_SIZE sizeof(SNAPSHOTFS_STAT_PREFIX)-1
//#define SNAPSHOTFS_POPULATE_DONE_FILE ".%_snapshotfs_populate_done"

class PathBase
{
public:
	inline bool exists() const
	{
		struct stat statInfo;
		return !lstat(ivPath, &statInfo);
	}

	inline const char* cdToParent()
	{
		*strrchr(ivPath, '/') = '\0';
		if(!*ivPath) {
			strcpy(ivPath, "/");
		}
		return ivPath;
	}

	inline const char* fileName() const
	{
		return (strrchr(ivPath, '/')+1);
	}

	inline const char* append(const char *iName)
	{
		strcat(ivPath, "/");
		strcat(ivPath, iName);
		return ivPath;
	}

protected:
	inline void fixPath()
	{
		char *slash2 = NULL;
		while((slash2 = strstr(ivPath, "//")))
		{
			uint32_t len = strlen(slash2);
			memmove(slash2, slash2+1, len-1);
			slash2[len-1] = '\0';
		}
		uint32_t len = strlen(ivPath);
		if(len > 1 && ivPath[len-1] == '/')
		{
			ivPath[len-1] = '\0';
		}
	}

	char ivPath[PATH_MAX];
};

class RelativePath : public PathBase
{
public:
	RelativePath(const char *iRelativePath, const char *iFileName=NULL)
	{
		strcpy(ivPath, iRelativePath);
		if(iFileName)
		{
			strcat(ivPath, "/");
			strcat(ivPath, iFileName);
		}
		fixPath();
	}
	operator char*() {return ivPath;}
	operator const char*() {return ivPath;}
	const char* toString() const {return ivPath;}
};

class CacheDir : public PathBase
{
public:
	CacheDir(const char *iRelativePath, const char *iFileName=NULL)

	{
		if(iFileName)
		{
			snprintf(ivPath, sizeof(ivPath), "%s/%s/%s", g_config.cacheDir, iRelativePath, iFileName);
		}
		else
		{
			snprintf(ivPath, sizeof(ivPath), "%s/%s", g_config.cacheDir, iRelativePath);
		}
		fixPath();
	}
	operator char*() {return ivPath;}
	operator const char*() {return ivPath;}
	const char* toString() const {return ivPath;}
private:
};

class RmtDir : public PathBase
{
public:
	RmtDir(const char *iRelativePath, const char *iFileName=NULL)

	{
		if(iFileName)
		{
			snprintf(ivPath, sizeof(ivPath), "%s/%s/%s", g_config.rmtDir, iRelativePath, iFileName);
		}
		else
		{
			snprintf(ivPath, sizeof(ivPath), "%s/%s", g_config.rmtDir, iRelativePath);
		}
		fixPath();
	}
	operator const char*() {return ivPath;}
	const char* toString() const {return ivPath;}
private:
};

// File handle definition
typedef struct FileHandle
{
	enum Type {
		TypeUnknow 	= 0,
		TypeWrite	= 1
	};

	FileHandle(int iFd, Type iType) {
		ivFh.type = iType;
		ivFh.fd = iFd;
	}

	static uint64_t set(FileHandle *ipFileHandle) {return (uint64_t)ipFileHandle;}
	static FileHandle* get(uint64_t iFh) {return (FileHandle*)iFh;}

	int getFd() const {return ivFh.fd;}
	void setFd(int iFd) {ivFh.fd = iFd;}

	Type getType() const {return (Type)ivFh.type;}
	void setType(Type iType) {ivFh.type = iType;}

private:
	typedef struct Fh {
		uint64_t type:32;
		uint64_t fd:32;
	} Fh_t;

	Fh ivFh;

} FileHandle_t;

#endif
