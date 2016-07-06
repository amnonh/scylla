#!/usr/bin/python
#
# Copyright (C) 2015 ScyllaDB
#

#
# This file is part of Scylla.
#
# Scylla is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Scylla is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
#

import argparse
import sys
import tempfile
import os
import json
import subprocess
import urllib2
import urllib
import requests
import uuid
import shutil
import zipfile

VERSION = "1.0"
TELEMETRICS = "scylla-telemetrics"
USER_UPLOAD = "scylladb-users-upload"
tmpdir = None
files = []
quiet = False
zip_file_name = None
crypt_file = None
scylla_public_key = """-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgIJAMGyVpwnNZq3MA0GCSqGSIb3DQEBCwUAMDcxCzAJBgNV
BAYTAmlsMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxETAPBgNVBAoMCFNjeWxsYURC
MB4XDTE2MDYyNzA4MjE1NFoXDTE2MDcyNzA4MjE1NFowNzELMAkGA1UEBhMCaWwx
FTATBgNVBAcMDERlZmF1bHQgQ2l0eTERMA8GA1UECgwIU2N5bGxhREIwggEiMA0G
CSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCS6iUznKqqttjk0GoZiDKiDKwsz2RJ
MFbteq37F92c3xr33TxYJWCPAplqpYruAcKhOj2fsV8vt9LZwCW7e85bHZKTy8pc
psu07Rf0r4ZQdEZm02TDDAlaxaOE339go0m7q7Z9CGbkK80El2uTS0nKdQAMJw44
Hbd3SMy4zYqbt/q2ZUWYqtMTxgoNZoCg/p6odIkMmA00fGlMdoxtlU3j0C5RxlKM
Vr3QOP8kuOW3v1gJGxZHp7vOidoTyX6ikRRHEjpSNPiuMUMGZwjX9ndsw0zfq5hg
pltcX6vgnh8/4YHxlNM24bR4LsYFRdbalBPoj4tCZmGMdLWBEMpuhs7tAgMBAAGj
UDBOMB0GA1UdDgQWBBSbKapPDRhYaOQy208zRRrGbYvilTAfBgNVHSMEGDAWgBSb
KapPDRhYaOQy208zRRrGbYvilTAMBgNVHRMEBTADAQH/MA0GCSqGSIb3DQEBCwUA
A4IBAQAT4nV44+Co5+sCufnflcGI5x1mSA8KpkFPq9yC0FSfyOEoJwEEXxzdrHfZ
LtaM9tsdKTWEC91+4TbVfbuux34JqQsfsq1M+0RxZuIHMl9uX8uefVsEAN6sAe86
kHrELoieZKPeeUyWsgABLNRi5/aX6xeEzPNjuRfjbRvEpilxwWSqWTbfhsugR8qd
N9UdL4vgUcXsuQeDJc8/Uw8pdDTBJEPIjBuoG8X6s+OHNWyrxXZe+GPlL5TxDGoG
zipmo08H26o1rL5Qv1l2cBcddh0NmoGarVxnZ+eRGRCWRtKdiCHjofoej2Fhucku
HywMyOPvn/L9RvyZ2aA8/gxK3qL/
-----END CERTIFICATE-----

"""

version_url = "https://i6a5h9l1kl.execute-api.us-east-1.amazonaws.com/prod/check_version"


def pr(*vals):
    for f in files:
        if not isinstance(f, basestring):
            f.write(''.join(vals))

def prln(*vals):
    pr(*(vals + ('\n',)))

def trace(*vals):
    print(''.join(vals))

def traceln(*vals):
    trace(*(vals + ('\n',)))

def verbosetraceln(*vals):
    if quiet:
        return
    traceln(*vals)

def open_file(path):
    return open(path, 'w')

def set_tmp_dir():
    global tmpdir
    if tmpdir:
        return
    tmpdir = tempfile.mkdtemp()
    verbosetraceln("using temporary directory ", tmpdir)

def open_files(args):
    global tmpdir
    if args.out_file != "":
        set_tmp_dir()
        files.append(open_file(args.out_file))
        verbosetraceln("writing to file ", files[-1].name)
    elif args.call_home:
        set_tmp_dir()
        files.append(open_file(os.path.join(tmpdir, "data.json")))
    if args.tee:
        files.append(sys.stdout)

def upload_file(f_name, url):
    if not os.path.exists(f_name):
        return
    with open(f_name, 'rb') as f:
        r = requests.put(url, files={f_name: f})
    if r.status_code != 200:
        traceln("Failed uploading file:", f_name)

def getUUID():
    return str(uuid.uuid1())



def upload_s3(f, bucket, uuid=""):
    if uuid == "":
        uuid = getUUID()
        verbosetraceln("Using UUID ", uuid)
    base_name = os.path.basename(f)
    upload_file(f, 'https://' + bucket + '.s3.amazonaws.com/' + uuid + '/' + base_name)

def help(args):
    parser.print_help()

def close_files(args):
    global files
    if args.tee:
        files.pop()
    for f in files:
        if not isinstance(f, basestring):
            f.close()

def send_results(args):
    if args.call_home:
        uuid = args.uuid if args.uuid != "" else getUUID()
        verbosetraceln("Using UUID ", uuid)
        if crypt_file:
            upload_s3(crypt_file, TELEMETRICS, uuid)
        elif zip_file_name:
            upload_s3(zip_file_name, TELEMETRICS, uuid)
        else:
            for f in files:
                upload_s3(f if isinstance(f, basestring) else f.name, TELEMETRICS, uuid)

def clean_up(args):
    global files
    if args.keep:
        verbosetraceln("Not cleaning any files")
        return
    if  args.out_file != "":
        files.pop()
    if tmpdir:
        shutil.rmtree(tmpdir)

