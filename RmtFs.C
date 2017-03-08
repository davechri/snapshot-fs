/*
 * RmtFs.C
 *
 *  Created on: Oct 7, 2011
 *      Author: christen
 */

#include "RmtFs.H"
#include "FileSystem.H"
#include "Types.H"

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <netinet/tcp.h>

using namespace std;

// Static class data
deque<RmtFs*> RmtFs::cvConnectionList;
uint32_t RmtFs::cvConnectionCount = 0;

void RmtFs::lock() {g_fileSystem.lock();}
void RmtFs::unlock() {g_fileSystem.unlock();}


int RmtFs::getConnection(RmtFs *&opRmtFs)
{
	SYSLOG("%s", "");
	int rc= 0;
	RmtFs *pRmtFs = NULL;

	lock();
	// Reuse an active connection, if possible
	while(cvConnectionList.front() && !pRmtFs)
	{
		pRmtFs = cvConnectionList.front();
		cvConnectionList.pop_front();
		// Is connection still usable?
		time_t curTime;
		if(!pRmtFs->isConnected() ||
			// If the connection has not been used for more than an hour,
			// the connection may be in a bad state, so close it.
			pRmtFs->ivLastUseTime + MAX_IDLE_TIME < time(&curTime))
		{
			SYSLOG("%s", "delete bad connection");
			--cvConnectionCount;
			delete pRmtFs;
			pRmtFs = NULL;
		}
	}
	unlock();

	// No active connections?
	if(!pRmtFs)
	{
		pRmtFs = new RmtFs();

		rc = pRmtFs->connect(g_config.rmtHost);
		if(!rc)
		{
			rc = pRmtFs->login(g_config.rmtUser, g_config.pass);
		}
		if(rc)
		{
			delete pRmtFs;
			pRmtFs = NULL;
		}
		else
		{
			lock();
			++cvConnectionCount;
			unlock();
		}
	}

	if(pRmtFs)
	{
		time(&pRmtFs->ivLastUseTime); // Record last use time
	}

	opRmtFs = pRmtFs;

	return rc;
}

void RmtFs::freeConnection(RmtFs *ipRmtFs)
{
	bool deleteCnn = false;
	lock();
	if(ipRmtFs->isConnected() && cvConnectionCount <= MAX_IDLE_CONNECTION_COUNT)
	{
		cvConnectionList.push_front(ipRmtFs);
	}
	else
	{
		deleteCnn = true;
		--cvConnectionCount;
	}
	unlock();

	if(deleteCnn)
	{
		delete ipRmtFs;
	}
}

RmtFs::RmtFs() :ivpSsh(NULL), ivpSFtp(NULL), ivLastUseTime(0)
{
	ivpSsh = ssh_new();
	if(!ivpSsh)
	{
		SYSLOG_ERROR("%s", "ssh_new failed");
	}

	SYSLOG("constructor complete %s", toString().c_str());
}

RmtFs::~RmtFs()
{
	SYSLOG("destructor %s", toString().c_str());
	if(ivpSFtp)
	{
		sftp_free(ivpSFtp);
	}

	if(ivpSsh)
	{
		ssh_disconnect(ivpSsh);
		ssh_free(ivpSsh);
	}
}

bool RmtFs::isConnected() const
{
	bool connected = true;
	if(ivpSsh)
	{
		int fd = ssh_get_fd(ivpSsh);
		char buf[1];
		if(recv(fd, buf, 0, MSG_DONTWAIT))
		{
			if(errno != EAGAIN)
			{
				connected = false;
			}
		}
	}
	else
	{
		connected = false;
	}
	SYSLOG("%s", connected?"yes":"no");
	return connected;
}

std::string RmtFs::toString() const
{
	char buf[100];
	snprintf(buf, sizeof(buf), "ivpSsh=%p ivpSFtp=%p", ivpSsh, ivpSFtp);
	return buf;
}

