/*
 * sync.C
 *
 *  Created on: Mar 8, 2011
 *      Author: christen
 */

#include "FileSystem.H"
#include "Cache.H"
#include "RmtFs.H"
#include "Metadata.H"

#include <stdio.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/statfs.h>
#include <limits.h>
#include <errno.h>
#include <sys/time.h>
#include <fstream>
#include <vector>
#include <ftw.h>
#include <sys/stat.h>
#include <time.h>
#include <list>

#include "Types.H"

#include <pthread.h>

using namespace std;

#define MULTI_THREADED

// Use lftp for 10M or greater files
#define MIN_LFTP_SIZE 10000000
//#define MIN_LFTP_SIZE 10000000000

int Cache::lftpGet(const char *iRelativePath)
{
	int rc = 0;
	char cmd[PATH_MAX];
	RmtDir rmtPath(iRelativePath);
	sprintf(cmd, "lftp -c 'open sftp://%s:%d; user %s %s; pget %s -o %s'",
			g_config.rmtHost,
			g_config.port,
			g_config.rmtUser, g_config.pass,
			rmtPath.toString(),
			CacheDir(iRelativePath).toString());
	SYSLOG("lftp pget %s", rmtPath.toString());
	rc = system(cmd);
	if(rc)
	{
		SYSLOG_ERROR("%d on %s", rc, cmd);
	}
	return rc;
}

int Cache::lftpMirror(const char *iRelativePath)
{
	int rc = 0;
	RmtDir rmtDir(iRelativePath);
	// Note, status file begining with "._snapshotfs_" are not deleted.
	char options[100];
	sprintf(options, "--use-pget --no-recursion --delete --only-existing");
	char cmd[PATH_MAX];
	sprintf(cmd, "lftp -c 'open sftp://%s; user %s %s; mirror %s %s %s'",
			g_config.rmtHost,
			g_config.rmtUser, g_config.pass,
			options,
			rmtDir.toString(),
			CacheDir(iRelativePath).toString());
	SYSLOG("lftp mirror %s", rmtDir.toString());
	rc = system(cmd);
	if(rc)
	{
		printf("error %d on %s\n", rc, cmd);
		SYSLOG_ERROR("return code %d on %s", rc, cmd);
	}
	return rc;
}

//int Cache::lftpPut(const char *iRelativePath)
//{
//	int rc = 0;
//	char cmd[PATH_MAX];
//	RmtDir rmtPath(iRelativePath);
//	sprintf(cmd, "lftp -c 'open sftp://%s; user %s %s; put %s -o %s'",
//			g_config.rmtHost,
//			g_config.rmtUser, g_config.pass,
//			CacheDir(iRelativePath).toString(),
//			rmtPath.toString());
//	SYSLOG("lftp put %s", rmtPath.toString());
//	rc = system(cmd);
//	if(rc)
//	{
//		SYSLOG_ERROR("%d on %s", rc, cmd);
//	}
//	return rc;
//}

bool Cache::isPopulated(const char *iRelativePath, const Metadata::EntryStat &iMdEntry)
{
	bool populated = false;

	if(iMdEntry.populateTime)
	{
		// Directory entries have been refreshed?
		if(g_config.refreshDirEntries && iMdEntry.populateTime < FileSystem::getRefreshStartTime())
		{
			SYSLOG("Refresh populated entries: %s", iRelativePath);
		}
		else
		{
			SYSLOG("Already populated: %s", iRelativePath);
			populated = true;
		}
	}
	else
	{
		SYSLOG("Not populated: %s", iRelativePath);
	}

	return populated;
}

int Cache::populateDir(const char *iRelativeDir, const Metadata::EntryStat &iMdEntry)
{
	SYSLOG("%s", iRelativeDir);

	int rc = 0;

#ifndef MULTI_THREADED
	g_fileSystem.lock();
#endif

	if(isPopulated(iRelativeDir, iMdEntry))
	{
		SYSLOG("already populated %s", iRelativeDir);
	}
	else
	{
		RmtFs::Handle_t handle;
		RmtFs::DirEntry dirEntry;
		while(!(rc = RmtFs::nextDirEntry(handle, RmtDir(iRelativeDir), dirEntry)) && !dirEntry.noMoreEntries)
		{
			if(*dirEntry.name && strcmp(dirEntry.name, ".") && strcmp(dirEntry.name, ".."))
			{
				g_metadata.addMetadata(RelativePath(iRelativeDir, dirEntry.name).toString(),
								dirEntry.statInfo);
			}
		}

		if(!rc)
		{
			// Update the populate time in the metadata entry
			g_metadata.addMetadata(iRelativeDir, iMdEntry.statInfo, FileSystem::getRefreshStartTime());
		}
	}

#ifndef MULTI_THREADED
	g_fileSystem.unlock();
#endif

	return rc;
}

