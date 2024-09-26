#!/usr/bin/python3

import os
import sys
import xml.etree.ElementTree as ETree
sys.path.append('../../utils')
from basic_utils import run_in_container, testing_cg

sys.path.append("/home/simonkurz/.local/lib/python3.11/site-packages")
import matplotlib.pyplot as plt
from scipy import stats
import numpy as np

build_path = '../../../src/build'

def status():
    return 0

def cg_memory_peak(results_tree: ETree):
    return results_tree.cg_total_mem_bytes

def rusage_rss(results_tree: ETree):
    return results_tree.get('rusage_total_mem_bytes')

def results_tree(results_xml):
    return ETree.parse(results_xml)

def allocation_test(exec: os.PathLike, alloc: int, results_xml : os.PathLike, config_xml: os.PathLike = None):
    cntntr_config = f"--stats-xml=\"{results_xml}\" --task-cg=\"alloc_test\""
    run_in_container(exec, config_args=cntntr_config, exec_args=f"{alloc}")
    return results_tree(f"{build_path}/{results_xml}")

def allocation_statistics():
    return 0




def main():
    tree = allocation_test(build_path + '/memory_test_fuzzy', 10_000_000, 'results.xml')
    print(cg_memory_peak(tree))

if __name__ == '__main__':
    #main()
    print(cg_memory_peak(results_tree("results.xml")))



