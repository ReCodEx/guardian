#!/usr/bin/python3

import os
import sys
import xml.etree.ElementTree as ETree
sys.path.append('../../utils')
from basic_utils import run_in_container, build_path

sys.path.append("/home/simonkurz/.local/lib/python3.11/site-packages")
import matplotlib.pyplot as plt
from scipy import stats
import numpy as np
import re


def status():
    return 0
    #print(cg_memory_peak(results_tree("results.xml")))

def cg_memory_peak(results_tree: ETree):
    return int(results_tree.find('cg_total_mem_bytes').text)

def rusage_rss(results_tree: ETree):
    return results_tree.find('rusage_total_mem_bytes').text

def results_tree(results_xml):
    with open(results_xml) as f:
        xml = f.read()
        tree = ETree.fromstring(re.sub(r"(<\?xml[^>]+\?>)", r"\1<root>", xml) + "</root>")
    return tree

def allocation_test(exec: os.PathLike, alloc: int, results_xml : os.PathLike = "test_run.xml"):
    cntnr_config = f"--stats-xml=\"{results_xml}\" --task-cg=\"test\""
    run_in_container(f"{build_path}/{exec}", config_args=cntnr_config, exec_args=str(alloc))
    return results_tree(results_xml)

class MemStats:
  def __init__(self, allocated, runs, mean, std, delta):
    self.allocated = allocated
    self.runs = runs
    self.mean = mean
    self.std = std
    self.delta = delta

def readable_memstats(stats: MemStats):
    return f"""{stats.runs} runs of allocating {stats.allocated} bytes:
            CGROUP ACCOUNTING:
                mean: {stats.mean} B
                std:  {stats.std} B
                delta between first two runs: {stats.delta} B
                """

def allocation_statistics(test: os.PathLike, alloc: int, runs: int):
    cg_values = []
    for i in range(runs):
        cg_mem_peak = cg_memory_peak(allocation_test(test, alloc))
        cg_values.append(cg_mem_peak)
    return MemStats(alloc, runs, np.mean(cg_values), np.std(cg_values), cg_values[1] - cg_values[0])

        




def main():
    print(readable_memstats(allocation_statistics("memory_test_fuzzy", 1000000, 10)))

if __name__ == '__main__':
    main()



