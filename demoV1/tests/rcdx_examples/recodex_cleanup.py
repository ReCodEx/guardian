#!/usr/bin/env python3
import subprocess
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

def remove_dotnet():
    try:
        # Check if running with sudo/root privileges
        if os.geteuid() != 0:
            print("This script must be run with sudo privileges")
            sys.exit(1)
            
        # Remove /opt/dotnet directory
        subprocess.run(["rm", "-rf", "/opt/dotnet"], check=True)
        print("Successfully removed /opt/dotnet directory")
        
    except subprocess.CalledProcessError as e:
        print(f"Error removing directory: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}")
        sys.exit(1)

def remove_test_data():
    try:
        if os.geteuid() != 0:
            print("This script must be run with sudo privileges")
            sys.exit(1)
            
        test_data_path = os.path.join(SCRIPT_DIR, "test-data")
        subprocess.run(["rm", "-rf", test_data_path], check=True)
        print("Successfully removed test-data directory")
        
    except subprocess.CalledProcessError as e:
        print(f"Error removing directory: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}")
        sys.exit(1)
        
def remove_worker():
    try:
        if os.geteuid() != 0:
            print("This script must be run with sudo privileges")
            sys.exit(1)
            
        worker_path = os.path.join(SCRIPT_DIR, "worker")
        subprocess.run(["rm", "-rf", worker_path], check=True)
        print("Successfully removed worker directory")
        
    except subprocess.CalledProcessError as e:
        print(f"Error removing directory: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}")
        sys.exit(1)
        
def remove_pandas():
    try:
        if os.geteuid() != 0:
            print("This script must be run with sudo privileges")
            sys.exit(1)
            
        subprocess.run(["pip3", "uninstall", "-y", "pandas"], check=True)
        print("Successfully uninstalled pandas")
        
    except subprocess.CalledProcessError as e:
        print(f"Error uninstalling pandas: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    remove_dotnet()
    remove_test_data()
    remove_worker()
    # remove_pandas()
    print("Cleanup of ReCoDex specific directories completed successfully.")