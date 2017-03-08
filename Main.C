/*
 * Main.C
 *
 *  Created on: Oct 7, 2011
 *      Author: christen
 */

#include "Main.H"
#include "FileSystem.H"
#include "Cache.H"
#include "RmtFs.H"
#include "Types.H"

#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <vector>
#include <string>

using namespace std;

// FUSE file system call backs
static struct fuse_operations snapshotfs_oper = {
    FileSystem::Getattr,
    FileSystem::Readlink,
    FileSystem::Getdir,
    FileSystem::Mknod,
    FileSystem::Mkdir,
    FileSystem::Unlink,
    FileSystem::Rmdir,
    FileSystem::Symlink,
    FileSystem::Rename,
    FileSystem::Link,
    FileSystem::Chmod,
    FileSystem::Chown,
    FileSystem::Truncate,
    FileSystem::Utime,
    FileSystem::Open,
    FileSystem::Read,
    FileSystem::Write,
    FileSystem::Statfs,
    NULL, 				// flush
    FileSystem::Release,
    FileSystem::Fsync,
    NULL, 				// setxattr
    NULL,				// getxattr
    NULL,				// listxattr
    NULL,				// removexattr
    NULL,				// opendir
    NULL,				// readdir
    NULL,				// releasedir
    NULL,				// fsyncdir
    FileSystem::Init,	// init
    NULL,				// destroy
    NULL,				// access
    NULL,				// create
    NULL,				// ftruncate
    NULL,				// fgetattr
    NULL,				// lock
    NULL,				// utimnens
    NULL,				// bmap
    false,				// flag_nullpath_ok
    0,					// flag_reserved
    NULL,           	// ioctl
    NULL				// poll
};

void Main::usage()
{
	printf("usage:\n\n");
	printf("\tsnapshotfs [user@]host:/dir mountpoint [options]\n");
	printf("\t\t-- or --\n");
	printf("\tsnapshotfs offline mountpoint # mount cache offline \n");
	printf("\t\t-- or --\n");
	printf("\tsnapshotfs refresh mountpoint # mark cache to do refresh\n");
	printf("\n");
	printf("\toptions:\n");
	printf("\t\t--cachedir,-c path  Cache directory.  Default is '~/.cache/snapshotfs'.\n");
	printf("\t\t--readonly,-ro      Read-only file system.\n");
	printf("\t\t--nonempty,-n       Mount over a non-empty mount point, making the underlying file system inaccessible.\n");
	printf("\t\t--snapshot,-s name  Use cache snapshot taken by the snapshot-tool.sh script.\n");
	printf("\t\t--debug,-d          Enable debug mode.  Does a lot of logging to SYSLOG (/var/log/messages).\n");
	printf("\t\t--password,-pw      Password when mounted online.  Eliminates prompt for password.\n");
	printf("\t\t--port,-p port      SSH port.\n");
	printf("\n");
	printf("\tEnvironment variables:\n");
	printf("\t\tSNAPSHOTFS_PW          Password\n");
	printf("\n");
	printf("Examples:\n");
	printf("\tSNAPSHOTFSPW=mypassword snapshotfs hostname:/dir /mnt/dir\n");
	printf("\tsnapshotfs hostname:/dir /mnt/dir --nonempty\n");
	printf("\tsnapshotfs hostname:/dir /mnt/dir --snapshot afs-1030  # use the snapshot named afs-1030\n");
	printf("\tsnapshotfs refresh /mnt/dir\n");
	printf("\tsudo umount /mnt/dir\n");	
	printf("\n");
	printf("Notes:\n");
	printf("\t1) The df command will show the mounted snapshotfs file systems.\n");	
}

int main(int argc, char *argv[])
{
	return Main::main(argc, argv);
}

