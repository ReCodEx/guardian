#!/usr/bin/python3

import os
import sys

cg_fs_path = "/sys/fs/cgroup"

def run_contained_executable(cntnr_path, exec, config_xml = None, config_args = None):
    command = f"{cntnr_path} --path=\"{exec}\""
    
    if config_xml != None:  command += f"--f={config_xml}"
    if config_args != None: command += config_args
        
    os.system(command)

def create_cgroup(cg_rel_path):
    path = os.path.join(cg_fs_path, cg_rel_path)
    delete_cgroup(cg_rel_path)
    os.mkdir(path)

def delete_cgroup(cg_rel_path):
    path = os.path.join(cg_fs_path, cg_rel_path)
    if os.path.exists(path):
        os.rmdir(path)

