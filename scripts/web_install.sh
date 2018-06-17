#!/bin/bash -e
#
#  Copyright (C) 2018 ScyllaDB
#  Download and install Scylla

DOWNLOAD_SITE="Check Scylla download site for supported distributions: https://www.scylladb.com/download/"
SCYLLA_BASE="https://github.com/scylladb/scylla/tree/master/scripts"
SCYLLA_VERSION="scylladb-2.1"
SCYLLA_RUID="RUID_PLACE_HOLDER"

verify_version() {
    if [ "$2" = "" ]; then
        if [ "$VERSION_ID" != "$1" ]; then
            echo "Scylla can not be installed on $ID $VERSION_ID"
            echo "$DOWNLOAD_SITE"
            return 0
        fi
    else
        if [ "$VERSION_ID" != "$1" ] && [ "$VERSION_ID" != "$2" ]; then
            echo "Scylla can not be installed on $ID $VERSION_ID"
            echo "$DOWNLOAD_SITE"
            return 0
        fi
    fi
}

install_debian() {
    if verify_version "8"; then
        source <(curl -s "$SCYLLA_BASE/web_installs/web_install_debian_$VERSION_ID.sh")
    fi
}


install_ubuntu() {
    if verify_version "14.04" "16.04"; then
        if [ "$VERSION_ID" = "14.04" ]; then
            source <(curl -s "$SCYLLA_BASE/web_installs/web_install_ubuntu_14.sh")
        else
            source <(curl -s "$SCYLLA_BASE/web_installs/web_install_ubuntu_16.sh")
        fi
    fi
}

install_centos() {
    if verify_version "7"; then
        source <(curl -s "$SCYLLA_BASE/web_installs/web_install_centos_$VERSION_ID.sh")
    fi
}

install_rhl() {
    VERSION_ID=`echo $VERSION_ID| cut -d'.' -f1`
    if verify_version "7"; then
       source <(curl -s "$SCYLLA_BASE/web_installs/web_install_rhel_$VERSION_ID.sh")
    fi
}

if [ "`id -u`" -ne 0 ]; then
    echo "The installation script requires root permission."

else
    OS=`uname -s`
    if [ "$OS" != "Linux" ]; then
        echo "Scylla can only be installed on Linux."
        echo "$DOWNLOAD_SITE"
    else
        if [ -f /etc/os-release ]; then
            . /etc/os-release
            if [ "$ID" = "debian" ]; then
                install_debian
            elif [ "$ID" = "centos" ]; then
                install_centos
            elif [ "$ID" = "rhel" ]; then
                install_rhl
            elif [ "$ID" = "ubuntu" ]; then
                install_ubuntu
            else
                echo "Unsupported linux version $ID"
                echo "$DOWNLOAD_SITE"
            fi
        else
            echo "Unsupported linux version"
            echo "$DOWNLOAD_SITE"
        fi
    fi
fi
