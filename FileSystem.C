/*
 * FileSystem.C
 *
 *  Created on: Feb 28, 2011
 *      Author: christen
 */
#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include "FileSystem.H"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statfs.h>
#include <stdlib.h>
#include <limits.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <map>
#include <signal.h>
#include <execinfo.h>

#include "Types.H"
#include "Cache.H"
#include "RmtFs.H"
#include "Metadata.H"

using namespace std;

// Global data
FileSystem g_fileSystem;
Config g_config;
Metadata g_metadata;

FileSystem::FileSystem()
{
	pthread_mutex_init(&ivMutex, NULL);
	bzero(&g_config, sizeof(g_config));
	strcat(g_config.logFile,"/tmp/snapshotfs.log"); // Create temporary log file
	g_config.port = 22;
	setRefreshStartTime();
}

void FileSystem::setRefreshStartTime() {
	CacheDir refreshFile("", SNAPSHOTFS_REFRESH_START_TIME_FILE);
	SYSLOG("create %s", refreshFile.toString());
	FILE *file = fopen(refreshFile,"w");
	if(file)
	{
		fclose(file);
	}
	else
	{
		SYSLOG_ERROR("Error %d creating %s", errno, refreshFile.toString());
	}
}

time_t FileSystem::getRefreshStartTime() {
	struct stat statInfo;
	bzero(&statInfo,sizeof(statInfo));
	CacheDir refreshFile("", SNAPSHOTFS_REFRESH_START_TIME_FILE);
	int rc = lstat(refreshFile, &statInfo);
	if(rc)
	{
		SYSLOG_ERROR("Error %d on lstat %s", errno, refreshFile.toString());
		setRefreshStartTime(); // Create refresh start time file
		lstat(refreshFile, &statInfo);
	}
	return statInfo.st_ctim.tv_sec;
}

void FileSystem::lock()
{
	pthread_mutex_lock(&ivMutex);
}

void FileSystem::unlock()
{
	pthread_mutex_unlock(&ivMutex);
}

std::string FileSystem::fuse_conn_info_toString(const fuse_conn_info &irCi) {
	char buf[1024];
	sprintf(buf,
			"proto_major=%d proto_minor=%d async_read=%d max_write=%d max_readahead=%d capable=%08x want=%08x",
			irCi.proto_major, irCi.proto_minor,
			irCi.async_read, irCi.max_write, irCi.max_readahead, irCi.capable, irCi.want
	);

	return buf;
}

static bool g_handling_signal = false;
void FileSystem::handler(int iSigNum)
{
	SYSLOG_ERROR("iSigNum=%d", iSigNum);

	if (g_handling_signal)
	{
	   raise(iSigNum);
	}
	g_handling_signal = true;

	int nptrs;
	#define BUF_SIZE 100
	void *buffer[BUF_SIZE];
	char **strings;

	nptrs = backtrace(buffer, BUF_SIZE);
	SYSLOG_ERROR("backtrace() returned %d addresses", nptrs);

	/* The call backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)
	  would produce similar output to the following: */

	strings = backtrace_symbols(buffer, nptrs);
	if (strings == NULL) {
	   SYSLOG_ERROR("backtrace_symbols() returned nothing %s", "");
	}

	for (int32_t i = 0; i < nptrs; i++)
	   SYSLOG_ERROR("%s", strings[i]);
	free(strings);

	// Terminate the program
	signal(iSigNum, SIG_DFL);
	raise(iSigNum);
}

void* FileSystem::Init(fuse_conn_info *ipFuseConnInfo)
{
	SYSLOG("%s", fuse_conn_info_toString(*ipFuseConnInfo).c_str());

	// Setup signal handler for segmentation faults
	if(signal(SIGSEGV, FileSystem::handler) == SIG_ERR)
	{
		SYSLOG_ERROR("signal SIGSEGV failed errno=%d", errno);
	}

	// Setup signal handler for segmentation faults
	if(signal(SIGABRT, FileSystem::handler) == SIG_ERR)
	{
		SYSLOG_ERROR("signal SIGABRT failed errno=%d", errno);
	}

	// Force abnormal problem termination to test signal handler
//	SYSLOG_ERROR("Force a segmentation fault%s", "");
//	int *p = NULL;
//	*p = 0;

	return NULL; // No private_data field
}

