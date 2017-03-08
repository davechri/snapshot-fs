/*
 * RmtFs.H
 *
 *  Created on: Oct 7, 2011
 *      Author: christen
 */

#ifndef RMTFS_H_
#define RMTFS_H_

#include "Types.H"

#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <string>
#include <netinet/in.h>
#include <sys/stat.h>
#include <vector>
#include <limits.h>
#include <deque>
#include <pthread.h>
#include <string.h>
#include <stdint.h>

// Wrapper macro to call a RmtFs function:
#define RMTFS_CALL_RETURN(FUNC) \
	RmtFs *pRmtFs; \
	int rc = getConnection(pRmtFs); \
	if(!rc) \
	{ \
		rc = pRmtFs->FUNC; \
		freeConnection(pRmtFs); \
	} \
	return rc;


class RmtFs {
public:
	/**
	 * @brief Test connectivity and authentication to the remote host.
	 * @return 0 on success, or errno
	 */
	static int test()
	{
		RMTFS_CALL_RETURN(noop());
	}

	/**
	 * @brief Get file status, but do not follow symbolic links.
	 * @param iPath		File path.
	 * @param oStatInfo File status.
	 * @return 0 on success, or errno
	 */
	static int lstat(const char *iPath, struct stat &oStatInfo)
	{
		RMTFS_CALL_RETURN(sftpLstat(iPath, oStatInfo));
	}

	/**
	 * @brief Read the value of a symbolic link.
	 * @param iPath1	Symbolic link path.
	 * @param oPath2	Value of symbolic link.
	 * @return 0 on success, or errno * @brief Remove a directory.
	 * @param iDir		Directory path.
	 * @return 0 on success, or errno
	 */
	static int readlink(const char *iPath1, char oPath2[PATH_MAX])
	{
		RMTFS_CALL_RETURN(sftpReadlink(iPath1, oPath2));
	}

	/**
	 * @brief Make a directory.
	 * @param iDir		Directory path.
	 * @param iMode		Permissions.
	 * @return 0 on success, or errno
	 */
	static int mkdir(const char *iDir, mode_t iMode)
	{
		RMTFS_CALL_RETURN(sftpMkdir(iDir, iMode));
	}

	/**
	 * @brief Make a special or regular file
	 * @param iPath		File path.
	 * @param iMode		Permissions.
	 * @return 0 on success, or errno.
	 */
	static int mknod(const char *iPath, mode_t iMode)
	{
		RMTFS_CALL_RETURN(sftpMknod(iPath, iMode));
	}


	typedef struct Handle
	{
		Handle() : sftpDir(NULL), sftpFile(NULL), pRmtFs(NULL) {}
		~Handle() {
			if(sftpDir)
			{
				sftp_closedir(sftpDir);
				sftpDir = NULL;
			}
			if(sftpFile)
			{
				sftp_close(sftpFile);
				sftpFile = NULL;
			}
			if(pRmtFs)
			{
				freeConnection(pRmtFs);
				pRmtFs = NULL;
			}
		}
		// Data
		sftp_dir 	sftpDir;
		sftp_file 	sftpFile;
		RmtFs 		*pRmtFs;
	} Handle_t;

	typedef struct DirEntry
	{
		DirEntry() {bzero(this, sizeof(*this));}
		bool noMoreEntries;
		char name[NAME_MAX];
		struct stat statInfo;
	} DirEntry_t;

	/**
	 * @brief Get the next directory entry or the specified directory.
	 * @param ioHandle handle
	 * @param iDir		Directory path.
	 * @param oDirEntry Directory entry.
	 * @return 0 on success, or errno
	 */
	static int nextDirEntry(Handle_t &ioHandle, const char *iDir, DirEntry &oDirEntry);

	/**
	 * @brief Get the next block of data from a file.
	 * @param ioHandle handle
	 * @param iFile			File path.
	 * @param iopBuf		Buffer to hold data.
	 * @param iBufSize		Size of buffer.
	 * @param oDataSize		Size of returned data.
	 * @return 0 on success, or errno
	 */
	static size_t nextDataBlock(Handle_t &ioHandle, const char *iFile, char *iopBuf, size_t iBufSize, size_t &oDataSize);

	/**
	 * @brief Write to current file position
	 * @param ioHandle 		handle
	 * @param iFile			File path.
	 * @param iBuf			Buffer.
	 * @param iBufSize		Size of buffer.
	 * @return 0 on success, or errno
	 */
	static int write(Handle_t &ioHandle, const char *iFile, char *iBuf, size_t iBufSize);