int RmtFs::connect(const char *iHost)
{
	SYSLOG("%s %s", iHost, toString().c_str());
	char portStr[10];

	int rc = 0;
	do {
		ssh_options_set(ivpSsh, SSH_OPTIONS_HOST, iHost);
		if(g_config.port != 22) {
			sprintf(portStr, "%d", g_config.port);
			ssh_options_set(ivpSsh, SSH_OPTIONS_PORT_STR, portStr);
		}
		uint32_t seconds = TIMEOUT_SECS;
		ssh_options_set(ivpSsh, SSH_OPTIONS_TIMEOUT, &seconds);
		rc = ssh_connect(ivpSsh);
		if(rc)
		{
			rc = ECONNRESET;
			SYSLOG_ERROR("ssh_connect error %d: ssh_get_error_code=%d", rc, ssh_get_error_code(ivpSsh));
			break;
		}
		else
		{
			// Set socket timeout
			timeval tv = {TIMEOUT_SECS, 0};
			if(setsockopt(ssh_get_fd(ivpSsh), SOL_SOCKET, SO_RCVTIMEO, (char*)&tv,  sizeof(tv)))
			{
				rc = errno;
				perror("setsockopt SO_RCVTIMEO");
				SYSLOG_ERROR("setsockopt SO_RECVTIMEO error %d: %s", rc, strerror(rc));
				break;
			}

			// Interval between sending keepalive probes
			uint32_t keepInt = KEEPALIVE_INTERVAL;
			if(setsockopt(ssh_get_fd(ivpSsh), SOL_TCP, TCP_KEEPINTVL, (char*)&keepInt,  sizeof(keepInt)))
			{
				rc = errno;
				perror("setsockopt TCP_KEEPINTVL");
				SYSLOG_ERROR("setsockopt SO_KEEPALIVE error %d: %s", rc, strerror(rc));
				break;
			}

			// Number of probes to send before closing connection
			uint32_t keepCount = KEEPALIVE_COUNT;
			if(setsockopt(ssh_get_fd(ivpSsh), SOL_TCP, TCP_KEEPCNT, (char*)&keepCount,  sizeof(keepCount)))
			{
				rc = errno;
				perror("setsockopt TCP_KEEPCNT");
				SYSLOG_ERROR("setsockopt SO_KEEPALIVE error %d: %s", rc, strerror(rc));
				break;
			}

			// Time to wait before sending probes on idle connection
			uint32_t keepIdle = KEEPALIVE_IDLE;
			if(setsockopt(ssh_get_fd(ivpSsh), SOL_TCP, TCP_KEEPIDLE, (char*)&keepIdle,  sizeof(keepIdle)))
			{
				rc = errno;
				perror("setsockopt TCP_KEEPIDLE");
				SYSLOG_ERROR("setsockopt SO_KEEPALIVE error %d: %s", rc, strerror(rc));
				break;
			}

			// Enable socket keepalive
			uint32_t keepAlive = true;
			if(setsockopt(ssh_get_fd(ivpSsh), SOL_SOCKET, SO_KEEPALIVE, (char*)&keepAlive,  sizeof(keepAlive)))
			{
				rc = errno;
				SYSLOG_ERROR("setsockopt SO_KEEPALIVE error %d: %s", rc, strerror(rc));
				break;
			}
		}
	} while(false);

	return rc;
}

int RmtFs::login(const char *iUser, const char *iPassword)
{
	SYSLOG("%s", toString().c_str());

	int rc = ssh_userauth_password(ivpSsh, iUser, iPassword);
	do {
		if(rc)
		{
			rc = EPERM;
			SYSLOG_ERROR("ssh_userauth_password error %d: ssh_get_error_code=%d",
					rc, ssh_get_error_code(ivpSsh));
			break;
		}
		else
		{
			ivpSFtp = sftp_new(ivpSsh);
			if(!ivpSFtp)
			{
				SYSLOG_ERROR("sftp_new error ssh_get_error_code=%d", ssh_get_error_code(ivpSsh));
				rc = EPERM;
				break;
			}

			rc = sftp_init(ivpSFtp);
			if(rc)
			{
				rc = EPERM;
				SYSLOG_ERROR("sftp_new error %d ssh_get_error_code=%d", rc, ssh_get_error_code(ivpSsh));
				break;
			}
		}
	} while(false);

	if(rc) printf("ssh_get_error_code=%d\n", ssh_get_error_code(ivpSsh)); // Print error to stdout
	return rc;
}

