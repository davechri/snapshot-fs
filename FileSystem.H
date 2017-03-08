/*
 * FileSystem.H
 *
 *  Created on: Oct 7, 2011
 *      Author: christen
 */

#ifndef FILESYSTEM_H_
#define FILESYSTEM_H_

// Use the most recent fuse version
#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdint.h>
#include <pthread.h>
#include <string>

class FileSystem {
public:
	enum
	{
		MAX_WRITE_SIZE = 512*1024, // fuse max_write argument (maximum write block size)
		WRITE_BUF_SIZE = 512*1024 // Must be at least as big as MAX_WRITE_SIZE
	};

	/**
	 * @brief Constructor.
	 */
	FileSystem();

	/**
	 * @brief Lock mutex.
	 */
	void lock();

	/**
	 * @brief Unlock mutex.
	 */
	void unlock();

	/**
	 * @brief Init file system.
	 * @param ipFuseConnInfo 	FUSE connection info
	 * @return Pointer to private_data field
	 */
	static void* Init(fuse_conn_info *ipFuseConnInfo);

	/**
	 * @brief Init file system.
	 * @return 0 on success or negative errno.
	 */
	static int Ioctl(const char *, int iCmd, void *iArg, struct fuse_file_info *, unsigned int iFlags, void *iData);

	/**
	 * @brief Get file status.
	 * @param iRelativePath 	Relative file or directory path.
	 * @param iStatInfo			File status.
	 * @return 0 on success or negative errno.
	 */
	static int Getattr(const char *iRelativePath, struct stat *oStatInfo);

	/**
	 * @brief Read value of symbolic link.
	 * @param iRelativePath 	Relative symbolic link path.
	 * @param ioBuf				Contents of symbolic link.
	 * @param iBufSize			Size of buffer.
	 * @return 0 on success or negative errno.
	 */
	static int Readlink(const char *iRelativePath, char *ioBuf, size_t iBufSize);

	/**
	 * @brief Get directory list.
	 * @param iRelativePath 	Relative directory path.
	 * @param notUsed			Deprecated.
	 * @param iDirFillerFunc	Fuse function called for each directory entry.
	 * @return 0 on success or negative errno.
	 */
	static int Getdir(const char *iRelativePath, fuse_dirh_t notUsed, fuse_dirfil_t iDirFillerFunc);

	/**
	 * @brief Make a special or ordinary file.
	 * @param iRelativePath 	Relative file path.
	 * @param iMode				File type and permissions.
	 * @param iDevice			Device used for special files.
	 * @return 0 on success or negative errno.
	 */
	static int Mknod(const char *iRelativePath, mode_t iMode, dev_t iDevice);

	/**
	 * @brief Make a directory.
	 * @param iRelativePath 	Relative directory path.
	 * @param iMode				Directory permissions.
	 * @return 0 on success or negative errno.
	 */
	static int Mkdir(const char *iRelativePath, mode_t iMode);

	/**
	 * @brief Delete a name and possible the file it refers to.
	 * @param iRelativePath 	Relative file path.
	 * @return 0 on success or negative errno.
	 */
	static int Unlink(const char *path);

	/**
	 * @brief Delete a directory.
	 * @param iRelativePath 	Relative directory path.
	 * @return 0 on success or negative errno.
	 */
	static int Rmdir(const char *iRelativePath);

	/**
	 * @brief Make symbolic link to lfile.
	 * @param iTarget			Target file name or path.
	 * @param iSymLink			Symbolic link name.
	 * @return 0 on success or negative errno.
	 */
	static int Symlink(const char *iTarget, const char *iSymLink);

	/**
	 * @brief Rename a file or directory.
	 * @param iOldPath		 	Old path.
	 * @param iNewPath			New path.
	 * @return 0 on success or negative errno.
	 */
	static int Rename(const char *iOldPath, const char *iNewPath);

	/**
	 * @brief Make a hard link.
	 * @param iExistingPath		Existing file path.
	 * @param iNewPath			New file path.
	 * @return 0 on success or negative errno.
	 */
	static int Link(const char *iExistingPath, const char *iNewPath);

