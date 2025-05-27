#~/bin/python3
import sys
import subprocess
import yaml
import os
from pathlib import Path
import re

SCRIPT_DIR = Path(__file__).resolve().parent
ISOLATE_SANDBOX = "/tmp/container"  # simulated sandbox dir

variables = {
    "ISOLATE_CONFIG": "isolate_config.yml",
    "SOURCE_DIR": "hello-world-c",
    "EVAL_DIR": "hello-world-c",
    "RESULT_DIR": "results/hello-world-c",
    "JUDGES_DIR": "/opt/recodex-judges"
}


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

def run_task(task):
    cmd = task.get('cmd')
    if not cmd:
        print(f"Task {task['task-id']} has no command.")
        return

    bin_path = cmd['bin']
    args = cmd.get('args', [])

    if bin_path == 'dumpdir':
        bin_path = os.path.join(SCRIPT_DIR, 'dumpdir')
    full_cmd = [bin_path] + args
    full_cmd = [str(arg) for arg in full_cmd]
    print(f"cmd: {full_cmd}")

    sandbox = task.get('sandbox')
    task_id = task.get('task-id')
    if sandbox and sandbox.get('name') == 'isolate':
        print(f"Running in isolate sandbox: {task['task-id']}")
        # workdir = os.path.join(ISOLATE_SANDBOX, sandbox.get('working-directory', '.'))
        # Find the yaml file from args
        for i, arg in enumerate(args):
            if arg.startswith('--yaml='):
                variables["ISOLATE_CONFIG"] = arg.split('=')[1]
            break
        
        config_file = os.path.join(variables['SOURCE_DIR'], variables['ISOLATE_CONFIG']) 
        with open(variables['ISOLATE_CONFIG'], 'r') as f:
            config_data = yaml.safe_load(f)

            # Inject sandbox node into the correct task in the task list
            config_data.setdefault('tasks', [])
            config_data['tasks'].append(sandbox)
            config_data['tasks'][-1]['task-id'] = task_id
            config_data['tasks'][-1].setdefault('cmd', {})['bin'] = bin_path
            config_data['tasks'][-1]['cmd']['args'] = args


            # Write modified YAML back to file
            with open(config_file, 'w') as f:
                yaml.dump(config_data, f)
            

        # os.makedirs(workdir, exist_ok=True)
        try:

            isolate_cmd = [ISOLATE_SANDBOX] + [f"--yaml={config_file}"]
            print(f"Running isolate command: {isolate_cmd}")
            result = subprocess.run(isolate_cmd, capture_output=True, text=True)
            print(result.stdout)
            if result.stderr:
                print(result.stderr)
        except FileNotFoundError:
            print(f"Command not found: {bin_path}")
    else:
        print(f"Running normal command: {task['task-id']}")
        try:
            result = subprocess.run(full_cmd, capture_output=True, text=True)
            print(result.stdout)
            if result.stderr:
                print(result.stderr)
        except FileNotFoundError:
            print(f"Command not found: {bin_path}")

def main():

    with open(sys.argv[1]) as f:
        data = yaml.safe_load(f)
    
    data = substitute_variables(data, variables)

    tasks = data.get('tasks', [])
    for task in tasks:
        print(f"--- Running {task['task-id']} ---")
        run_task(task)

if __name__ == "__main__":
    main()