int FileSystem::Ioctl(const char *iRelativePath, int iCmd, void *iArg, struct fuse_file_info *, unsigned int iFlags, void *iData)
{
	SYSLOG("%s", iRelativePath);
	return 0;
}

int FileSystem::Getattr(const char *iRelativePath, struct stat *ipStatInfo)
{
    SYSLOG("%s", iRelativePath);
    int rc = 0;
    bzero(ipStatInfo, sizeof(*ipStatInfo));

    do {
    	errno = 0;
    	Metadata::EntryStat mdEntry;
    	if(g_metadata.findMetadata(iRelativePath,  mdEntry))
    	{
    		*ipStatInfo = mdEntry.statInfo;

    		// Negative cache entry?
    		if(mdEntry.negative)
    		{
    			errno = ENOENT;
    			SYSLOG("Negative cache entry errno=%d %s", errno, iRelativePath);
    			rc = -errno;
    			break;
    		}
    	}
    	else
    	{
    		if(g_config.offline)
    		{
    			errno = ENOENT;
				SYSLOG_ERROR("Cache is offline errno=%d %s", errno, iRelativePath);
				rc = -errno;
    			break;
    		}
    		else
    		{
    			// See of directory/file is cached?
    			rc = lstat(CacheDir(iRelativePath), ipStatInfo);
    			if(rc)
    			{
    				rc = RmtFs::lstat(RmtDir(iRelativePath), *ipStatInfo);
    			}
    			if(rc)
    			{
    				// Add negative cache entry
    				g_metadata.addMetadata(iRelativePath, *ipStatInfo, 0, Metadata::NEGATIVE);
    				errno = ENOENT;
    				SYSLOG_ERROR("lstat failed errno=%d %s", errno, iRelativePath);
    				rc = -errno;
    				break;
    			}

    			// Add new metadata entry
				g_metadata.addMetadata(iRelativePath, *ipStatInfo);
    		}
    	}

    	// Directory?
		if(S_ISDIR(ipStatInfo->st_mode))
		{
			if(mkdir(CacheDir(iRelativePath), ipStatInfo->st_mode|S_IWUSR))
			{
				SYSLOG("mkdir failed errno=%d %s", errno, iRelativePath);
			}
		}
	} while(0);

    return rc;
}

int FileSystem::Readlink(const char *iRelativePath, char *opBuf, size_t iBufSize)
{
    int rc = 0;
    opBuf[0] = '\0';
    SYSLOG("%s", iRelativePath);

    do {
    	rc = readlink(CacheDir(iRelativePath), opBuf, iBufSize - 1);
		if(rc == -1) {
			// Offline?
			if(g_config.offline)
			{
				rc = -errno;
				SYSLOG_ERROR("lstat cache if offline errno %d %s", errno, CacheDir(iRelativePath).toString());
				break;
			}

			Metadata::EntryStat mdEntry;
			if(!g_metadata.findMetadata(iRelativePath, mdEntry) || mdEntry.negative) {
				errno = ENOENT;
				SYSLOG_ERROR("lstat errno %d %s", errno, iRelativePath);
				rc = -errno;
				break;
			}

			struct stat statInfo;
			rc = RmtFs::lstat(RmtDir(iRelativePath), statInfo);
			if(rc)
			{
				SYSLOG_ERROR("lstat errno %d %s", rc, RmtDir(iRelativePath).toString());
				rc = -rc;
				break;
			}

			rc = Cache::cacheFile(iRelativePath, statInfo);
			if(rc)
			{
				SYSLOG_ERROR("cacheFile errno %d %s", rc, iRelativePath);
				rc = -rc;
				break;
			}

			rc = readlink(CacheDir(iRelativePath), opBuf, iBufSize - 1);
			if(rc == -1)
			{
				rc = errno;
				SYSLOG_ERROR("readlink errno %d %s", rc, CacheDir(iRelativePath).toString());
				rc = -rc;
				break;
			}
		}
		else
		{
			// Metadata is out of sync with cache?
			if(!g_metadata.metadataExists(iRelativePath))
			{
				struct stat statInfo;
				lstat(iRelativePath, &statInfo);
				g_metadata.addMetadata(iRelativePath, statInfo);
			}
			/** @todo add refreshLinks flag???? **/
			if(g_config.refreshOpenedFiles)
			{
				Cache::refreshIfStale(iRelativePath);
			}
		}

		opBuf[rc] = '\0';
		rc = 0;

    } while(0);

    SYSLOG("return %s", opBuf);

    return rc;
}


