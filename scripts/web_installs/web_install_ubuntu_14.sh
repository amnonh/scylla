#!/bin/bash -e
#
#  Copyright (C) 2018 ScyllaDB
#  Download and install Scylla

curl -o /etc/apt/sources.list.d/scylla.list -L "http://repositories.scylladb.com/scylla/repo/$SCYLLA_RUID/ubuntu/$SCYLLA_VERSION-trusty.list"
add-apt-repository -y ppa:openjdk-r/ppa
apt-get -y update
apt-get install -y openjdk-8-jre-headless
update-java-alternatives -s java-1.8.0-openjdk-amd64
apt-get -y update
apt-get -y install scylla