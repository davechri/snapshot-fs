/*
 * Metadata.C
 *
 *  Created on: May 20, 2013
 *      Author: christen
 */


#include "Metadata.H"
#include "Types.H"
#include "RmtFs.H"
#include <limits.h>

using namespace std;

string Metadata::Entry::toString() const
{
	char str[1024] = "";
	snprintf(str, sizeof(str), "name=%s mode=%04x populateTime=%d type=%s",
			name, statInfo.st_mode, (uint32_t)populateTime,
			(negative?"'NEGATIVE'":"NORMAL"));
	return str;
}

bool Metadata::exists(const char *iRelativePath)
{
	EntryStat mdEntry;
	return find(iRelativePath, mdEntry);
}

bool Metadata::findChildren(const char *iRelativePath, vector<Metadata::EntryStat> &orChildren)
{
	lock();

	Entry *pMdEntry = locate(iRelativePath);
	if(pMdEntry)
	{
		for(map<Key,Entry>::iterator iter = pMdEntry->children.begin();
				iter != pMdEntry->children.end(); ++iter)
		{
			orChildren.push_back(iter->second);
		}
	}

	unlock();

	return !orChildren.empty();
}

bool Metadata::find(const char *iRelativePath, EntryStat &orEntry)
{
	lock();

	bzero(&orEntry, sizeof(orEntry));
	Entry *pMdEntry = locate(iRelativePath);
	if(pMdEntry)
	{
		orEntry = *pMdEntry;
	}

	unlock();

	return pMdEntry != NULL;
}

Metadata::Entry* Metadata::locate(const char *iRelativePath)
{
	SYSLOG("Entry: %s", iRelativePath);
	Entry *pMdEntry = NULL;
	RelativePath path(iRelativePath);
	char *nextName = path;

	lock();

	// Root exits?
	if(!ivpRoot)
	{
		ivpRoot = new Entry();
		strcpy(ivpRoot->name, "/");
		RmtFs::lstat(RmtDir("/"), ivpRoot->statInfo);
		ivpRoot->statInfo.st_ctim.tv_sec = 0;
	}

	// Root?
	if(strlen(nextName) == sizeof('/'))
	{
		SYSLOG("Located root: %s", iRelativePath);
		pMdEntry = ivpRoot;
	}
	else
	{
		// Look up each name in the directory path
		nextName++; // after '/'
		char *curName = nextName;
		nextName = strchr(nextName, '/');
		if(nextName)
		{
			*nextName = '\0';
			++nextName;
		}

		map<Key,Entry> *pList = &ivpRoot->children;
		while(curName && pMdEntry == NULL)
		{
			// Find name
			map<Key,Entry>::iterator iter = pList->find(Key(curName));
			// Name non found?
			if(iter == pList->end())
			{
				curName = NULL; // Not found
			}
			else
			{
				// More names to match?
				if(nextName)
				{
					SYSLOG("Switch to child iterator of: %s", iter->second.toString().c_str());
					pList = &iter->second.children; // advance to children
					if(nextName)
					{
						curName = nextName;
						nextName = strchr(nextName, '/');
						if(nextName)
						{
							*nextName = '\0';
							++nextName;
						}
					}
					else
					{
						curName = NULL;
					}
				}
				else
				{
					// Done
					//SYSLOG("Found entry: %s", iter->toString().c_str());
					pMdEntry = &iter->second;
				}
			}
		}
	}

	if(pMdEntry)
	{
		SYSLOG("Found: %s %s", iRelativePath, pMdEntry->toString().c_str());
	}
	else
	{
		SYSLOG("Not found: %s", iRelativePath);
	}

	unlock();

	return pMdEntry;
}

void Metadata::add(const char *iRelativePath,
		const struct stat &irStatInfo,
		time_t iPopulateTime, EntryType iType)
{
	SYSLOG("Entry: %s", iRelativePath);
	RelativePath path(iRelativePath);
	Entry mdEntry;
	mdEntry.populateTime = iPopulateTime;
	mdEntry.negative = iType == NEGATIVE;
	mdEntry.statInfo = irStatInfo;

	lock();

	if(strlen(iRelativePath) == 1)
	{
		delete ivpRoot;
		*mdEntry.name = '/';
		ivpRoot = new Entry(mdEntry);
	}
	else
	{
		strcpy(mdEntry.name, path.fileName());
		// Locate parent
		path.cdToParent();
		Entry *pMdParent = locate(path.toString());
		if(pMdParent)
		{
			// Check for old entry
			// @todo check for stale entries
			map<Key,Entry>::iterator iter = pMdParent->children.find(mdEntry.name);
			if(iter != pMdParent->children.end())
			{
				SYSLOG("Inserting unique old entries %s new %s",
							iRelativePath, mdEntry.toString().c_str());
				mdEntry.children.insert(iter->second.children.begin(),
										iter->second.children.end());
				pMdParent->children.erase(iter);
			}
			// Add child
			pMdParent->children[mdEntry.name] = mdEntry;
			SYSLOG("successful: %s %s", iRelativePath, mdEntry.toString().c_str());
		}
		else
		{
			SYSLOG_ERROR("Parent directory not found: %s", iRelativePath);
		}
	}

	unlock();
}
void Metadata::remove(const char *iRelativePath)
{
	SYSLOG("Entry: %s", iRelativePath);

	lock();

	if(strlen(iRelativePath) == sizeof('/'))
	{
		SYSLOG_ERROR("Cannot delete the root: %s", iRelativePath);
	}
	else
	{
		RelativePath parentPath(iRelativePath);
		parentPath.cdToParent();
		Entry *pMdEntry = locate(parentPath.toString());
		if(pMdEntry)
		{

			pMdEntry->children.erase(pMdEntry->name);
			SYSLOG("%s %s", iRelativePath, pMdEntry->toString().c_str());
		}
		else
		{
			SYSLOG_ERROR("Not found: %s", iRelativePath);
		}
	}

	unlock();
}

