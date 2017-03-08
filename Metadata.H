/*
 * Metadata.H
 *
 *  Created on: May 20, 2013
 *      Author: christen
 */

#ifndef CACHE_METADATA_H_
#define CACHE_METADATA_H_

#include <map>
#include <string>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <vector>

/**
 * @todo:
 * - Adding locking to make thread-safe
 * - locate return copy???
 */
class Metadata {
public:
	Metadata() : ivpRoot(NULL) {
		pthread_mutex_init(&ivMutex, NULL);
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&ivMutex, &attr);
	}
	typedef std::string Key;
	struct EntryStat {
		// Constructor
		EntryStat() {
			bzero(this, sizeof(*this));
		}
		char				name[NAME_MAX];
		bool				negative; // Negative cache entry
		struct stat 		statInfo;
		time_t				populateTime; // Used by Cache::populate()
	};

	struct Entry : EntryStat {
		// Constructor
		Entry() : EntryStat() {
		}
		~Entry() {}
		// operators
		bool operator==(const Entry &irEntry) const {return !strcmp(name, irEntry.name);}
		bool operator!=(const Entry &irEntry) const {return strcmp(name, irEntry.name);}

		std::string toString() const;

		// Data
		std::map<Key,Entry> children;
	};

	bool exists(const char *iRelativePath);
	bool findChildren(const char *iRelativePath, std::vector<EntryStat> &orChildren);
	bool find(const char *iRelativePath, EntryStat &orEntry);
	enum EntryType {
		NORMAL,
		NEGATIVE
	};
	void add(const char *iRelativePath,
				const struct stat &irStatInfo, time_t iPopulateTime=0, EntryType iType=NORMAL);
	void remove(const char *iRelativePath);
private:
	Entry* locate(const char *iRelativePath);
	void lock() {pthread_mutex_lock(&ivMutex);}
	void unlock() {pthread_mutex_unlock(&ivMutex);}
	// Private data
	pthread_mutex_t 	ivMutex;
	Entry 				*ivpRoot; // Root entry
};

extern Metadata g_metadata;

#endif /* CACHE_METADATA_H_ */