int Cache::cacheFile(const char *iRelativePath, struct stat iStatInfo)
{
	int rc = 0;

	// Make sure the directory path exists before creating file in cache.
	CacheDir cacheDir(iRelativePath);
	cacheDir.cdToParent();
	struct stat statInfo;
	rc = lstat(cacheDir.toString(), &statInfo);
	// Directory doesn't exist?
	if(rc)
	{
		// Make directory
		char *dir = cacheDir;
		char *endDir = dir;
		while((endDir = strchr(endDir+1, '/')))
		{
			*endDir = '\0';
			rc = mkdir(dir, S_IRWXU|S_IRWXG|S_IRWXO);
			// mkdir failed for unknown reason?
			if(rc && errno != EEXIST)
			{
				SYSLOG_ERROR("mkdir failed: errno=%d %s", errno, dir);
			}
			*endDir = '/';
		}
		rc = 0;
	}

	// Symbolic link?
	if(S_ISLNK(iStatInfo.st_mode))
	{
		// Copy only the symbolic path (-P flag)
		char path[PATH_MAX];
		rc = RmtFs::readlink(RmtDir(iRelativePath), path);

		if(rc)
		{
			unlink(CacheDir(iRelativePath));
			SYSLOG_ERROR("symbolic path error rc=%d %s", rc, CacheDir(iRelativePath).toString());
		}
		else
		{
			SYSLOG("cache symbolic link rc=%d %s -> %s", rc, CacheDir(iRelativePath).toString(), path);
			unlink(CacheDir(iRelativePath));
			rc = symlink(path, CacheDir(iRelativePath));
			if(rc)
			{
				unlink(CacheDir(iRelativePath)); // Delete the bad symlink
				SYSLOG_ERROR("symlink error rc=%d %s", errno, CacheDir(iRelativePath).toString());
			}
			else
			{
				// Set correct file modification times
				struct timeval tv[2];
				tv[0].tv_sec = iStatInfo.st_atim.tv_sec;
				tv[0].tv_usec = iStatInfo.st_atim.tv_nsec/1000;
				tv[1].tv_sec = iStatInfo.st_mtim.tv_sec;
				tv[1].tv_usec = iStatInfo.st_mtim.tv_nsec/1000;
				utimes(CacheDir(iRelativePath), tv);
			}
		}
	}
	else
	{
		/** @todo should lftp be used? faster download???? **/
		// FTP server configured and file size it faitly large?
		if(iStatInfo.st_size > MIN_LFTP_SIZE)
		{
			lftpGet(iRelativePath);
		}
		else
		{
			// Copy the contents of the netfs file to the cache
			FILE *pFile = fopen(CacheDir(iRelativePath),"w");
			if(pFile)
			{
				char buf[320000];
				RmtFs::Handle_t handle;
				size_t size = 0;
				while((rc = RmtFs::nextDataBlock(handle, RmtDir(iRelativePath), buf, sizeof(buf), size)) == 0 && size)
				{
					if(fwrite(buf, size, 1, pFile) != 1)
					{
						rc = errno;
						SYSLOG_ERROR("fwrite error %d %s", errno, CacheDir(iRelativePath).toString());
						break;
					}
				}
				fclose(pFile);
				// Error detected?
				if(rc)
				{
					// Delete the partially cached file
					unlink(CacheDir(iRelativePath));
				}
				else
				{
					SYSLOG("cached file content %s", CacheDir(iRelativePath).toString());
				}
			}
			else
			{
				SYSLOG_ERROR("fopen error %d %s", errno, CacheDir(iRelativePath).toString());
			}
		}

		if(!rc)
		{
			// The file code seems to lose the execute permission.
			chmod(CacheDir(iRelativePath), iStatInfo.st_mode|S_IWUSR);

			// Set correct file modification times
			struct timeval tv[2];
			tv[0].tv_sec = iStatInfo.st_atim.tv_sec;
			tv[0].tv_usec = iStatInfo.st_atim.tv_nsec/1000;
			tv[1].tv_sec = iStatInfo.st_mtim.tv_sec;
			tv[1].tv_usec = iStatInfo.st_mtim.tv_nsec/1000;
			utimes(CacheDir(iRelativePath), tv);
		}
	}

	return rc;
}

static std::vector<__ino_t> g_inodeVector;

int Cache::nftwRefreshCacheFunc(const char *iPath, const struct stat *iStat, int iTypeflag, struct FTW *iFtwbuf)
{
	//printf("%s %s\n", iTypeflag == FTW_F || iTypeflag == FTW_SL ? "file":"dir", iPath);
	if(iTypeflag == FTW_F || iTypeflag == FTW_SL)
	{
		char path[PATH_MAX];
		strcpy(path, iPath);
		*strrchr(path, '/') = '\0';

		struct stat statInfo;
		bzero(&statInfo, sizeof(statInfo));
		stat(path, &statInfo);

		for(uint i = 0; i < g_inodeVector.size(); ++i)
		{
			if(g_inodeVector[i] == statInfo.st_ino)
			{
				//printf("skip ino=%u %s\n", (int)statInfo.st_ino, path);
				return FTW_CONTINUE;
			}
		}

		//printf("mirror ino=%u %s\n", (int)statInfo.st_ino, path);

		g_inodeVector.push_back(statInfo.st_ino); // Remember this ino has been mirrored

		// Mirror this directory
		lftpMirror(path+strlen(g_config.cacheDir));
	}

	return FTW_CONTINUE;
}

