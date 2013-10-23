#!/bin/sh
#
# PVR HW-recovery dumper.
#
# This tool dumps all the state provided by the pvr drivers hwrec interface.
#
# It reads debugfs/pvr/hwrec_event, which blocks until a hwrec event happens.
# Then it tars up hwrec_regs, hwrec_status, hwrec_mem and hwrec_edm and
# lzops it for later retrieval and analysis.
#
# Intended use case: call this script from /etc/init/sgx.conf after
# modprobe omaplfb, so that hwrec dumps are provided all the time.
#

#
# Edit these.
#
DEBUGFS_DIR=/sys/kernel/debug
DUMP_DIR=/home/user/MyDocs

#
# Everything down here should just match the driver.
#

PVR_DEBUGFS_DIR=$DEBUGFS_DIR/pvr/

if [ `id -nu` != "root" ]; then
    echo "dumper needs to be run as root!"
    exit
fi

# mount debugfs if needed.
if [ ! -e $DEBUGFS_DIR ]; then
    echo "Debugfs mount point $DEBUGFS_DIR doesn't exist!"
    exit
fi

if [ ! -e $PVR_DEBUGFS_DIR ]; then
    mount -t debugfs none $DEBUGFS_DIR
fi

if [ ! -e $PVR_DEBUGFS_DIR ]; then
    echo "Failed to mount debugfs!"
    exit
fi

# check debugfs hwrec files.
if [ ! -e $PVR_DEBUGFS_DIR/hwrec_event -o \
     ! -e $PVR_DEBUGFS_DIR/hwrec_time -o \
     ! -e $PVR_DEBUGFS_DIR/hwrec_regs -o \
     ! -e $PVR_DEBUGFS_DIR/hwrec_status ]; then
    echo "Failed to find debugfs hwrec files!"
    exit
fi

if [ ! -f $PVR_DEBUGFS_DIR/hwrec_event -o \
     ! -f $PVR_DEBUGFS_DIR/hwrec_time -o \
     ! -f $PVR_DEBUGFS_DIR/hwrec_regs -o \
     ! -f $PVR_DEBUGFS_DIR/hwrec_status -o \
     ! -r $PVR_DEBUGFS_DIR/hwrec_event -o \
     ! -r $PVR_DEBUGFS_DIR/hwrec_time -o \
     ! -r $PVR_DEBUGFS_DIR/hwrec_regs -o \
     ! -r $PVR_DEBUGFS_DIR/hwrec_status ]; then
    echo "Unable to access debugfs hwrec files!"
    exit
fi

# might have been built in conditionally.
if [ ! -e $PVR_DEBUGFS_DIR/hwrec_edm -o \
     ! -f $PVR_DEBUGFS_DIR/hwrec_edm -o \
     ! -r $PVR_DEBUGFS_DIR/hwrec_edm ]; then
    HAVE_EDM=
else
    HAVE_EDM=1
fi

if [ ! -e $PVR_DEBUGFS_DIR/hwrec_mem -o \
     ! -f $PVR_DEBUGFS_DIR/hwrec_mem -o \
     ! -r $PVR_DEBUGFS_DIR/hwrec_mem ]; then
    HAVE_MEM=
else
    HAVE_MEM=1
fi

# check for dump directory.
if [ ! -e $DUMP_DIR ]; then
    echo "Dump directory $DUMP_DIR does not exist!"
    exit
fi

# Now do the actual work.
while true
do
    cat $PVR_DEBUGFS_DIR/hwrec_event

    TIMESTAMP=`cat $PVR_DEBUGFS_DIR/hwrec_time`

    HWREC_DUMP_DIR=pvr_hwrec_$TIMESTAMP
    HWREC_DUMP_FILE=$HWREC_DUMP_DIR.tar.lzo

    if [ -e $DUMP_DIR/$HWREC_DUMP_FILE ]; then
	echo "Skipping dump: $DUMP_DIR/$HWREC_DUMP_FILE already exists."
    else
	if [ -e $DUMP_DIR/$HWREC_DUMP_DIR ]; then
	    echo "Skipping dump: $DUMP_DIR/$HWREC_DUMP_DIR already exists."
	else
	    mkdir "$DUMP_DIR/$HWREC_DUMP_DIR"
	    cat $PVR_DEBUGFS_DIR/hwrec_regs > \
                        $DUMP_DIR/$HWREC_DUMP_DIR/hwrec_regs
	    cat $PVR_DEBUGFS_DIR/hwrec_status > \
                        $DUMP_DIR/$HWREC_DUMP_DIR/hwrec_status

	    if [ $HAVE_EDM ]; then
		cat $PVR_DEBUGFS_DIR/hwrec_edm > \
		    $DUMP_DIR/$HWREC_DUMP_DIR/hwrec_edm
	    fi

	    if [ $HAVE_MEM ]; then
                cat $PVR_DEBUGFS_DIR/hwrec_mem > \
                    $DUMP_DIR/$HWREC_DUMP_DIR/hwrec_mem
            fi

	fi

	tar -c -C $DUMP_DIR $HWREC_DUMP_DIR | lzop > $DUMP_DIR/$HWREC_DUMP_FILE

	rm -R $DUMP_DIR/$HWREC_DUMP_DIR/

	echo "Dumped HWRecovery frame to $DUMP_DIR/$HWREC_DUMP_FILE"
    fi
done
