#!/bin/python3
import sys
import subprocess
import yaml
import os
import pathlib
from pathlib import Path
import re

SCRIPT_DIR = Path(__file__).resolve().parent
ISOLATE_SANDBOX = f"{SCRIPT_DIR}/../../build/src/container"
CWD = os.getcwd()

MAVEN_REPO = "/opt/maven-repo"

dirs = {
    "ISOLATE_CONFIG": "isolate_config.yml",
    "SOURCE_DIR": f"{CWD}/{sys.argv[1]}",
    "EVAL_DIR": ".",
    "RESULT_DIR": f"{CWD}/results/{sys.argv[1]}",
    "JUDGES_DIR": f"worker/judges/build",
}

print(f"Directories: {dirs}")

def find_java_home():
    try:
        javac_path = subprocess.check_output(['which', 'javac'], text=True).strip()
        real_javac = pathlib.Path(javac_path).resolve()
        java_home = real_javac.parents[1]  # usually the grandparent of 'bin/javac'
        return str(java_home)
    except subprocess.CalledProcessError:
        return None

def mvn_init():
    if not os.path.exists(f"{MAVEN_REPO}/.m2"):
        print("Initializing Maven repository with an online compilation")
        os.makedirs(f"{MAVEN_REPO}/.m2", exist_ok=True)
        subprocess.run([f"{SCRIPT_DIR}/recodex_mock.py", "mvn_init_job"])

def get_container_path():
    paths = set()

    # 1. Start with current system PATH
    paths.update(os.environ.get("PATH", "").split(":"))

    # 2. Add standard system paths explicitly (to avoid stripping them later)
    paths.update(["/bin", "/usr/bin", "/usr/local/bin", "/sbin", "/usr/sbin"])

    if os.path.exists("/usr/libexec/gcc"):
        for root, dirs, files in os.walk("/usr/libexec/gcc"):
            paths.add(root + '/')

    return ":".join(sorted(paths))        

def get_ld_library_path():
    paths = set()

    # 1. Standard lib paths
    standard_paths = ["/lib", "/lib64", "/usr/lib", "/usr/lib64"]
    paths.update(standard_paths)

    if os.path.exists("/usr/libexec/gcc"):
        for root, dirs, files in os.walk("/usr/libexec/gcc"):
            paths.add(root + '/')
    paths.add("/usr/libexec/gcc/x86_64-redhat-linux/11/liblto_plugin.so")

    # 2. GCC internal paths
    try:
        out = subprocess.check_output(["gcc", "-print-search-dirs"], text=True)
        for line in out.splitlines():
            if line.startswith("libraries: ="):
                libs = line.split("=", 1)[1].split(":")
                paths.update(libs)
    except subprocess.CalledProcessError:
        pass

    # 3. Path to libstdc++
    try:
        libstdcpp = subprocess.check_output(["gcc", "-print-file-name=libstdc++.so"], text=True).strip()
        if os.path.isfile(libstdcpp):
            paths.add(os.path.dirname(libstdcpp))
    except subprocess.CalledProcessError:
        pass

    # 4. Remove duplicates and empty entries
    return ":".join(sorted(p for p in paths if p))