int Main::main(int argc, char *argv[])
{
	int rc = 0;

	// Called by snapshot-tool.sh script?
	if(argc >= 2 && !strcmp(argv[1], "tool"))
	{
		return tool(argc, argv);
	}

	if(argc < 3 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
	{
		usage();
		return 0;
	}

	bool refresh = false;

	// Parse arguments
	if(!strcmp(argv[1], "offline"))
	{
		g_config.offline = true;
		g_config.readonly = true;
	}
	else if(!strcmp(argv[1], "refresh"))
	{
		refresh = true;
	}
	else
	{
		rc = parseHostAndDir(argv[1]);
		if(rc)
		{
			return rc;
		}
		g_config.refreshDirEntries = g_config.refreshOpenedFiles = true;
	}

	strcpy(g_config.mountPoint, argv[2]);

	// Handle environment variables
	const char *pw = getenv("SNAPSHOTFS_PW");
	if(pw)
	{
		strcpy(g_config.pass, pw);
	}

	// Check for options:
	char cacheBaseDir[PATH_MAX];
	sprintf(cacheBaseDir, "%s/.cache/snapshotfs", getenv("HOME")); // Default cache directory
	bool nonempty = false;
	char maxWriteSize[100]="";
	char maxReadSize[100]="max_read=524288"; // Default is 512K - seems to round down to 128K
	char snapshot[NAME_MAX] = "";
	for(int i = 3; i < argc; ++i)
	{
		const char *arg = argv[i];
		if(!strcmp(arg, "-c") || !strcmp(arg, "--cache"))
		{
			if(i+1 == argc)
			{
				printf("No cache directory specified\n");
				return EINVAL;
			}
			strcpy(cacheBaseDir, argv[++i]); // override default
		}
		else if(!strcmp(arg, "-ro") || !strcmp(arg, "--readonly"))
		{
			g_config.readonly = true;
		}
		else if(!strcmp(arg, "-o") || !strcmp(arg, "--nonempty"))
		{
			nonempty = true;
		}
		else if(!strcmp(arg, "--max_read"))
		{
			if(i+1 == argc)
			{
				printf("No max_read size specified\n");
				return EINVAL;
			}
			snprintf(maxReadSize, sizeof(maxReadSize), "max_read=%s", argv[++i]);
		}
		else if(!strcmp(arg, "--max_write"))
		{
			if(i+1 == argc)
			{
				printf("No max_write size specified\n");
				return EINVAL;
			}
			snprintf(maxWriteSize, sizeof(maxWriteSize), "max_write=%s", argv[++i]);
		}
		else if(!strcmp(arg, "--singlethread"))
		{
			g_config.singleThread = true;
		}
		else if(!strcmp(arg, "-d") || !strcmp(arg, "--debug"))
		{
			g_config.debug = true;
			//system("ulimit -c unlimited"); // Enable core dumps
		}
		else if(!strcmp(arg, "-s") || !strcmp(arg, "--snapshot"))
		{
			if(i+1 == argc)
			{
				printf("No snapshot name specified\n");
				return EINVAL;
			}

			strcpy(snapshot, argv[++i]);
		}
		else if(!strcmp(arg, "-pw") || !strcmp(arg, "--password"))
		{
			if(i+1 == argc)
			{
				printf("No password specifed\n");
				return EINVAL;
			}

			strcpy(g_config.pass, argv[++i]);
		}
		else if(!strcmp(arg, "-p") || !strcmp(arg, "--port"))
		{
			if(i+1 == argc)
			{
				printf("No port number specified\n");
				return EINVAL;
			}
			g_config.port = atoi(argv[++i]);
		}
		else
		{
			printf("Invalid option %s\n", arg);
			return EINVAL;
		}

	}

	// Make sure the cache directory has been created
	sprintf(g_config.cacheDir, "%s%s", cacheBaseDir, g_config.mountPoint);
	mkCacheDir(g_config.cacheDir);

	// Mounting a snapshot?
	if(*snapshot) {
		// Verify that the snapshot exists and change the cache directory
		char path[PATH_MAX];
		sprintf(path, "%s/%s", g_config.cacheDir, snapshot);
		struct stat statInfo;
		if(lstat(path, &statInfo) == -1)
		{
			printf("Snapshot %s doesn't exist in %s\n", snapshot, path);
			return EINVAL;
		}

		// Set cache directory to snapshot name
		strcpy(g_config.cacheDir, path);
	}

	// Create log file in .cache/snapshotfs/xxx.log
	char name[100];
	strcpy(name, g_config.mountPoint);
	for(uint i = 1; name[i]; ++i) {
		if(name[i] == '/') {
			name[i] = '-';
		}
	}
	strcat(name, ".log");
	sprintf(g_config.logFile, "%s/%s", cacheBaseDir, name);

	if(verifyConfig()) return 1;

	// Delete the SYSLOG file
	SYSLOG_DELETE();
	SYSLOG("%s","Mounting snapshotfs");

	// Verify access to mount point
	struct stat statMntPnt;
	if(stat(g_config.mountPoint, &statMntPnt))
	{
		int rc = errno;
		printf("%s: %s\n", g_config.mountPoint, strerror(rc));
		return rc;
	}

	if(!g_config.offline && !*g_config.pass)
	{
		// Prompt for password
		rc = passwordPrompt();
		if(rc)
		{
			return rc;
		}
	}

	// Verify network connection
	if(!g_config.offline)
	{
		rc = verifyNetwork();
		if(rc) {
			return rc;
		}
	}

	// Refresh cache?
	if(refresh) {
		FileSystem::setRefreshStartTime();
		return 0;
	}

	// Launch fuse
    vector<const char*> args;
    char fsName[NAME_MAX] = "snapshotfs-";
    strcat(fsName,g_config.mountPoint+1);
    if(fsName[strlen(fsName)-1] == '/') fsName[strlen(fsName)-1] = '\0';
    if(*snapshot)
    {
    	strcat(fsName,"-");
    	strcat(fsName, snapshot);
    }

    if(g_config.readonly) strcat(fsName,"-readonly");

    if(g_config.debug) strcat(fsName,"-debug");
    for(uint i = 0; fsName[i]; ++i) {
		if(fsName[i] == '/') {
			fsName[i] = '-';
		}
	}
    args.push_back(fsName);

    args.push_back(g_config.mountPoint);
    args.push_back("-o");
    args.push_back("allow_other");

    if(g_config.singleThread)
    {
    	args.push_back("-s"); // use single thread only
    }

    // Overlay existing file system mount point?
    if(nonempty)
    {
    	args.push_back("-o");
    	args.push_back("nonempty");
    }

    if(*maxReadSize)
    {
    	args.push_back("-o");
    	args.push_back(maxReadSize);
    	args.push_back("-o");
    	args.push_back("large_read");
    }
//
//    if(*maxWriteSize)
//	{
//		args.push_back("-o");
//		args.push_back(maxWriteSize);
//	}

//    else
//    {
//    	args.push_back("-o");
//    	args.push_back("big_writes");
//    }

    printf("Mounting %s %s@%s:%s %s\n", fsName, g_config.rmtUser, g_config.rmtHost, g_config.rmtDir, g_config.mountPoint);
	for(uint i = 0; i < args.size(); ++i) {
		printf("arg %d=%s\n", i, args[i]);
	}
    if(fuse_main(args.size(), (char**)&args[0], &snapshotfs_oper, NULL))
	{
		printf("Mount failed!\n");
		if(statMntPnt.st_uid != getuid())
		{
			printf("Recommendation: Run sudo chown %s:%s %s\n", getenv("USER"), getenv("USER"), g_config.mountPoint);
		}
		return 1;
	}

    return 0;
}

int Main::tool(int argc, char *argv[])
{
	int rc = 0;

	if(argc < 4)
	{
		printf("Invalid snapshot-tool.sh script arguments.\n");
		return -1;
	}

	// Parse arguments
	strcpy(g_config.mountPoint, argv[3]);

	mkCacheDir("snapshotfs");

	if(verifyConfig()) return 1;

	if(!strcmp(argv[2], "refresh-cache"))
	{
		rc = parseHostAndDir(argv[4]);
		if(rc)
		{
			return rc;
		}

		rc = passwordPrompt();
		if(rc)
		{
			return rc;
		}

		// Verify network connection
		rc = verifyNetwork();
		if(rc) {
			return rc;
		}

		Cache::refreshCache();
	}
	else if(!strcmp(argv[2], "clean-cache"))
	{
		Cache::cleanCache();
	}
	else if(!strcmp(argv[2], "delete-cache"))
	{
		rc = Cache::deleteCache();
	}

    return rc;
}

int Main::parseHostAndDir(char *iArg)
{
	char *rmtMount = iArg;
	char *pColon = strstr(rmtMount, ":");
	if(!pColon)
	{
		printf("Invalid [user@]host:/dir syntax: %s\n", rmtMount);
		return EINVAL;
	}

	*pColon = '\0';

	char *pAt = strstr(rmtMount, "@");
	if(pAt)
	{
		*pAt = '\0';
		strcpy(g_config.rmtUser, rmtMount);
		strcpy(g_config.rmtHost, pAt+1);
	}
	else
	{
		strcpy(g_config.rmtUser, getenv("USER"));
		strcpy(g_config.rmtHost, rmtMount);
	}
	strcpy(g_config.rmtDir, pColon+1);
	return 0;
}

int Main::passwordPrompt()
{
	int rc = 0;
	// Prompt for password
	char prompt[200];
	snprintf(prompt, sizeof(prompt), "%s@%s's password:", g_config.rmtUser, g_config.rmtHost);
	char *password = getpass(prompt);
	if(!password)
	{
		printf("No password entered!\n");
		return -1;
	}
	strcpy(g_config.pass, password);

	return rc;
}

void Main::mkCacheDir(const char *iPath)
{
	/** @todo use mkdir function to create one or more sub directories **/
	char cmd[NAME_MAX];
	snprintf(cmd, sizeof(cmd), "/bin/mkdir -p %s", iPath);
	system(cmd);
}

int Main::verifyConfig()
{
	int rc = 0;

	// Verify the /etc/syscond/fuse.conf specifies: user_allow_other
	char fuseConfig[] = "/etc/fuse.conf";
	FILE *file = fopen(fuseConfig, "r");
	if(!file)
	{
		rc = errno;
		printf("Cannot open /etc/fuse.conf - %s\n", strerror(errno));
	}
	else
	{
		char line[1024];
		rc = -1; // assume user_allow_other is not specified
		while(fgets(line, sizeof(line), file))
		{
			if(strrchr(line, '#')) continue;
			if(!strcmp(line, "user_allow_other\n"))
			{
				rc = 0;
				break;
			}
		}
		fclose(file);
	}

	// user_allow_other not present?
	if(rc)
	{
		printf("The following line must be added to the /etc/fuse.conf file: user_allow_other\n");
	}
	else
	{
		struct stat statInfo;
		if(stat("/usr/bin/lftp", &statInfo))
		{
			rc = errno;
			printf("The lftp package must be installed!\n");
			return rc;
		}
	}

	return rc;
}

int Main::verifyNetwork()
{
	int rc = 0;

	rc = RmtFs::test();
	if(rc)
	{
		printf("Cannot connect to %s@%s:%d - %s\n", g_config.rmtUser, g_config.rmtHost, g_config.port, strerror(rc));
		printf("\t- Check BSO authentication on local system.\n");
		printf("\t- Check AFS authentication (klog) on %s.\n", g_config.rmtHost);
		printf("\t- Check GSA authentication (gsa_login) on %s.\n", g_config.rmtHost);
	}

	return rc;
};
