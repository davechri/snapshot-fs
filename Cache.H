/*
 * Cache.H
 *
 *  Created on: Oct 7, 2011
 *      Author: christen
 */

#ifndef CACHE_H_
#define CACHE_H_

#include <stddef.h>

#include "Metadata.H"

class Cache
{
public:
	/**
	 * @brief Check if a directory has been populated.
	 * @param iRelativePath		Relative file path.
	 * @param iMdEntry 			Metadata entry
	 * @return true, if the specified directory has been populated.
	 * @attention If iRelativePath is a file, its parent directory is checked.
	 */
	static bool isPopulated(const char *iRelativePath, const Metadata::EntryStat &iMdEntry);

	/**
	 * @brief Get a directory list and populate all files and directory entries.
	 * 		  The file contents is not read.
	 * @param iRelativePath		Relative file path.
	 * @param iMdEntry 			Metadata entry
	 * @return errno
	 * @attention If iRelativePath is a file, its parent directory is checked.
	 */
	static int populateDir(const char *iRelativeDir, const Metadata::EntryStat &iMdEntry);

	/**
	 * @brief Cache file.
	 * @param iRelativePath		Relative file path.
	 * @param iStatInfo			Status information for file.
	 * @return errno
	 */
	static int cacheFile(const char *iRelativePath, struct stat iStatInfo);

	/**
	 * @brief Refresh file content, if it is stale (out of date).
	 * @param iRelativePath		Relative file path.
	 * @param ipRmtStat			Status of remote file, if known
	 * @return 0 on success, or errno.
	 */
	static int refreshIfStale(const char *iRelativePath, const struct stat *ipRmtStat=NULL);

	/**
	 * @brief Refresh the entire cache.
	 */
	static void refreshCache();

	/**
	 * @brief Delete all status files from cache.
	 * @attention This may only be needed if the cache becomes corrupted.
	 */
	static void cleanCache();

	/**
	 * @brief Delete all directories and files in cache.
	 * @return 0 or errno
	 */
	static int deleteCache();

	//static int lftpPut(const char *iRelativePath);
private:
	Cache(); // Disallow constructor
	static int lftpGet(const char *iRelativePath);
	static int lftpMirror(const char *iRelativePath);
	static int nftwRefreshCacheFunc(const char *iPath, const struct stat *iStat, int iTypeflag, struct FTW *iFtwbuf);
	static int refreshCacheDir(char *iDir, char *iRelativePath);
	static int nftwCleanCacheFunc(const char *iPath, const struct stat *iStat, int iTypeflag, struct FTW *iFtwbuf);
	static int cleanCacheDir(char *iDir, char *iRelativePath);
};


#endif /* CACHE_H_ */