int FileSystem::Getdir(const char *iRelativePath, fuse_dirh_t iHandle, fuse_dirfil_t iDirFillerFunc)
{
    int rc = 0;
    SYSLOG("%s", iRelativePath);

    /** Should be ok to just check metadata, since getattr is always called first
     *  and will create the directory in the cache.  **/
    Metadata::EntryStat mdEntry;
	if(!g_metadata.findMetadata(iRelativePath, mdEntry) || mdEntry.negative) {
		errno = ENOENT;
		SYSLOG_ERROR("errno %d %s", errno, iRelativePath);
		rc = -errno;
	}
	else
	{
		if(!g_config.offline)
		{
			// Populate directory entries, if required
			Cache::populateDir(iRelativePath, mdEntry);
		}

		vector<Metadata::EntryStat> children;
		g_metadata.findMetadataChildren(iRelativePath, children);
		for(uint32_t i = 0; i < children.size(); ++i) {
			mode_t mode = children[i].statInfo.st_mode;
			int dtype;
			if(S_ISDIR(mode)) dtype = DT_DIR;
			else if(S_ISCHR(mode)) dtype = DT_CHR;
			else if(S_ISBLK(mode)) dtype = DT_BLK;
			else if(S_ISREG(mode)) dtype = DT_REG;
			else if(S_ISFIFO(mode)) dtype = DT_FIFO;
			else if(S_ISLNK(mode)) dtype = DT_LNK;
			else dtype = DT_UNKNOWN;
			rc = iDirFillerFunc(iHandle, children[i].name, dtype, 0);
		}
	}

    if(rc)
    {
    	SYSLOG_ERROR("return error %d %s", rc, iRelativePath);
    }

    return rc?-rc:0;
}

int FileSystem::Mknod(const char *iRelativePath, mode_t iMode, dev_t iDevice)
{
    SYSLOG("%s", iRelativePath);

    /** @todo add support for non-regular files **/

    if(!S_ISREG(iMode) && !S_ISFIFO(iMode))
    {
    	SYSLOG_ERROR("Unsupported file mode %d %s", iMode, iRelativePath);
    	return -EPERM;
    }

    if(g_config.readonly)
    {
    	SYSLOG_ERROR("Read only file system errno %d %s", EROFS, iRelativePath);
    	return -EROFS;
    }

    int rc = RmtFs::mknod(RmtDir(iRelativePath), iMode);
    rc = 0;
//	if(rc) {
//		SYSLOG_ERROR("netfs errno %d", rc);
//		return -rc;
//	}

    return rc;
}

int FileSystem::Mkdir(const char *iRelativePath, mode_t iMode)
{
    int rc;
    SYSLOG("%s", iRelativePath);

    if(g_config.readonly)
	{
		SYSLOG_ERROR("Read only file system errno %d %s", EROFS, iRelativePath);
		return -EROFS;
	}

	rc = RmtFs::mkdir(RmtDir(iRelativePath), iMode);
	if(rc) {
		SYSLOG_ERROR("netfs error %d %s", errno, iRelativePath);
		return -errno;
	}

    return 0;
}

int FileSystem::Unlink(const char *iRelativePath)
{
    int rc = 0;
    SYSLOG("%s", iRelativePath);

    if(g_config.readonly)
	{
		SYSLOG_ERROR("Read only file system errno %d %s", EROFS, iRelativePath);
		return -EROFS;
	}

    // Remove metadata
    g_metadata.removeMetadata(iRelativePath);

	// Remove cache file, if it exists
	unlink(CacheDir(iRelativePath));

	rc = RmtFs::unlink(RmtDir(iRelativePath));
	if(rc) {
		SYSLOG_ERROR("netfs error %d %s", rc, RmtDir(iRelativePath).toString());
		return -rc;
	}

	return 0;
}