	/**
	 * @brief Delete a name and possible the file it refers to.
	 * @param iFile			File path.
	 * @return 0 on success, or errno
	 */
	static int unlink(const char *iPath)
	{
		RMTFS_CALL_RETURN(sftpUnlink(iPath));
	}

	/**
	 * @brief Create a symbolic link.
	 * @param iTarget	Target of symbolic link.
	 * @param iDest		Name of the symbolic link.
	 * @return 0 on success, or errno
	 */
	static int symlink(const char *iTarget, const char *iDest)
	{
		RMTFS_CALL_RETURN(sftpSymlink(iTarget, iDest));
	}

	/**
	 * @brief Rename file.
	 * @param iOldName	Old file name.
	 * @param iNewName	New file name.
	 * @return 0 on success, or errno
	 */
	static int rename(const char *iOldName, const char *iNewName)
	{
		RMTFS_CALL_RETURN(sftpRename(iOldName, iNewName));
	}

	/**
	 * @brief Change file permissions.
	 * @param iPath		File path.
	 * @param iMode		File permissions.
	 * @return 0 on success, or errno
	 */
	static int chmod(const char *iPath, mode_t iMode)
	{
		RMTFS_CALL_RETURN(sftpChmod(iPath, iMode));
	}

	/**
	 * @brief Change uid and gid.
	 * @param iPath		File path.
	 * @param iUid		User id.
	 * @param iGid		Group id.
	 * @return 0 on success, or errno
	 */
	static int chown(const char *iPath, uid_t iUid, gid_t iGid)
	{
		RMTFS_CALL_RETURN(sftpChown(iPath, iUid, iGid));
	}

	/**
	 * @brief Truncate file to zero bytes.
	 * @param iPath		File path.
	 * @return 0 on success, or errno
	 */
	static int truncate(const char *iPath)
	{
		RMTFS_CALL_RETURN(sftpTruncate(iPath));
	}

	/**
	 * @brief Remove a directory.
	 * @param iDir		Directory path.
	 * @return 0 on success, or errno
	 */
	static int rmdir(const char *iDir)
	{
		RMTFS_CALL_RETURN(sftpRmdir(iDir));
	}

	/**
	 * @brief Execute a remote command.
	 * @param iCmd		Command.
	 * @return 0 on success, or errno
	 */
	static int runCmd(const char *iCmd)
	{
		RMTFS_CALL_RETURN(sshRunCmd(iCmd));
	}

private:
	enum {
			TIMEOUT_SECS 				= 30,
			KEEPALIVE_IDLE 				= 120, 	// 2 minutes
			KEEPALIVE_COUNT 			= 5, 	// send 5 probes before ending connection
			KEEPALIVE_INTERVAL 			= 5, 	// 5 seconds between probes
			MAX_IDLE_CONNECTION_COUNT	= 5
	};
	static int getConnection(RmtFs *&opRmtFs);
	static void freeConnection(RmtFs *ipRmtFs);
	bool isConnected() const;

	RmtFs();
	~RmtFs();

	int noop() {return 0;}
	int connect(const char *iHost);
	int login(const char *iUser, const char *iPassword);
	int sftpLstat(const char *iPath, struct stat &oStatInfo);
	int sftpReadlink(const char *iPath1, char *oPath2);
	int sftpMkdir(const char *iDir, mode_t iMode);
	int sftpMknod(const char *iPath, mode_t iMode);
	int sftpNextDirEntry(Handle_t &ioHandle, const char *iDir, DirEntry &oDirEntry);
	int sftpNextDataBlock(Handle_t &ioHandle, const char *iFile, char *iopBuf, size_t iBufSize, size_t &oDataSize);
	int sftpWrite(Handle_t &ioHandle, const char *iFile, const char *iBuf, size_t iBufSize);
	int sftpUnlink(const char *iPath);
	int sftpSymlink(const char *iTarget, const char *iDest);
	int sftpRename(const char *iOldName, const char *iNewName);
	int sftpChmod(const char *iPath, mode_t iMode);
	int sftpChown(const char *iPath, uid_t iUid, gid_t iGid);
	int sftpTruncate(const char *iPath);
	int sftpRmdir(const char *iDir);

	int sshRunCmd(const char *iCmd);

	std::string toString() const;
	void convertStat(const sftp_attributes iSFtpAttr, struct stat &oStatInfo);
	static int convertSFtpRc(int iRc);
	int getErrno() const;

	static void lock();
	static void unlock();

	ssh_session ivpSsh;
	sftp_session ivpSFtp;

	enum {
		MAX_IDLE_TIME = 60*60 // 1 hour
	};
	time_t ivLastUseTime;

	static std::deque<RmtFs*> cvConnectionList;
	static uint32_t cvConnectionCount;
};

#endif /* RMTFS_H_ */
