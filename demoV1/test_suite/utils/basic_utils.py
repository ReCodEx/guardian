#!/usr/bin/python3

import os
import sys
import xml.etree.ElementTree as ETree

cg_fs_path = "/sys/fs/cgroup"
cntnr_path = "../src/build/rcdx_cntntr_demo"


def testing_cg():
    return ETree.parse("test_config.xml").find()

def run_in_container(exec, config_xml = None, config_args = None, exec_args = None):
    command = f"{cntnr_path} --path=\"{exec}\""
    
    if config_xml != None:  command += f"--f={config_xml} "
    if config_args != None: command += config_args
    if exec_args != None:   command += f" --args={exec_args} "
        
    os.system(command)

def create_cgroup(cg_rel_path):
    path = os.path.join(cg_fs_path, cg_rel_path)
    delete_cgroup(cg_rel_path)
    os.mkdir(path)

def delete_cgroup(cg_rel_path):
    path = os.path.join(cg_fs_path, cg_rel_path)
    if os.path.exists(path):
        os.rmdir(path)

