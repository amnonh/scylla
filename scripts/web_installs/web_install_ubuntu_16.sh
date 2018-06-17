#!/bin/bash -e
#
#  Copyright (C) 2018 ScyllaDB
#  Download and install Scylla

apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 6B2BFD3660EF3F5B
apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 17723034C56D4B19

curl -o /etc/apt/sources.list.d/scylla.list -L "http://repositories.scylladb.com/scylla/repo/$SCYLLA_RUID/ubuntu/$SCYLLA_VERSION-xenial.list"

apt-get -y update
apt-get -y install scylla