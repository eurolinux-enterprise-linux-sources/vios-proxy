#!/bin/bash
#
# Compile .pod files into .1.gz files in parent directory.
# From there an install script can move them to /usr/share/man/man1
#
pod2man -c vios-proxy-host  -n vios-proxy-host  -d 0.1 vios-proxy-host.pod  > vios-proxy-host.1
pod2man -c vios-proxy-guest -n vios-proxy-guest -d 0.1 vios-proxy-guest.pod > vios-proxy-guest.1

gzip vios-proxy-host.1
gzip vios-proxy-guest.1

mv *.1.gz ../