int RmtFs::convertSFtpRc(int iRc)
{
	if(iRc == SSH_FX_OK) return 0;
	if(iRc == SSH_FX_EOF) return 0;
	if(iRc == SSH_FX_NO_SUCH_FILE) return ENOENT;
	if(iRc == SSH_FX_PERMISSION_DENIED) return EACCES;
	if(iRc == SSH_FX_FAILURE) return EPERM;
	if(iRc == SSH_FX_BAD_MESSAGE) return EBADMSG;
	if(iRc == SSH_FX_NO_CONNECTION) return ENOTCONN;
	if(iRc == SSH_FX_CONNECTION_LOST) return ENETDOWN;
	if(iRc == SSH_FX_OP_UNSUPPORTED) return EOPNOTSUPP;
	if(iRc == SSH_FX_INVALID_HANDLE ) return EINVAL;
	if(iRc == SSH_FX_NO_SUCH_PATH) return ENOENT;
	if(iRc == SSH_FX_FILE_ALREADY_EXISTS) return EEXIST;
	if(iRc == SSH_FX_WRITE_PROTECT) return EACCES;
	if(iRc == SSH_FX_NO_MEDIA) return ENOMEDIUM;
	return EINVAL;
}

/**
 * @brief	Find the ssh/sftp error and convert it to an errno.
 * @return	errno - defaults to ENOENT if the ssh or sftp error cannot be determined.
 * @atttention getErrno always returns a non-zero errno
 */
int RmtFs::getErrno() const
{
	int rc = 0;
	if(ivpSFtp)
	{
		int sftpRc = sftp_get_error(ivpSFtp);
		if(sftpRc)
		{
			rc = convertSFtpRc(sftpRc);
			if(rc != ENOENT)
			{
				SYSLOG_ERROR("%s (errno=%d/SFTP RC=%d)", strerror(rc), rc, sftpRc);
			}
		}
	}

	if(!rc && ivpSsh)
	{
		rc = ssh_get_error_code(ivpSsh);
		if(rc)
		{
			SYSLOG_ERROR("ssh_get_error_code=%d", rc);
			rc = EIO; // Use generic errno
		}
	}

	if(!rc)
	{
		SYSLOG_ERROR("Set default error %d", ENOENT);
		rc = ENOENT;
	}
	return rc;
}

void RmtFs::convertStat(const sftp_attributes iSFtpAttr, struct stat &oStatInfo)
{
	bzero(&oStatInfo, sizeof(oStatInfo));
	oStatInfo.st_uid = getuid();
	oStatInfo.st_gid = getgid();
	oStatInfo.st_size = iSFtpAttr->size;
	oStatInfo.st_atim.tv_sec = iSFtpAttr->atime;
	oStatInfo.st_atim.tv_nsec = iSFtpAttr->atime_nseconds;
	oStatInfo.st_mtim.tv_sec = iSFtpAttr->mtime;
	oStatInfo.st_mtim.tv_nsec = iSFtpAttr->mtime_nseconds;
	oStatInfo.st_ctim.tv_sec = iSFtpAttr->createtime;
	oStatInfo.st_ctim.tv_nsec = iSFtpAttr->createtime_nseconds;
	oStatInfo.st_mode = iSFtpAttr->permissions;
}

int RmtFs::sftpLstat(const char *iPath, struct stat &oStatInfo)
{
	SYSLOG("%s", iPath);
	SYSLOG("%s", toString().c_str());
	int rc = 0;
	sftp_attributes sFtpAttr = sftp_lstat(ivpSFtp, iPath);
	if(!sFtpAttr)
	{
		rc = getErrno();
		SYSLOG("sftp_lstat error %d %s", rc, iPath);
	}
	else
	{
		convertStat(sFtpAttr, oStatInfo);
		sftp_attributes_free(sFtpAttr);
	}

	return rc;
}

int RmtFs::sftpReadlink(const char *iPath1, char *oPath2)
{
	SYSLOG("%s", iPath1);
	SYSLOG("%s", toString().c_str());
	int rc = 0;
	/** @todo is sftp_readlink really thread safe??? */
	char *path2 = sftp_readlink(ivpSFtp, iPath1);
	if(path2)
	{
		strcpy(oPath2, path2);
	}
	else
	{
		rc = getErrno();
	}

	return rc;
}

int RmtFs::sftpMkdir(const char *iDir, mode_t iMode)
{
	SYSLOG("%s", iDir);
	SYSLOG("%s", toString().c_str());
	int rc = sftp_mkdir(ivpSFtp, iDir, iMode);
	if(rc)
	{
		rc = getErrno();
		SYSLOG("sftp_mkdir error %d %s", rc, iDir);
	}

	return rc;
}

