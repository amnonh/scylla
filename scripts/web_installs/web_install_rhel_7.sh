#!/bin/bash -e
#
#  Copyright (C) 2018 ScyllaDB
#  Download and install Scylla

SUB_VERSION=`rpm --query redhat-release-server|cut -d'-' -f4|cut -d'.' -f1`
if [[ 2 -gt $SUB_VERSION ]]; then
    echo "Scylla can not be installed on RHEL distribution older than 7.2"
    echo "$DOWNLOAD_SITE"
else
    rpm -i http://dl.fedoraproject.org/pub/epel/7/x86_64/Packages/e/epel-release-7-11.noarch.rpm
    curl -o /etc/yum.repos.d/scylla.repo -L "http://repositories.scylladb.com/scylla/repo/$SCYLLA_RUID/centos/$SCYLLA_VERSION.repo"
    yum install -y scylla
fi
