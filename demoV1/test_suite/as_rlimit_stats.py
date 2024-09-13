#!/usr/bin/python3


import os
import sys


sys.path.append("/home/simonkurz/.local/lib/python3.11/site-packages")
import matplotlib.pyplot as plt
from scipy import stats
import numpy as np

cntnr_path = "../src/build/rcdx_cntnr_demo"

def run_memory_test(exec, limit, alloc):
    print(f"calling {cntnr_path} --path=\"{exec}\" --mem={str(limit)} --args=\"{str(alloc)}\"")
    os.system(f"{cntnr_path} --path=\"{exec}\" --mem={str(limit)} --args=\"{str(alloc)}\"")
    return check_report()

def check_report():
    with open("/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/logs/task_report.txt") as file:
        status = file.readline()
        return status == "OK"

def single_allocation_test():
    memory_test_statistics
    return 0

def fuzzy_allocation_test():
    return 0

def memory_test_statistics(exec):
    limits = range(1024*1024, 1024*1024*1024, 1024)
    data = []
    for limit in limits:
        alloc = 0
        step = limit // 1024
        while(True):
            alloc += step
            survived = run_memory_test(exec, limit, alloc)
            if not survived: break
            
        data.append(alloc - step)
    plt.scatter(data, limits)
    plt.show() 


limit = 10000000
alloc = 10000000
exec = "/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/memory_test"
#run_memory_test(exec, limit, alloc)
#print(check_report())
#os.system("/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/memory_test 10000")

memory_test_statistics(exec)




