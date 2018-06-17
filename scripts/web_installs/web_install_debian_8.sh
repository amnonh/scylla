#!/bin/bash -e
#
#  Copyright (C) 2018 ScyllaDB
#  Download and install Scylla

SUB_VERSION=`cat /etc/debian_version|cut -c3-`
if [[ 6 -gt $SUB_VERSION ]]; then
    echo "Scylla can not be installed on Debian distribution older than 8.6"
    echo "$DOWNLOAD_SITE"
else
    if [ ! -f /etc/apt/sources.list.d/backports.list ]; then
        echo 'deb http://http.debian.net/debian jessie-backports main' | tee /etc/apt/sources.list.d/backports.list > /dev/null;
    fi
    apt-get install -y apt-transport-https
    apt-get -y update
    apt-get -y install gnupg curl
    apt-key adv --fetch-keys https://download.opensuse.org/repositories/home:/scylladb:/scylla-3rdparty-jessie/Debian_8.0/Release.key
    apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 17723034C56D4B19
    wget -O /etc/apt/sources.list.d/scylla.list "http://repositories.scylladb.com/scylla/repo/$SCYLLA_RUID/debian/$SCYLLA_VERSION-jessie.list"
    apt-get -y update
    apt-get install -t jessie-backports ca-certificates-java 
    apt-get install -y openjdk-8-jre-headless
    update-java-alternatives --jre-headless -s java-1.8.0-openjdk-amd64
    
    apt-get -y install scylla
fi
