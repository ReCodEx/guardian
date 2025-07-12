#!/usr/bin/env python3
import subprocess
import yaml
import os
import sys
from pathlib import Path
import copy
import shutil

SCRIPT_DIR = Path(__file__).resolve().parent
CONTAINER_BIN = f"{SCRIPT_DIR}/../../build/src/container"
LIMITS_TEST_CONFIG = f"{SCRIPT_DIR}/resource_limits_test.yml"
ISOLATION_TEST_CONFIG = f"{SCRIPT_DIR}/isolation_tests.yml"

def cleanup_results():
    """Clean up result files from the res directory"""
    res_dir = SCRIPT_DIR / "res"
    if res_dir.exists():
        shutil.rmtree(res_dir)

def setup_dirs():
    """Set up required test directories"""
    res_dir = SCRIPT_DIR / "res"
    res_dir.mkdir(exist_ok=True)

def build():
    try:
        # Build test binaries
        subprocess.run(['cmake', '--build', f"{SCRIPT_DIR}/../../build"], check=True)
    except subprocess.CalledProcessError as e:
        print(f"Failed to build test binaries: {e}")
        return False

def run_isolation_test(capture_output=True):
    print("\n===== ISOLATION TESTS =====")
    try:
        # Run tests
        cmd = ["sudo", CONTAINER_BIN, f"--yaml={ISOLATION_TEST_CONFIG}"]
        result = subprocess.run(cmd, capture_output=capture_output, text=True)
        
        # Load config to get task IDs
        with open(ISOLATION_TEST_CONFIG) as f:
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


def run_test_without_limits(config, capture_output=True):
    # Create a copy of config without resource limits
    config_no_limits = copy.deepcopy(config)
    for task in config_no_limits['tasks']:
        if 'limits' in task:
            del task['limits']
        # Add a suffix to differentiate result files
        task['stats-yaml'] = task['stats-yaml'].replace('.result.yaml', '.no-limits.result.yaml')

    # Write temporary config
    no_limits_config = f"{SCRIPT_DIR}/resource_limits_test_no_limits.yml"
    with open(no_limits_config, 'w') as f:
        yaml.dump(config_no_limits, f)

    try:
        # Run without limits
        cmd = ["sudo", CONTAINER_BIN, f"--yaml={no_limits_config}"]
        result = subprocess.run(cmd, capture_output=capture_output, text=True)
        
        # Check results
        for task in config_no_limits['tasks']:
            task_id = task['task-id']
            result_file = f"{SCRIPT_DIR}/res/{task_id}.no-limits.result.yaml"
            try:
                with open(result_file) as f:
                    task_result = yaml.safe_load(f)
                    status = task_result.get('status', 'unknown')
                    if status != 'OK':
                        print(f"⚠️  {task_id}: Failed without resource limits (status: {status})")
                        return False
                    else:
                        print(f"✅ {task_id}: Passed without resource limits")
            except FileNotFoundError:
                print(f"❌ {task_id}: No result file found for unlimited run")
                return False
    finally:
        # Clean up temporary config
        try:
            os.remove(no_limits_config)
        except:
            pass
    return True

def run_limits_test(capture_output=True):
    print("\n===== RESOURCE LIMITS TESTS =====")
    # First make sure the test binaries are built
    try:
        # Load config
        with open(LIMITS_TEST_CONFIG) as f:
            config = yaml.safe_load(f)

        # First run tests without limits
        print("Running tests without resource limits...")
        if not run_test_without_limits(config):
            print("Tests failed without resource limits!")
            return False

        print("\nRunning tests with resource limits...")
        # Run the container with our test configuration
        cmd = ["sudo", CONTAINER_BIN, f"--yaml={LIMITS_TEST_CONFIG}"]
        result = subprocess.run(cmd, capture_output=capture_output, text=True)
        
        passed_tests = []
        failed_tests = []
        
        for task in config['tasks']:
            task_id = task['task-id']
            result_file = f"{SCRIPT_DIR}/res/{task_id}.result.yaml"
            try:
                with open(f"{result_file}") as f:
                    task_result = yaml.safe_load(f)
                    status = task_result.get('status', 'unknown')
                    
                    # For these tests, we expect them to fail due to resource limits
                    if status == 'OK':
                        failed_tests.append(task_id)
                        print(f"❌ {task_id}: Got status 'OK', expected to fail")
                    else:
                        passed_tests.append(task_id)
                        print(f"✅ {task_id}: Failed as expected with status '{status}'")
                        
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
    # Initialize and clean up system before running tests
    build()
    subprocess.run([f"{SCRIPT_DIR}/../../scripts/cleanup_system.sh"], shell=True)
    subprocess.run([f"{SCRIPT_DIR}/../../scripts/initialize_system.sh"], shell=True)
    res_dir = SCRIPT_DIR / "res"
    res_dir.mkdir(exist_ok=True)
    try:
        # Create res directory before tests
        run_isolation_test()
        run_limits_test()
    finally:
        # Clean up result files regardless of test outcome
        cleanup_results()