print("LD_LIBRARY_PATH=" + get_ld_library_path())
print("PATH=" + get_container_path())
print("JAVA_HOME=" + find_java_home())

        
def substitute_variables(obj, variables):
    """Recursively substitute ${VAR} in strings of a nested structure."""
    if isinstance(obj, dict):
        return {k: substitute_variables(v, variables) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [substitute_variables(item, variables) for item in obj]
    elif isinstance(obj, str):
        return re.sub(r"\$\{([^}]+)\}", lambda m: variables.get(m.group(1), m.group(0)), obj)
    else:
        return obj

def run_task(task, results):
    cmd = task.get('cmd')
    if not cmd:
        print(f"Task {task['task-id']} has no command.")
        return

    bin_path = cmd['bin']
    args = cmd.get('args', [])

    if bin_path == 'dumpdir':
        bin_path = os.path.join(SCRIPT_DIR, 'dumpdir')
        
    elif bin_path == 'extract':
        bin_path = os.path.join(SCRIPT_DIR, 'extract')
    elif bin_path == 'exists':
        bin_path = os.path.join(SCRIPT_DIR, 'exists')
        
    elif bin_path == 'fetch':
        bin_path = 'cp'
        src = os.path.join(dirs['SOURCE_DIR'], f"{args[0]}")
        dst = args[1]
        args = [src, dst]
        
    elif "g++" in bin_path:
        bin_path = subprocess.getoutput("which g++")
    
    elif "gcc" in bin_path:
        bin_path = subprocess.getoutput("which gcc")
        
    elif "maven" in bin_path:
        mvn_init()
        bin_path = subprocess.getoutput("which mvn")
        
    elif "token-judge" in bin_path:
        bin_path = os.path.join(f"/{dirs['JUDGES_DIR']}", 'recodex_token_judge/recodex-token-judge')

    full_cmd = [bin_path] + args
    full_cmd = [str(arg) for arg in full_cmd]
    sandbox = task.get('sandbox')
    task_id = task.get('task-id')

    if (sandbox and sandbox.get('name') == 'isolate'):
        print(f"Running in isolate sandbox: {task['task-id']}")
        
        config_file = os.path.join(dirs['SOURCE_DIR'], task_id + ".yml") 
        workdir = sandbox['working-directory']
        sandbox_res_in = f"{workdir}/{task['task-id']}.result.yml"
        sandbox_res_out = f"{dirs['SOURCE_DIR']}/{sandbox_res_in}"
        with open(f"{SCRIPT_DIR}/{dirs['ISOLATE_CONFIG']}", 'r') as f:
            config_data = yaml.safe_load(f)

            if('share-net' in sandbox):
                config_data['share-net'] = sandbox['share-net']
            if('as-uid' in sandbox):
                config_data['as-uid'] = sandbox['as-uid']
            if('as-gid' in sandbox):
                config_data['as-gid'] = sandbox['as-gid']

            config_data.setdefault('tasks', [])
            config_data['tasks'].append(sandbox)
            config_data['tasks'][-1]['task-id'] = task_id
            config_data['tasks'][-1].setdefault('cmd', {})['bin'] = bin_path
            config_data['tasks'][-1]['cmd']['args'] = args
            config_data['tasks'][-1]['stats-yaml'] = f"{sandbox_res_in}"
            config_data['env']['vars'].append(f"LD_LIBRARY_PATH={get_ld_library_path()}")
            config_data['env']['vars'].append(f"PATH={get_container_path()}")
            config_data['env']['vars'].append(f"JAVA_HOME={find_java_home()}")
            config_data['env']['vars'].append(f"HOME=/{workdir}")
            config_data['box-fs']['dir-rules'].append(f"{dirs['JUDGES_DIR']}={SCRIPT_DIR}/{dirs['JUDGES_DIR']}")
            if 'box-fs' in sandbox:
                fs = sandbox['box-fs']
                if 'dir-rules' in fs:
                    rules = fs['dir-rules']
                    for rule in rules:
                        print(f"Adding box-fs rule: {rule}")
                        config_data['box-fs']['dir-rules'].append(rule)

            config_data['box-fs']['dir-rules'].append(f"{workdir}={dirs['SOURCE_DIR']}/{workdir}:rw")
            config_data['tasks'][-1]['chdir'] = workdir

            # Write modified YAML back to file
            with open(config_file, 'w') as f:
                yaml.dump(config_data, f)
                
        
            

        # os.makedirs(workdir, exist_ok=True)
        try:
                
            isolate_cmd = [ISOLATE_SANDBOX] + [f"--yaml={config_file}"]

            print(f"Running isolate command: {isolate_cmd}")
            result = subprocess.run(isolate_cmd, capture_output=True, text=True)
            print(result.stdout)
            taskresults = {}
            # print(f"sandbox_res: {sandbox_res}")
            with open(sandbox_res_out, 'r') as f:
                taskresults['sandbox_results'] = yaml.safe_load(f)
            if result.returncode == 0:
                taskresults["status"] = "OK"
                taskresults["task-id"] = task_id
            if result.stderr:
                print(result.stderr)
            results['results'].append(taskresults)
            
        except Exception as e:
            print(f"Exception while running the isolator command: {e}")
    else:
        print(f"Running normal command: {task['task-id']}")
        try:
            result = subprocess.run(full_cmd, capture_output=True, text=True)
            taskresults = {}
            if result.returncode == 0:
                taskresults["task-id"] = task_id
                taskresults["status"] = "OK"
                
            print(result.stdout)
            if result.stderr:
                print(result.stderr)
            results['results'].append(taskresults)
        except Exception as e:
            print(f"Exception while running the command: {e}")

def main():
    job_configs = ["job-config.yml", "job.yaml", "job.yml"]
    job_config = None

    for config in job_configs:
        if os.path.exists(f"{dirs['SOURCE_DIR']}/{config}"):
            job_config = config
            break

    if job_config is None:
        print(f"No job config file found in {dirs['SOURCE_DIR']}/. Tried: {', '.join(job_configs)}")
        sys.exit(1)

    with open(f"{dirs['SOURCE_DIR']}/{job_config}") as f:
        data = yaml.safe_load(f)
    
    data = substitute_variables(data, dirs)

    tasks = data.get('tasks', [])
    results = {}
    results.setdefault('results', [])
    for task in tasks:
        print(f"--- Running {task['task-id']} ---")
        run_task(task,results)
    with open(f"{dirs['SOURCE_DIR']}/my_results.yml", 'w') as f:
        yaml.dump(results, f)

if __name__ == "__main__":
    main()
