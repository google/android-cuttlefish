#!/usr/bin/env python3

import re
import subprocess

def parse_debian_control(filename):
    data = {}
    current_section = None
    with open(filename, 'r') as f:
        key = ""
        for line in f:
            if not line or line.startswith('#'):
                continue
            if line.startswith(' ') or line.startswith('\t'):
                line = line.strip()
                data[key] = data[key] + line
            else:
                data1 = line.split(':', 1)
                if (len(data1) == 2):
                    key = data1[0].strip()
                    value = data1[1].strip()
                    data[key] = value
    return data

def get_arch():
    arch = None
    try:
        arch = subprocess.check_output(
            ['dpkg', '--print-architecture'],
            encoding='utf-8'
        ).strip()
    except:
        pass
    return arch

if __name__ == '__main__':
    config = parse_debian_control("debian/control")
    apt_command = ["apt", "-o", "Apt::Get::Assume-Yes=true", "-o", "APT::Color=0", "-o", "DPkgPM::Progress-Fancy=0", "install"]

    for i in ['Build-Depends', 'Build-Depends-Arch', 'Build-Depends-Indep']:
        if (not (i in config)):
            continue
        packages = config[i]
        packages = packages.split(",")
        for package in packages:
            # package (version) <...>
            m1 = re.match(r"([^(\s]*)\s*([(][^)]*[)])\s*(?:<(?:.*)>)?", package);
            # package (version) [arch] <...>
            m2 = re.match(r"([^[(\s]*)\s*(?:[(][^)]*[)])?\s*\[(.*)\]\s*(?:<(?:.*)>)?", package);
            # package <...>
            m3 = re.match(r"([^[(\s]*)\s*(?:[(][^)]*[)])?\s*(?:<(?:.*)>)?", package);
            if (m1):
                print("Install %s"%(m1.group(1)))
                apt_command_1 = apt_command[:]
                apt_command_1.append(m1.group(1))
                subprocess.run(apt_command_1)
            elif (m2):
                if (get_arch() in m2.group(2).split()):
                    print("Install %s for %s"%(m2.group(1), m2.group(2)))
                    apt_command_1 = apt_command[:]
                    apt_command_1.append(m2.group(1))
                    subprocess.run(apt_command_1)
                else:
                    print("Not install %s because architecture not match (%s)."%(m2.group(1), m2.group(2)))
            elif (m3):
                print("Install %s"%(m3.group(1)))
                apt_command_1 = apt_command[:]
                apt_command_1.append(m3.group(1))
                subprocess.run(apt_command_1)
            else:
                print("Install %s"%(package))
                apt_command_1 = apt_command[:]
                apt_command_1.append(package)
                subprocess.run(apt_command_1)
        pass