int RmtFs::sftpMknod(const char *iPath, mode_t iMode)
{
	SYSLOG("%s", iPath);
	SYSLOG("%s", toString().c_str());
	int rc = 0;
	sftp_file pFile = sftp_open(ivpSFtp, iPath, O_CREAT|O_EXCL, iMode);
	if(!pFile)
	{
		rc = getErrno();
		SYSLOG("sftp_open error %d %s", rc, iPath);
	}
	else
	{
		sftp_close(pFile);
	}

	return rc;
}

int RmtFs::nextDirEntry(Handle_t &ioHandle, const char *iDir, DirEntry &oDirEntry)
{
	int rc = 0;
	if(!ioHandle.pRmtFs)
	{
		rc = getConnection(ioHandle.pRmtFs);
	}

	if(ioHandle.pRmtFs)
	{
		rc = ioHandle.pRmtFs->sftpNextDirEntry(ioHandle, iDir, oDirEntry);
	}

	return rc;
}

int RmtFs::sftpNextDirEntry(Handle_t &ioHandle, const char *iDir, DirEntry &oDirEntry)
{
	SYSLOG("%s", iDir);
	SYSLOG("%s sftp_dir=%p", toString().c_str(), ioHandle.sftpDir);
	int rc = 0;
	if(!ioHandle.sftpDir)
	{
		SYSLOG("sftp_opendir %s", iDir);
		ioHandle.sftpDir = sftp_opendir(ivpSFtp, iDir);
	}

	if(!ioHandle.sftpDir)
	{
		rc = getErrno();
		SYSLOG_ERROR("sftp_opendir error %d %s", rc, iDir);
	}
	else
	{
		sftp_attributes sFtpAttr = sftp_readdir(ivpSFtp, ioHandle.sftpDir);
		if(!sFtpAttr)
		{
			if(!sftp_dir_eof(ioHandle.sftpDir))
			{
				rc = getErrno();
				SYSLOG_ERROR("sftp_readdir error %d: ssh_get_error_code=%d",
						rc, ssh_get_error_code(ivpSsh));
			}
			else
			{
				oDirEntry.noMoreEntries = true;
			}
			sftp_closedir(ioHandle.sftpDir);
			ioHandle.sftpDir = NULL;
		}
		else
		{
			SYSLOG("%s", sFtpAttr->name);
			oDirEntry.noMoreEntries = false;;
			strcpy(oDirEntry.name, sFtpAttr->name);
			convertStat(sFtpAttr, oDirEntry.statInfo);
			sftp_attributes_free(sFtpAttr);
		}
	}

	return rc;
}

size_t RmtFs::nextDataBlock(Handle_t &ioHandle, const char *iFile, char *iopBuf, size_t iBufSize, size_t &oDataSize)
{
	int rc = 0;
	if(!ioHandle.pRmtFs)
	{
		rc = getConnection(ioHandle.pRmtFs);
	}

	if(ioHandle.pRmtFs)
	{
		rc = ioHandle.pRmtFs->sftpNextDataBlock(ioHandle, iFile, iopBuf, iBufSize, oDataSize);
	}
	return rc;
}

int RmtFs::sftpNextDataBlock(Handle_t &ioHandle, const char *iFile, char *iopBuf, size_t iBufSize, size_t &oDataSize)
{
	SYSLOG("%s", iFile);
	SYSLOG("%s sftp_file=%p", toString().c_str(), ioHandle.sftpFile);
	int rc = 0;
	if(!ioHandle.sftpFile)
	{
		SYSLOG("sftp_open %s", iFile);
		ioHandle.sftpFile = sftp_open(ivpSFtp, iFile, O_RDONLY, 0);
	}

	if(!ioHandle.sftpFile)
	{
		rc = getErrno();
		SYSLOG_ERROR("sftp_open error %d %s", rc, iFile);
	}
	else
	{
		oDataSize = sftp_read(ioHandle.sftpFile, iopBuf, iBufSize);
		if(oDataSize < 0)
		{
			oDataSize = 0;
			rc = getErrno();
			SYSLOG_ERROR("sftp_readdir error %d", rc);
			sftp_close(ioHandle.sftpFile);
			ioHandle.sftpFile = NULL;
		}
		else if(oDataSize == 0)
		{
			sftp_close(ioHandle.sftpFile);
			ioHandle.sftpFile = NULL;
		}
	}

	return rc;
}

int RmtFs::write(Handle_t &ioHandle, const char *iFile, char *iBuf, size_t iBufSize)
{
	int rc = 0;
	if(!ioHandle.pRmtFs)
	{
		rc = getConnection(ioHandle.pRmtFs);
	}

	if(ioHandle.pRmtFs)
	{
		rc = ioHandle.pRmtFs->sftpWrite(ioHandle, iFile, iBuf, iBufSize);
	}
	return rc;
}

