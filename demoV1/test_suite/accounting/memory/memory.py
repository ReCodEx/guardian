#!/usr/bin/python3

import os
import sys
import xml.etree.ElementTree as ETree
from ...basic_utils import run_in_container, testing_cg

sys.path.append("/home/simonkurz/.local/lib/python3.11/site-packages")
import matplotlib.pyplot as plt
from scipy import stats
import numpy as np

def status():
    return 0

def cg_memory_peak(results_xml):
    return 0

def rusage_rss(results_xml):
    return 0

def results_tree(results_xml):
    return ETree.parse(results_xml)

#TODO: config.xml support in the container
def allocation_test(exec, alloc, results_xml, config_xml = None):
    run_in_container(exec, config_xml, exec_args=f"{alloc}")
    return results_tree(results_xml)