int FileSystem::Rmdir(const char *iRelativePath)
{
    int rc;
    SYSLOG("%s", iRelativePath);

    if(g_config.readonly)
	{
		SYSLOG_ERROR("Read only file system errno %d %s", EROFS, iRelativePath);
		return -EROFS;
	}

	rc = RmtFs::rmdir(RmtDir(iRelativePath));
	if(rc) {
		SYSLOG_ERROR("netfs error %d %s", rc, RmtDir(iRelativePath).toString());
		rc = -rc;
	}

    // Remote the cached directory
    char cmd[PATH_MAX];
	snprintf(cmd, sizeof(cmd), "rm -rf %s",CacheDir(iRelativePath).toString());
	int rmRc = 0;
	if((rmRc = system(cmd)))
	{
		SYSLOG_ERROR("cache rm -rf error %d %s", rmRc, CacheDir(iRelativePath).toString());
	}

    return rc;
}

int FileSystem::Symlink(const char *iFromPath, const char *iToPath)
{
    int rc;
    SYSLOG("%s->%s", iFromPath, iToPath);

    if(g_config.readonly)
	{
		SYSLOG_ERROR("Read only file system errno %d from %s to %s", EROFS, iFromPath, iToPath);
		return -EROFS;
	}

    rc = RmtFs::symlink(iFromPath, RmtDir(iToPath));
    if(rc) {
    	SYSLOG_ERROR("netfs error %d %s", rc, RmtDir(iToPath).toString());
    	return -rc;
    }

    return 0;
}

int FileSystem::Rename(const char *iFromPath, const char *iToPath)
{
    SYSLOG("%s->%s", iFromPath, iToPath);

//    if(g_config.readonly)
//	{
//		SYSLOG_ERROR("Read only file system errno %d %s %s", EROFS, iFromPath, iToPath);
//		return -EROFS;
//	}
//
//	rc = RmtFs::rename(RmtDir(iFromPath), RmtDir(iToPath));
//	if(rc) {
//		SYSLOG_ERROR("netfs error %d %s %s", rc, RmtDir(iFromPath).toString(), RmtDir(iToPath).toString());
//		return -rc;
//	}
//
//    // Rename stat or cached file, whichever exists
//    rename(StatFile(iFromPath), StatFile(iToPath));
//    rename(CacheDir(iFromPath), CacheDir(iToPath));

    return -EROFS;
}

int FileSystem::Link(const char *iFromPath, const char *iToPath)
{
    SYSLOG("%s->%s", iFromPath, iToPath);

    int rc = 0;

    if(g_config.readonly)
	{
		SYSLOG_ERROR("Read only file system errno %d %s %s", EROFS, iFromPath, iToPath);
		return -EROFS;
	}

	char cmd[PATH_MAX];
	snprintf(cmd, sizeof(cmd), "ln %s %s", RmtDir(iFromPath).toString(), RmtDir(iToPath).toString());
	rc = RmtFs::runCmd(cmd);
	if(rc) {
		SYSLOG_ERROR("netfs error %d %s %s", rc, iFromPath, iToPath);
		rc = -rc;
	}

    /** @todo how is hard link represneted in cache ??? **/
//    if(!rc)
//    {
//		rc = link(CacheDir(iFromPath), CacheDir(iToPath));
//		if(rc == -1) {
//			rc = -errno;
//			SYSLOG_ERROR("cache error %d", errno);
//		}
//    }

    return rc;
}

int FileSystem::Chmod(const char *iRelativePath, mode_t iMode)
{
    SYSLOG("%s", iRelativePath);

    int rc = 0;

    if(g_config.readonly)
	{
		SYSLOG_ERROR("Read only file system errno %d %s", EROFS, iRelativePath);
		return -EROFS;
	}

	rc = RmtFs::chmod(RmtDir(iRelativePath), iMode);
	if(rc) {
		SYSLOG_ERROR("netfs error %d %s", rc, RmtDir(iRelativePath).toString());
		return -rc;
	}

	// @todo???
    chmod(CacheDir(iRelativePath), iMode|S_IWUSR);

    return 0;
}