// This function is recursively called.
int Cache::refreshCacheDir(char *iDir, char *iRelativePath)
{
	int rc = 0;

	rc = nftw(iDir, nftwRefreshCacheFunc, 20, FTW_PHYS);

	return rc;
}

int Cache::nftwCleanCacheFunc(const char *iPath, const struct stat *iStat, int iTypeflag, struct FTW *iFtwbuf)
{
	int rc = 0;
	//printf("%s %s\n", iTypeflag == FTW_F || iTypeflag == FTW_SL ? "file":"dir", iPath);
	if(iTypeflag == FTW_F || iTypeflag == FTW_SL)
	{
		char parentDir[PATH_MAX];
		strcpy(parentDir, iPath);
		*strrchr(parentDir, '/') = '\0';

		struct stat statInfo;
		bzero(&statInfo, sizeof(statInfo));
		stat(parentDir, &statInfo);

		//printf("mirror ino=%u %s\n", (int)statInfo.st_ino, parentDir);

		if(S_ISREG(iStat->st_mode))
		{
			bool modeChanged = false;
			// Directory is not writable?
			if(!(statInfo.st_mode & S_IWUSR))
			{
				chmod(parentDir, statInfo.st_mode|S_IWUSR);
				modeChanged = true;
			}

			rc = remove(iPath);
			if(rc)
			{
				printf("Error %d, fail to delete file %s", errno, iPath);
			}


			// Mode changed?
			if(modeChanged)
			{
				chmod(parentDir, statInfo.st_mode&~S_IWUSR); // change back to read only
			}
		}
	}

	return FTW_CONTINUE;
}

// This function is recursively called.
int Cache::cleanCacheDir(char *iDir, char *iRelativePath)
{
	int rc = 0;

	rc = nftw(iDir, nftwCleanCacheFunc, 20, FTW_PHYS);

	return rc;
}

int Cache::refreshIfStale(const char *iRelativePath, const struct stat *ipRmtStat)
{
	int rc = 0;
	struct stat netStat;
	struct stat cacheStat;
	CacheDir cachePath(iRelativePath);

	rc = lstat(cachePath, &cacheStat);
	if(rc == -1) {
		SYSLOG("lstat cache error %d", errno);
		return 0;
	}

	// Already did a refresh on this file?
	if(cacheStat.st_ctim.tv_sec > FileSystem::getRefreshStartTime())
	{
		SYSLOG("File already refreshed %s", cachePath.toString());
		return 0;
	}
	else
	{
		// Force update to st_ctim
		chmod(cachePath, cacheStat.st_mode|S_IWUSR);
	}

	if(!ipRmtStat)
	{
		rc = RmtFs::lstat(RmtDir(iRelativePath), netStat);
	}
	else
	{
		netStat = *ipRmtStat;
	}

	// File not found?
	if(rc)
	{
		if(rc == ENOENT)
		{
			SYSLOG("Delete file from cache %s", cachePath.toString());
			remove(cachePath);
		}
		else
		{
			SYSLOG("SFtp::lstat error %d", rc);
		}
		return rc;
	}

	// File has changed?
	if(netStat.st_size != cacheStat.st_size	||
		// @todo timestamps are off by 1 second - must be a rounding error????
		netStat.st_mtim.tv_sec > cacheStat.st_mtim.tv_sec+1
		)
	{
		SYSLOG("refresh required sizes=(%d %d) times=(%d %d) %s",
				(uint)netStat.st_size, (uint)cacheStat.st_size,
				(uint)netStat.st_mtim.tv_sec, (uint)cacheStat.st_mtim.tv_sec,
				iRelativePath);

		// Update cache
		cacheFile(iRelativePath, cacheStat); /** @todo what if file permissions have changed? **/
	}

	return rc;
}

void Cache::refreshCache()
{
	char dir[PATH_MAX];
	char relativePath[PATH_MAX] = "";
	strcpy(dir, g_config.cacheDir);
	refreshCacheDir(dir, relativePath);
}

void Cache::cleanCache()
{
	char dir[PATH_MAX];
	char relativePath[PATH_MAX] = "";
	strcpy(dir, CacheDir(""));
	cleanCacheDir(dir, relativePath);
}

int Cache::deleteCache()
{
	int rc = 0;
	char cmd[200];
	snprintf(cmd, sizeof(cmd), "/bin/rm -r %s", CacheDir("*").toString());
	rc = system(cmd);
	if(rc)
	{
		printf("%s\n", cmd);
		printf("Failed with error %d: %s\n", rc, strerror(rc));
	}
	return rc;
}

