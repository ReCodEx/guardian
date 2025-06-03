#~/bin/python3
import sys
import subprocess
import yaml
import os
from pathlib import Path
import re

SCRIPT_DIR = Path(__file__).resolve().parent
ISOLATE_SANDBOX = f"{SCRIPT_DIR}/../../build/src/container"
CWD = os.getcwd()

MAVEN_REPO = "/opt/maven-repo"
GCC_LD_LIBRARY_PATH = "/usr/lib/gcc/x86_64-redhat-linux/14/include:/usr/lib/gcc/x86_64-redhat-linux/14/"

variables = {
    "ISOLATE_CONFIG": "isolate_config.yml",
    "SOURCE_DIR": f"{CWD}/{sys.argv[1]}",
    "EVAL_DIR": ".",
    "RESULT_DIR": f"{CWD}/results/{sys.argv[1]}",
    "JUDGES_DIR": f"worker/judges/build",
}

print(f"Variables: {variables}")

def mvn_init():
    if not os.path.exists(f"{MAVEN_REPO}/.m2"):
        print("Initializing Maven repository...")
        os.makedirs(f"{MAVEN_REPO}/.m2", exist_ok=True)
        subprocess.run(["mvn", "-s", "mvn_settings.xml", "dependency:go-offline"])
        
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
        src = os.path.join(variables['SOURCE_DIR'], f"fetch/{args[0]}")
        dst = args[1]
        args = [src, dst]
    
    elif "gcc" in bin_path:
        bin_path = subprocess.getoutput("which gcc")
        ### REPLACED BY CORRECT LD_LIBRARY_PATH
        # args.insert(0, "-B/usr/libexec/gcc/x86_64-redhat-linux/14/")
        # args.insert(0, "-B/usr/lib/gcc/x86_64-redhat-linux/14/")
        # args.insert(0, "-I/usr/lib/gcc/x86_64-redhat-linux/14/include/")
        
    elif "maven" in bin_path:
        mvn_init()
        bin_path = subprocess.getoutput("which mvn")
        
    elif "token-judge" in bin_path:
        bin_path = os.path.join(f"/{variables['JUDGES_DIR']}", 'recodex_token_judge/recodex-token-judge')

    full_cmd = [bin_path] + args
    full_cmd = [str(arg) for arg in full_cmd]
    sandbox = task.get('sandbox')
    task_id = task.get('task-id')

    if (sandbox and sandbox.get('name') == 'isolate'):
        print(f"Running in isolate sandbox: {task['task-id']}")
        # workdir = os.path.join(ISOLATE_SANDBOX, sandbox.get('working-directory', '.'))
        # Find the yaml file from args
        for i, arg in enumerate(args):
            if arg.startswith('--yaml='):
                variables["ISOLATE_CONFIG"] = arg.split('=')[1]
            break
        
        config_file = os.path.join(variables['SOURCE_DIR'], task_id + ".yml") 
        workdir = None
        with open(variables['ISOLATE_CONFIG'], 'r') as f:
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
            config_data['env']['vars'].append(f"LD_LIBRARY_PATH={GCC_LD_LIBRARY_PATH}:/usr/lib64:/usr/lib:/lib64:/lib")
            config_data['box-fs']['dir-rules'].append(f"{variables['JUDGES_DIR']}={CWD}/{variables['JUDGES_DIR']}")
            if 'working-directory' in sandbox:
                workdir = sandbox['working-directory']
                config_data['box-fs']['dir-rules'].append(f"{workdir}={variables['SOURCE_DIR']}/{workdir}:rw")
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
            sandbox_res = f"{variables['SOURCE_DIR']}/{sandbox.get('stats-yaml')}"
            print(f"sandbox_res: {sandbox_res}")
            with open(sandbox_res, 'r') as f:
                taskresults['sandbox_results'] = yaml.safe_load(f)
            if result.returncode == 0:
                taskresults["status"] = "OK"
                taskresults["task-id"] = task_id
            if result.stderr:
                print(result.stderr)
            results['results'].append(taskresults)
            
        except FileNotFoundError:
            print(f"Launching isolator with \"{bin_path}\" failed.")
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
        except FileNotFoundError:
            print(f"Command not found: {bin_path}")

def main():
    with open(f"{variables['SOURCE_DIR']}/job-config.yml") as f:
        data = yaml.safe_load(f)
    
    data = substitute_variables(data, variables)

    tasks = data.get('tasks', [])
    results = {}
    results.setdefault('results', [])
    for task in tasks:
        print(f"--- Running {task['task-id']} ---")
        run_task(task,results)
    with open(f"{variables['SOURCE_DIR']}/my_results.yml", 'w') as f:
        yaml.dump(results, f)

if __name__ == "__main__":
    main()