int FileSystem::Chown(const char *iRelativePath, uid_t iUid, gid_t iGid)
{
    SYSLOG("%s", iRelativePath);

    if(g_config.readonly)
   	{
   		SYSLOG_ERROR("Read only file system errno %d %s", EROFS, iRelativePath);
   		return -EACCES;
   	}

	int rc = RmtFs::chown(RmtDir(iRelativePath), iUid, iGid);
	if(rc) {
		SYSLOG_ERROR("netfs error %d %s", errno, RmtDir(iRelativePath).toString());
		return -rc;
	}

    /** @todo should owner on cached file be changed??? **/

    return 0;
}

int FileSystem::Truncate(const char *iRelativePath, off_t iSize)
{
    SYSLOG("size=%d %s", (int)iSize, iRelativePath);

    if(g_config.readonly)
   	{
   		SYSLOG_ERROR("Read only file system errno %d %s", EROFS, iRelativePath);
   		return -EROFS;
   	}

    if(iSize > 0)
    {
    	SYSLOG("Non zero size not supported size= %d %s", (int)iSize, iRelativePath);
    	return -EPERM;
    }

    int rc = 0;

	rc = RmtFs::truncate(RmtDir(iRelativePath));
	if(rc) {
		SYSLOG_ERROR("netfs error %d %s", rc, RmtDir(iRelativePath).toString());
		return -rc;
	}

	// @todo ???
    rc = truncate(CacheDir(iRelativePath), iSize);
	if(rc == -1) {
		SYSLOG_ERROR("cache error %d %s", errno, CacheDir(iRelativePath).toString());
		return -errno;
	}

    return 0;
}

int FileSystem::Utime(const char *iRelativePath, struct utimbuf *opUtimBuf)
{
    int rc;
    SYSLOG("%s", iRelativePath);

    // @todo ???
    rc = utime(CacheDir(iRelativePath), opUtimBuf);
    if(rc == -1) {
    	SYSLOG_ERROR("errno %d %s", errno, CacheDir(iRelativePath).toString());
    	return -errno;
    }

    return 0;
}

int FileSystem::Open(const char *iRelativePath, fuse_file_info *ipFileInfo)
{
    int rc;
    int fd = -1;
    SYSLOG("%s", iRelativePath);

    rc = open(CacheDir(iRelativePath), ipFileInfo->flags);
    if(rc == -1)
    {
    	SYSLOG("cache open error %d",errno);
		struct stat statInfo;
		rc = Getattr(iRelativePath, &statInfo);
		if(rc == -1)
		{
			SYSLOG_ERROR("getattr errno %d %s", errno, iRelativePath);
			return -errno;
		}

		rc = Cache::cacheFile(iRelativePath, statInfo);
		if(rc)
		{
			SYSLOG_ERROR("cacheContents errno %d %s", rc, iRelativePath);
			return -rc;
		}

		 rc = open(CacheDir(iRelativePath), ipFileInfo->flags);
		 if(rc == -1)
		 {
			 SYSLOG_ERROR("open errno %d %s", errno, CacheDir(iRelativePath).toString());
			 return -errno;
		 }
		 fd = rc;
    }
    else
    {
    	fd = rc;
    	// Refresh opened files?
    	if(g_config.refreshOpenedFiles)
    	{
    		close(fd);
    		Cache::refreshIfStale(iRelativePath);
    		rc = open(CacheDir(iRelativePath), ipFileInfo->flags);
    		if(rc == -1)
    		{
    			SYSLOG_ERROR("open errno %d %s", errno, CacheDir(iRelativePath).toString());
    			return -errno;
    		}
    		fd = rc;
    	}
    }

    ipFileInfo->fh = FileHandle::set(new FileHandle(fd, FileHandle::TypeUnknow));

    SYSLOG("fd=%d", fd);
    //close(rc);

    return 0;
}

