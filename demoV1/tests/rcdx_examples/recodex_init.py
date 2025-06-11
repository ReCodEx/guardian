#!/usr/bin/env python3
import subprocess
import os

def run_build_script(current_dir):
    build_script = os.path.join(current_dir, "..", "..", "scripts", "build.sh")
    try:
        subprocess.run(["bash", build_script], check=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error running build.sh: {e}")
        return False

def build_recodex_judge(current_dir):
    worker_dir = os.path.join(current_dir, "worker")
    try:
        if not os.path.exists(worker_dir):
            subprocess.run(["git", "clone", "https://github.com/ReCodEx/worker.git", worker_dir], check=True)
        
        build_dir = os.path.join(worker_dir, "judges", "build")
        os.makedirs(build_dir, exist_ok=True)
        
        subprocess.run(["cmake", ".."], cwd=build_dir, check=True)
        subprocess.run(["cmake", "--build", ".", "-t", "recodex-token-judge"], cwd=build_dir, check=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error building ReCodEx worker: {e}")
        return False

def install_pandas():
    try:
        subprocess.run(["pip3", "install", "pandas"], check=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error installing pandas: {e}")
        return False

def install_dotnet(current_dir):
    if not os.path.exists("/opt/dotnet/dotnet"):
        dotnet_install = os.path.join(current_dir, "dotnet-install.sh")
        try:
            subprocess.run(["sudo", dotnet_install], check=True)
            return True
        except subprocess.CalledProcessError as e:
            print(f"Error running dotnet-install.sh: {e}")
            return False
    return True

def download_test_data(current_dir):
    test_data_dir = os.path.join(current_dir, "test-data")
    if not os.path.exists(os.path.join(test_data_dir, "download")):
        try:
            subprocess.run(["wget", "https://www.ksi.mff.cuni.cz/~krulis/download/kurz/kurz-test-data.zip"], cwd=current_dir, check=True)
            subprocess.run(["unzip", "kurz-test-data.zip", "-d", test_data_dir], cwd=current_dir, check=True)
            os.remove(os.path.join(current_dir, "kurz-test-data.zip"))
            return True
        except subprocess.CalledProcessError as e:
            print(f"Error downloading/extracting test data: {e}")
            return False
    return True

def run_scripts():
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    tasks = [
        lambda: run_build_script(current_dir),
        lambda: build_recodex_judge(current_dir),
        lambda: install_pandas(),
        lambda: install_dotnet(current_dir),
        lambda: download_test_data(current_dir)
    ]
    
    return all(task() for task in tasks)

if __name__ == "__main__":
    success = run_scripts()
    exit(0 if success else 1)