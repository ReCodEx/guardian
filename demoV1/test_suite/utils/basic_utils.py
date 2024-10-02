#!/usr/bin/python3

import os
import sys
import xml.etree.ElementTree as ETree
import et_utils


cg_fs_path = "/sys/fs/cgroup"

repo_path = "/home/simonkurz/mff/rcdx_cntnr/demoV1"
build_path = f"{repo_path}/src/build"
cntnr_path = f"{build_path}/rcdx_cntnr_demo"

def run_in_container(exec, config_xml = None, config_args = None, exec_args = None):
    command = f"{cntnr_path} --path={exec}"
    
    if config_xml != None:  command += f" --f={config_xml}"
    if config_args != None: command += f" {config_args}"
    if exec_args != None:   command += f" --args={exec_args}"

    print(f"calling: \"{command}\"")
        
    os.system(command)

def allocation_test(exec: os.PathLike, alloc: int, results_xml : os.PathLike = "test_run.xml"):
    cntnr_config = f"--stats-xml=\"{results_xml}\" --task-cg=\"test\""
    run_in_container(f"{build_path}/{exec}", config_args=cntnr_config, exec_args=str(alloc))
    return et_utils.results_tree(results_xml)

def test():
    run_in_container(f"{build_path}/memory_test_fuzzy", config_args="--task-cg=test", exec_args="1000000")

if __name__ == '__main__':
    test()