int FileSystem::Read(const char *iRelativePath, char *opBuf, size_t iBufSize, off_t iOffset, fuse_file_info *ipFileInfo)
{
    int rc = 0;
    SYSLOG("iBufSize=%d iOffset=%d %s", (uint)iBufSize, (uint)iOffset, iRelativePath);

    SYSLOG("fd=%d", FileHandle::get(ipFileInfo->fh)->getFd());

    rc = pread(FileHandle::get(ipFileInfo->fh)->getFd(), opBuf, iBufSize, iOffset);
    if(rc == -1) {
    	SYSLOG_ERROR("pread error %d %s", errno, iRelativePath);
    	rc = -errno;
    }

    return rc;
}

int FileSystem::Write(const char *iRelativePath, const char *ipBuf, size_t iBufSize, off_t iOffset, fuse_file_info *ipFileInfo)
{
    SYSLOG("iBufSize=%d iOffset=%d %s", (int)iBufSize, (int)iOffset, iRelativePath);

    int rc = 0;

    if(g_config.readonly)
	{
		SYSLOG_ERROR("Read only file system: %s", iRelativePath);
		return -EROFS;
	}

    SYSLOG("fd=%d", FileHandle::get(ipFileInfo->fh)->getFd());

    FileHandle::get(ipFileInfo->fh)->setType(FileHandle::TypeWrite);

	rc = pwrite(FileHandle::get(ipFileInfo->fh)->getFd(), ipBuf, iBufSize, iOffset);
	if(rc == -1) {
		SYSLOG_ERROR("pwrite error %d %s", errno, iRelativePath);
		return -errno;
	}

    return iBufSize;
}

enum {
	MAX_WRITE_SIZE = 1024*512
};

int FileSystem::Release(const char *iRelativePath, fuse_file_info *ipFileInfo)
{
	int fd = FileHandle::get(ipFileInfo->fh)->getFd();
	SYSLOG("fd=%d %s", fd, iRelativePath);
	int rc = 0;

	close(fd);

    if(FileHandle::get(ipFileInfo->fh)->getType() == FileHandle::TypeWrite)
    {
    	RmtFs::Handle_t handle;
    	struct stat statInfo;
    	lstat(CacheDir(iRelativePath), &statInfo);
    	uint32_t bufSize = (statInfo.st_size > MAX_WRITE_SIZE ? MAX_WRITE_SIZE : statInfo.st_size);
    	char *buf = new char[bufSize];
    	FILE *pFile = fopen(CacheDir(iRelativePath), "r");
    	if(pFile)
    	{
			uint32_t len = 0;
			for(;;) {
				rc = fread(buf, 1, bufSize, pFile);
				if(rc == -1)
				{
					rc = -errno;
					SYSLOG_ERROR("read error %d %s", errno, CacheDir(iRelativePath).toString());
					break;
				}
				else if(!rc)
				{
					break;
				}

				len = rc;

				rc = RmtFs::write(handle, RmtDir(iRelativePath), buf, len);
				if(rc) {
					SYSLOG_ERROR("pwrite error %d %s", rc, CacheDir(iRelativePath).toString());
					rc = -rc;
					break;
				}
			}
			fclose(pFile);
    	}
    	else
    	{
    		rc = -errno;
    		SYSLOG_ERROR("pwrite error %d %s", errno, CacheDir(iRelativePath).toString());
    	}
    	delete [] buf;
    }

    delete FileHandle::get(ipFileInfo->fh);

    return rc;
}

int FileSystem::Statfs(const char *iRelativePath, struct statvfs *ipStatvFs)
{
	SYSLOG("%s", iRelativePath);
    struct statvfs st;

    int rv = statvfs(g_config.cacheDir,&st);
    if(!rv) {
    	*ipStatvFs = st;
    }
    return rv;
}

int FileSystem::Fsync(const char *iRelativePath, int isdatasync, fuse_file_info *ipFileInfo)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */

    (void) iRelativePath;
    (void) isdatasync;
    (void) ipFileInfo;
    return 0;
}
