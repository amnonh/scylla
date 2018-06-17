#!/bin/bash -e
#
#  Copyright (C) 2018 ScyllaDB
#  Download and install Scylla

SUB_VERSION=`rpm --query centos-release|cut -d'-' -f4|cut -d'.' -f1`
if [[ 2 -gt $SUB_VERSION ]]; then
    echo "Scylla can not be installed on Centos distribution older than 7.2"
    echo "$DOWNLOAD_SITE"
else
    yum install -y epel-release
    curl -o /etc/yum.repos.d/scylla.repo -L "http://repositories.scylladb.com/scylla/repo/$SCYLLA_RUID/centos/$SCYLLA_VERSION.repo"
    sudo yum install -y scylla
fi