	/**
	 * @brief Change file access permissions.
	 * @param iRelativePath		Relative file path.
	 * @param iMode				Permissions.
	 * @return 0 on success or negative errno.
	 */
	static int Chmod(const char *iRelativePath, mode_t iMode);

	/**
	 * @brief Change file owner and group.
	 * @param iRelativePath		Relative file path.
	 * @param iUid				New user id.
	 * @param iGid				New group id.
	 * @return 0 on success or negative errno.
	 */
	static int Chown(const char *iRelativePath, uid_t iUid, gid_t iGid);

	/**
	 * @brief Truncate a file to the specified length.
	 * @param iRelativePath		Relative file path.
	 * @param iSize				New file size.
	 * @return 0 on success or negative errno.
	 */
	static int Truncate(const char *iRelativePath, off_t iSize);

	/**
	 * @brief Change access and/or modification times on a file.
	 * @param iRelativePath		Relative file path.
	 * @param ipUtimBuf			New access and/or modification time.
	 * @return 0 on success or negative errno.
	 */
	static int Utime(const char *iRelativePath, struct utimbuf *ipUtimBuf);

	/**
	 * @brief Open a file.
	 * @param iRelativePath		Relative file path.
	 * @param iFileInfo			File info.
	 * @return 0 on success or negative errno.
	 */
	static int Open(const char *iRelativePath, fuse_file_info *iFileInfo);

	/**
	 * @brief Read data from a file at the specified offset.
	 * @param iRelativePath		Relative file path.
	 * @param ioBuf				Buffer to receive data.
	 * @param iBufSize			Buffer size.
	 * @param iOffset			Offset into file to start reading data.
	 * @param iFileInfo			File info.
	 * @return 0 on success or negative errno.
	 */
	static int Read(const char *iRelativePath, char *ioBuf, size_t iSize, off_t iOffset, fuse_file_info *iFileInfo);

	/**
	 * @brief Write data to a file at the specified offset.
	 * @param iRelativePath		Relative file path.
	 * @param iBuf				Buffer containing data to write.
	 * @param iBufSize			Buffer size.
	 * @param iOffset			Offset into file to start reading data.
	 * @param iFileInfo			File info.
	 * @return 0 on success or negative errno.
	 */
	static int Write(const char *iRelativePath, const char *iBuf, size_t iSize, off_t iOffset, fuse_file_info *iFileInfo);

	/**
	 * @brief Get file system statistics.
	 * @param iRelativePath		Relative file path.
	 * @param oStatistics		File system statistics.
	 * @return 0 on success or negative errno.
	 */
	static int Statfs(const char *iRelativePath, struct statvfs *oStatistics);

	/**
	 * @brief Release path (not implemented).
	 * @param iRelativePath		Relative file path.
	 * @param iFlags			Flags.
	 * @param iFileInfo			File info.
	 * @return 0 on success or negative errno.
	 */
	static int Release(const char *iRelativePath, fuse_file_info *iFileInfo);

	/**
	 * @brief Synchronize changes to a file.
	 * @param iRelativePath		Relative file path.
	 * @param isdatasync		???
	 * @param iFileInfo			File info.
	 * @return 0 on success or negative errno.
	 */
	static int Fsync(const char *iRelativePath, int isdatasync, fuse_file_info *iFileInfo);

	/**
	 * @brief Set the write buffer size.
	 * @param iBufSize 	Write buffer size.
	 * @return 0 on success or negative errno.
	 */
	void setWriteBufSize(uint32_t iBufSize);

	/**
	 * @brief Get refresh start time
	 * @return time_t
	 */
	static time_t getRefreshStartTime();

	/**
	 * @brief Set refresh start time
	 */
	static void setRefreshStartTime();

private:
	static void handler(int iSigNum);
	static std::string fuse_conn_info_toString(const fuse_conn_info &irCi);

	// Data
	pthread_mutex_t ivMutex;

	time_t ivRefreshTime;
};

extern FileSystem g_fileSystem;

#endif /* FILESYSTEM_H_ */
