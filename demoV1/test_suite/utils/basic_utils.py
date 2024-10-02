#!/usr/bin/python3

import os
import sys
import xml.etree.ElementTree as ETree



cg_fs_path = "/sys/fs/cgroup"

repo_path = "/home/simonkurz/mff/rcdx_cntnr/demoV1"
build_path = f"{repo_path}/src/build"
cntnr_path = f"{build_path}/rcdx_cntnr_demo"


def testing_cg():
    return ETree.parse("test_config.xml").find()

def run_in_container(exec, config_xml = None, config_args = None, exec_args = None):
    command = f"{cntnr_path} --path={exec}"
    
    if config_xml != None:  command += f" --f={config_xml}"
    if config_args != None: command += f" {config_args}"
    if exec_args != None:   command += f" --args={exec_args}"

    print(f"calling: \"{command}\"")
        
    os.system(command)

def create_cgroup(cg_rel_path):
    path = os.path.join(cg_fs_path, cg_rel_path)
    delete_cgroup(cg_rel_path)
    os.mkdir(path)

def delete_cgroup(cg_rel_path):
    path = os.path.join(cg_fs_path, cg_rel_path)
    if os.path.exists(path):
        os.rmdir(path)

def test():
    run_in_container(f"{build_path}/memory_test_fuzzy", config_args="--task-cg=test", exec_args="1000000")

if __name__ == '__main__':
    test()