def sh_command(*args):
    try:
        p = subprocess.Popen(args, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
        out, err = p.communicate()
        if err:
            return err
        return out
    except:
        return str(sys.exc_info()[1])

def get_file(*path):
    paths = list(path)
    file = paths.pop()
    if not paths:
        paths.append("")
    for r in paths:
        if os.path.exists(os.path.join(r, file)):
            with open(os.path.join(r, file), 'r') as myfile:
                return myfile.read()
    return ""

def nodetool(*vals):
    return sh_command(*(['nodetool'] + list(vals)))

def get_distribution():
    res = {}
    for l in sh_command('lsb_release', '-a').split('\n'):
        vals = l.split(':', 1)
        if len(vals) > 1:
            res[vals[0].strip()] = vals[1].strip()
    return res

def get_json_from_url(path):
    req = urllib2.Request(path)
    try:
        response = urllib2.urlopen(req)
        return json.loads(response.read())
    except (urllib2.HTTPError, urllib2.URLError) as e:
        pass
    return ""

def get_api(path):
    return get_json_from_url("http://localhost:10000" + path)

def set_logs(res):
    global files
    if res['distribution']['Distributor ID'].lower() == 'ubuntu' and float(res['distribution']['Release']) < 15:
        files = files + ['/var/log/syslog', '/var/log/upstart/scylla-server.log']
    else:
        name = os.path.join(tmpdir, 'journal.txt')
        with open(name, 'a') as f:
            f.write(sh_command("journalctl", "_COMM=scylla", "--since", "2day ago", "-n", "10000"))
        files.append(name)

def zip_files(args):
    global zip_file_name
    if not args.zip and not args.encrypt:
        return
    uuid = getUUID()
    set_tmp_dir()
    zip_file_name = os.path.join(tmpdir, uuid + '.zip')
    zipf = zipfile.ZipFile(zip_file_name, 'w', zipfile.ZIP_DEFLATED)
    for f in files:
        name = f if isinstance(f, basestring) else f.name
        verbosetraceln("Adding file to archive", name)
        zipf.write(name)
    zipf.close()

def encrypt_file(args):
    global crypt_file
    if not args.encrypt:
        return
    set_tmp_dir()
    key_file = os.path.join(tmpdir, 'scylla_public_key.pem')
    with open(key_file, 'a') as f:
        f.write(scylla_public_key)
    crypt_file = zip_file_name + ".cryp"
    sh_command("openssl", "smime", "-encrypt", "-binary", "-aes-256-cbc", "-in", zip_file_name, "-out", crypt_file, "-outform", "DER", key_file)


def system(ar):
    res = {"help_version" : VERSION}
    res["distribution"] = get_distribution()
    res["rpms"] = sh_command("rpm", "-qa")
    set_logs(res)
    res["scylla.yaml"] = get_file("conf", "/var/lib/scylla/conf/", "scylla.yaml")
    res["disk"] = sh_command("df", "-k")
    res["cpuinfo"] = get_file("/proc/", "cpuinfo")
    res["meminfo"] = get_file("/proc/", "cpuinfo")
    prln(json.dumps(res))

def check_version(ar):
    current_version = get_api('/storage_service/scylla_release_version')
    latest_version = get_json_from_url(version_url)
    if current_version != latest_version:
        traceln("A new version was found, current version=", current_version, " latest version=", latest_version)


def scylla(ar):
    res = {"help_version" : VERSION}
    res["read_latency"] = get_api("/column_family/metrics/read_latency/moving_average_histogram/")
    res["write_latency"] = get_api("/column_family/metrics/write_latency/moving_average_histogram/")
    res["version"] = get_api('/storage_service/release_version')
    column_family = get_api("/column_family")
    res["column_family"] = column_family
    nt = {}
    for n in ["version", "describecluster"]:
        nt[n] = nodetool(n)
    cfhistograms = {}
    for cf in column_family:
        kcf = cf["ks"] + ":" + cf["cf"]
        cfhistograms[kcf] = nodetool("cfhistograms", cf["ks"], cf["cf"])
    nt["cfhistograms"] = cfhistograms
    res["nodetool"] = nt
    prln(json.dumps(res))

parser = argparse.ArgumentParser(description='ScyllDB help report tool', conflict_handler="resolve")
parser.add_argument('-ch', '--call-home', action='store_true', help='Upload the information to Scylla support server')
parser.add_argument('-k', '--keep', action='store_true', help='Keep any by product')
parser.add_argument('-o', '--out-file', default="", help='path to an output file')
parser.add_argument('-d', '--uuid', default="", help='The application personal UUID. If not set, a new one will be created')
parser.add_argument('--tee', action='store_true', help='Print to standard output')
parser.add_argument('-q', '--quiet', action='store_true', default=False, help='Quiet mode')
parser.add_argument('-z', '--zip', action='store_true', default=False, help='Zip the results before sending')
parser.add_argument('-e', '--encrypt', action='store_true', default=False, help='Zip and encrypt the results before sending')

subparsers = parser.add_subparsers(help='Available commands')
parser_help = subparsers.add_parser('help', help='Display help information')
parser_help.set_defaults(func=help)
parser_system = subparsers.add_parser('system', help='Collect general system information')
parser_system.set_defaults(func=system)
parser_system = subparsers.add_parser('scylla', help='Collect information about scylla runtime environment')
parser_system.set_defaults(func=scylla)
parser_system = subparsers.add_parser('version', help='Check if the current running version is the latest one')
parser_system.set_defaults(func=check_version)

args = parser.parse_args()
quiet = args.quiet
open_files(args)
args.func(args)
close_files(args)
zip_files(args)
encrypt_file(args)
send_results(args)
clean_up(args)