int RmtFs::sftpWrite(Handle_t &ioHandle, const char *iFile, const char *iBuf, size_t iBufSize)
{
	SYSLOG("%s iBufSize=%d", iFile, (int)iBufSize);
	SYSLOG("%s sftp_file=%p", toString().c_str(), ioHandle.sftpFile);
	int rc = 0;

	if(!ioHandle.sftpFile)
	{
		ioHandle.sftpFile = sftp_open(ivpSFtp, iFile, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
	}

	if(!ioHandle.sftpFile)
	{
		rc = getErrno();
		SYSLOG_ERROR("sftp_open error %d", rc);
	}
	else
	{
		SYSLOG("sftp_write size=%d", (int)iBufSize);
		if(sftp_write(ioHandle.sftpFile, iBuf, iBufSize) < 0)
		{
			rc = getErrno();
			SYSLOG_ERROR("sftp_write error %d", rc);
		}
	}

	return rc;
}

int RmtFs::sftpUnlink(const char *iPath)
{
	SYSLOG("%s %s", iPath, toString().c_str());
	int rc = sftp_unlink(ivpSFtp, iPath);
	if(rc)
	{
		rc = getErrno();
		SYSLOG("sftp_unlink error %d", rc);
	}

	return rc;
}

int RmtFs::sftpSymlink(const char *iTarget, const char *iDest)
{
	SYSLOG("%s %s", iTarget, iDest);
	int rc = sftp_symlink(ivpSFtp, iTarget, iDest);
	if(rc)
	{
		rc = getErrno();
		SYSLOG("sftp_symlink error %d", rc);
	}

	return rc;
}

int RmtFs::sftpRename(const char *iOldName, const char *iNewName)
{
	SYSLOG("%s to %s", iOldName, iNewName);
	int rc = sftp_rename(ivpSFtp, iOldName, iNewName);
	if(rc)
	{
		rc = getErrno();
		SYSLOG("sftp_rename error %d", rc);
	}

	return rc;
}

int RmtFs::sftpChmod(const char *iPath, mode_t iMode)
{
	SYSLOG("%s 0x%x", iPath, iMode);
	int rc = sftp_chmod(ivpSFtp, iPath, iMode);
	if(rc)
	{
		rc = getErrno();
		SYSLOG("sftp_chmod error %d", rc);
	}

	return rc;
}

int RmtFs::sftpChown(const char *iPath, uid_t iUid, gid_t iGid)
{
	SYSLOG("%s uid=%d gid=%d", iPath, iUid, iGid);
	int rc = sftp_chown(ivpSFtp, iPath, iUid, iGid);
	if(rc)
	{
		rc = getErrno();
		SYSLOG("sftp_chmod error %d", rc);
	}

	return rc;
}

int RmtFs::sftpTruncate(const char *iPath)
{
	SYSLOG("%s", iPath);
	int rc = 0;
	sftp_file pFile = sftp_open(ivpSFtp, iPath, O_TRUNC, 0);
	if(!pFile)
	{
		rc = getErrno();
		SYSLOG("sftp_chmod error %d", rc);
	}
	else
	{
		sftp_close(pFile);
	}

	return rc;
}

int RmtFs::sftpRmdir(const char *iDir)
{
	SYSLOG("%s %s", iDir, toString().c_str());
	int rc = sftp_rmdir(ivpSFtp, iDir);
	if(rc)
	{
		rc = getErrno();
		SYSLOG("sftp_rmdir error %d", rc);
	}

	return rc;
}

int RmtFs::sshRunCmd(const char *iCmd)
{
	SYSLOG("%s %s", iCmd, toString().c_str());
	int rc = 0;

	ssh_channel channel = channel_new(ivpSsh);
	if(channel == NULL)
	{
		rc = getErrno();
		SYSLOG("ssh_channel_new error %d", rc);
	}
	else
	{
		rc = channel_open_session(channel);
		if (rc)
		{
			SYSLOG("ssh_channel_open error %d", rc);
		}
		else
		{
			rc = channel_request_exec(channel, iCmd);
			if (rc)
			{
				SYSLOG("ssh_channel_request_exec error %d %s", rc, iCmd);
			}
			channel_close(channel);
		}
		channel_free(channel);
	}

	/** @todo convert return code to posix return code **/
	return rc;
}

