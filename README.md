This README file was copied from the https://githug.com/libfuse/sshfs project,
and modified as needed.

Abstract
========

This is a filesystem client based on the SSH File Transfer Protocol.
Since most SSH servers already support this protocol it is very easy
to set up: i.e. on the server side there's nothing to do.  On the
client side mounting the filesystem is as easy as logging into the
server with ssh.

The snapshotfs filesystem can be used like the sshfs filesystem, except
it is designed to cache a snapshot of the SSH connected filesystem.  Writing
to the filesystem is supported, but is experimental.  

Features of this implementation are:

  - Based on FUSE (the best userspace filesystem framework for Linux ;)

  - Multithreading: more than one request can be on it's way to the
    server

  - Files and directory entries are cached locally on first access.

  - Run applications locally instead of using VNC or remote desktop.

Latest version
==============

The latest version and more information can be found on
https://github.com/davechri/snapshot-fs


How to mount a filesystem
=========================

Once snapshotfs is installed (see next section) running it is very simple:

    snapshotfs hostname:/dir mountpoint

Note, that it's recommended to run it as user, not as root.  For this
to work the mountpoint must be owned by the user.  If the username is
different on the host you are connecting to, then use the
"username@host:" form.  If you need to enter a password snapshotfs will ask
for it (actually it just runs ssh which ask for the password if
needed).  You can also specify a directory after the ":".  The default
is the home directory.

To unmount the filesystem:

    fusermount -u mountpoint


Installing
==========

First you need to download FUSE 2.2 or later from
http://github.com/libfuse/libfuse.

These libraries must be installed:
- libpthread
- libssh

You also need to install the devel package for glib2.0.  After
installing FUSE, compile sshfs the usual way:

    make
    make install (as root)

And you are ready to go.
