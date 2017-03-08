#!/bin/bash

MOUNT_POINT=
HOST=localhost
RMT_DIR=/
CACHE_DIR=~/.cache/snapshotfs

# Usage
usage()
{
	echo "usage: snapshot-tool.sh OPERATION mountpoint [optional parameters]"
	echo "  OPERATIONS:"
	echo "    refresh-cache    Update stale cache entries and remove deleted files"
	echo "        Syntax: snapshot-tool.sh refresh-cache mountpoint [user@host:/dir]"
	echo "    clean-cache      Remove cached status (attribute) files."
	echo "        Syntax: snapshot-tool.sh clean-cache mountpoint"
	echo "    delete-cache     Delete all directories and files stored in the cache."
	echo "        Syntax: snapshot-tool.sh delete-cache mountpoint"
	echo "    create-snapshot  Create a snapshot of the cache."
	echo "        Syntax: snapshot-tool.sh create-snapshot mountpoint snapshot-name"
	echo "    delete-snapshot  Delete a snapshot."
	echo "        Syntax: snapshot-tool.sh delete-snapshot snapshot-name"
	echo 
	echo "  Examples:"
	echo "    snapshot-tool.sh refresh-cache /mnt/dir hostname:/dir"
	echo "    snapshot-tool.sh clean-cache /dir"
	echo "    snapshot-tool.sh delete-cache /dir"
	echo "    snapshot-tool.sh create-snapshot /dir snapshot-1030      
	echo "    snapshot-tool.sh delete-snapshot snapshot-1030"
	
	exit 1
}

SNAPSHOTFS=snapshotfs
if ! (which snapshotfs > /dev/null 2>1); then		
	if ! (which ./Release/snapshotfs > /dev/null 2>1); then
		echo "snapshotfs executable not found (Update PATH variable)"
		exit 1
	else
		SNAPSHOTFS=./Release/$SNAPSHOTFS
	fi
fi	

if [ "$1" = "refresh-cache" ]; then	
	if [ $# != 3 ]; then
		echo "Invalid number of arguments for $1"
		exit 1
	fi
	$SNAPSHOTFS tool $1 $2 $3	
	exit $?
elif [ "$1" = "clean-cache" ]; then
	if [ $# != 2 ]; then
		echo "Invalid number of arguments for $1"
		exit 1
	fi	
	$SNAPSHOTFS tool $1 $2	
	exit $?
elif [ "$1" = "delete-cache" ]; then	
	if [ $# != 2 ]; then
		echo "Invalid number of arguments for $1"
		exit 1
	fi
	$SNAPSHOTFS tool $1 $2
	exit $?
elif [ "$1" = "create-snapshot" ]; then	
	if [ $# != 3 ]; then
		echo "Invalid number of arguments for $1"
		exit 1
	fi
	CMD="cp -R -l $CACHE_DIR$2 $CACHE_DIR/$3"
	echo "Executing: $CMD"
	$CMD
	exit $?
elif [ "$1" = "delete-snapshot" ]; then	
	if [ $# != 2 ]; then
		echo "Invalid number of arguments for $1"
		exit 1
	fi
	CMD="rm -rf $CACHE_DIR/$2"
	echo "Executing: $CMD"
	$CMD
	exit $?
elif [ -z "$1" -o "$1" = "-h" -o "$1" = "--help" ]; then
	usage	
else
	echo "Unknown operation: $1"
	exit 1
fi
