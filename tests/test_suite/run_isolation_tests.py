#!/usr/bin/env python3
import subprocess
import yaml
import os
import sys
from pathlib import Path
import shutil

SCRIPT_DIR = Path(__file__).resolve().parent
CONTAINER_BIN = f"{SCRIPT_DIR}/../../build/src/container"
TEST_CONFIG = f"{SCRIPT_DIR}/isolation_tests.yml"

def setup_dirs():
    """Set up required test directories"""
    res_dir = SCRIPT_DIR / "res"
    res_dir.mkdir(exist_ok=True)

def cleanup():
    """Clean up test directories"""
    res_dir = SCRIPT_DIR / "res"
    if res_dir.exists():
        shutil.rmtree(res_dir)
    allowed_dir = Path("/tmp/allowed")
    if allowed_dir.exists():
        shutil.rmtree(allowed_dir)

def run_isolation_test():
    try:
        # Build test binaries
        subprocess.run(['cmake', '--build', f"{SCRIPT_DIR}/../../build"], check=True)
    except subprocess.CalledProcessError as e:
        print(f"Failed to build test binaries: {e}")
        return False

    try:
        # Run tests
        cmd = ["sudo", CONTAINER_BIN, f"--yaml={TEST_CONFIG}"]
        result = subprocess.run(cmd, text=True)
        
        # Load config to get task IDs
        with open(TEST_CONFIG) as f:
            config = yaml.safe_load(f)
            
        passed_tests = []
        failed_tests = []
        
        # Check results for each task
        for task in config['tasks']:
            task_id = task['task-id']
            result_file = f"{SCRIPT_DIR}/res/{task_id}.result.yaml"
            try:
                with open(result_file) as f:
                    task_result = yaml.safe_load(f)
                    status = task_result.get('status', 'unknown')
                    if status != 'OK':
                        failed_tests.append(task_id)
                        print(f"❌ {task_id}: Failed with status '{status}'")
                    else:
                        passed_tests.append(task_id)
                        print(f"✅ {task_id}: Passed")
            except FileNotFoundError:
                failed_tests.append(task_id)
                print(f"❌ {task_id}: No result file found")
                
    except Exception as e:
        print(f"Error running tests: {e}")
        return False
        
    print("\nTest Summary:")
    print(f"Passed: {len(passed_tests)} tests")
    print(f"Failed: {len(failed_tests)} tests")
    if failed_tests:
        print("Failed tests:", ", ".join(failed_tests))
        
    return len(failed_tests) == 0

if __name__ == "__main__":
    # Initialize system
    subprocess.run([f"{SCRIPT_DIR}/../../scripts/cleanup_system.sh"], shell=True)
    subprocess.run([f"{SCRIPT_DIR}/../../scripts/initialize_system.sh"], shell=True)
    try:
        # Create directories
        setup_dirs()
        success = run_isolation_test()
    finally:
        # Clean up
        cleanup()
    sys.exit(0 if success else 1)