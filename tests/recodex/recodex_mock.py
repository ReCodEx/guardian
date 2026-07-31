#!/bin/env python3
import sys
import subprocess
import yaml
import os
import pathlib
from pathlib import Path
import re
import pandas as pd

SCRIPT_DIR = Path(__file__).resolve().parent
ISOLATE_SANDBOX = f"{SCRIPT_DIR}/../../build/src/isolator"
CWD = os.getcwd()

dotnet_versions = {
    6: ("6.0.420", "6.0.28"),
    7: ("7.0.400", "7.0.10"),
    8: ("8.0.100", "8.0.0")
}
dotnet_version_files = {
    "0076854220a16837db1d9ed03c15bd95f473d992": 6,
    "4d877a1f7ee1685ff7f3b5bacb5be28e2a4f6b09": 8,
}
MAVEN_REPO = "/opt/maven-repo"

dirs = {
    "ISOLATE_CONFIG": "isolate_config.yml",
    # "SOURCE_DIR": f"{CWD}/{sys.argv[1]}",
    "EVAL_DIR": ".",
    # "RESULT_DIR": f"{CWD}/results/{sys.argv[1]}",
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
        subprocess.run("sudo", [f"{SCRIPT_DIR}/recodex_mock.py", "mvn_init_job"])

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

def get_ld_library_path(CC="gcc"):
    paths = set()

    # 1. gcc/g++ internal paths
    try:
        out = subprocess.check_output([f"{CC}", "-print-search-dirs"], text=True)
        for line in out.splitlines():
            if line.startswith("libraries: ="):
                libs = line.split("=", 1)[1].split(":")
                paths.update(libs)
    except subprocess.CalledProcessError:
        pass

    # 4. Path to libstdc++
    try:
        libstdcpp = subprocess.check_output([f"{CC}", "-print-file-name=libstdc++.so"], text=True).strip()
        if os.path.isfile(libstdcpp):
            real_libstdcpp = pathlib.Path(libstdcpp).resolve()
            paths.add(str(real_libstdcpp.parent))
    except subprocess.CalledProcessError:
        pass

    paths = sorted(paths, key=len, reverse=True)
    paths = [p for p in paths if p]  # Remove empty entries
    return ":".join(paths)

print("GCC LD_LIBRARY_PATH=" + get_ld_library_path())
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

test_ids = {}
token_failed_tests = 0
token_successful_tests = 0
diff_failed_tests = 0
diff_successful_tests = 0
failed_submissions = []
failed_tests = []

def run_task(task, results):
    global token_failed_tests, token_successful_tests, diff_failed_tests, diff_successful_tests, test_ids
    cmd = task.get('cmd')
    if task.get('test-id'):
        test_ids[task['test-id']] = True
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
    
    elif "/bin/csc" in bin_path:
        # Convert csc command to dotnet invocation
        dotnet_cmd = convert_csc_to_dotnet(args)
        bin_path = dotnet_cmd['bin']
        args = dotnet_cmd['args']

    elif "/bin/mono" in bin_path:
        # Convert csc command to dotnet invocation
        bin_path = "/opt/dotnet/dotnet"
        
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
        instance_id = 1
        config_file = os.path.join(dirs['SOURCE_DIR'], task_id + ".yml") 

        workdir = sandbox.get('working-directory')

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
                
            if 'g++' in bin_path:
                ld_path = get_ld_library_path("g++")

            else:
                ld_path = get_ld_library_path("gcc")

            config_data.setdefault('tasks', [])
            config_data['tasks'].append(sandbox)
            config_data['tasks'][-1]['task-id'] = task_id
            config_data['tasks'][-1].setdefault('cmd', {})['bin'] = bin_path
            config_data['tasks'][-1]['cmd']['args'] = args
            config_data['tasks'][-1]['stats-yaml'] = f"{sandbox_res_in}"
            config_data['env']['vars'].append(f"LD_LIBRARY_PATH={ld_path}")
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
            config_data['id'] = instance_id
            # Write modified YAML back to file
            with open(config_file, 'w') as f:
                yaml.dump(config_data, f)
                
        
            

        # os.makedirs(workdir, exist_ok=True)
        try:
                
            isolate_cmd = ["sudo"] + [ISOLATE_SANDBOX] + [f"--yaml={config_file}"]

            result = subprocess.run(isolate_cmd, capture_output=True, text=True)
            print(f"Isolate command: {isolate_cmd}")
            print(f"Outer workdir: {dirs['SOURCE_DIR']}/{workdir}")
            print(f"Inner workdir: {workdir}")
            print(f"Inner cmd: {full_cmd}\n")
            print(f"Stdout: {result.stdout}")

            if "/bin/diff" in bin_path:
                if result.stdout != "":
                    diff_failed_tests += 1
                    print(f"Test {task_id} failed with diff output: {result.stdout}")
                    failed_tests.append(task_id)
                else:
                    diff_successful_tests += 1
            if "token-judge" in bin_path:
                if not result.stdout.strip() == "1":
                    token_failed_tests += 1
                    print(f"Test {task_id} failed with token judge output: {result.stdout}")
                    failed_tests.append(task_id)
                else:
                    token_successful_tests += 1
            taskresults = {}
            # print(f"sandbox_res: {sandbox_res}")
            # with open(sandbox_res_out, 'r') as f:
            #     taskresults['sandbox_results'] = yaml.safe_load(f)
            if result.returncode == 0:
                taskresults["status"] = "OK"
                taskresults["task-id"] = task_id
            if result.stderr:
                print(result.stderr)
            results['results'].append(taskresults)
            cleanup_box(instance_id)
            
        except Exception as e:
            print(f"Exception while running the isolator command: {e}")
    else:
        try:
            result = subprocess.run(["sudo"] + full_cmd, capture_output=True, text=True)
            taskresults = {}
            if result.returncode == 0:
                taskresults["task-id"] = task_id
                taskresults["status"] = "OK"
                
            print(result.stdout)
            if result.stderr:
                print(result.stderr)
            results['results'].append(taskresults)

            print(f"Command: {full_cmd}")
            print(f"Return code: {result.returncode}")
            print(f"Stdout: {result.stdout}")
        except Exception as e:
            print(f"Exception while running the command: {e}")

def cleanup_box(box_id):
    """Clean up isolate box directories"""
    try:
        # Remove cgroup directories
        subprocess.run(['sudo','find', f"/sys/fs/cgroup/isolator_boxes/{box_id}", '-type', 'd', '-depth', '-exec', 'rmdir', '{}', ';'], 
                      stderr=subprocess.PIPE)
        # Remove isolate box directories  
        subprocess.run(['sudo','rm', '-rf', f"/var/lib/isolator_boxes/{box_id}"],
                      stderr=subprocess.PIPE)
    except Exception as e:
        print(f"Error cleaning up isolate box \"{box_id}\": {e}")

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
        print(f"---------------------------------------------\n")
    with open(f"{dirs['SOURCE_DIR']}/my_results.yml", 'w') as f:
        yaml.dump(results, f)
        
    print(f"--- Summary ---")
    print(f"Total tasks: {len(tasks)}")
    print(f"Total tests: {len(test_ids)}")
    print(f"Successful tests: {token_successful_tests + diff_successful_tests}")
    print(f"Failed tests: {token_failed_tests + diff_failed_tests}")
    print(f"Token judge successful tests: {token_successful_tests}")
    print(f"Token judge failed tests: {token_failed_tests}")
    print(f"Diff successful tests: {diff_successful_tests}")
    print(f"Diff failed tests: {diff_failed_tests}")
    if len(failed_tests) > 0:
        print(f"Failed tests: {', '.join(failed_tests)}")
        
def get_dir_state(source_dir):
    """Get the state of the directory before running the submission."""
    orig_files = set()
    for root, dirs, files in os.walk(source_dir):
        for name in files:
            orig_files.add(os.path.join(root, name))
        for name in dirs:
            orig_files.add(os.path.join(root, name))
    return orig_files

def switch_dotnet_version(source_dir):
    for file_hash, version in dotnet_version_files.items():
        version_file = os.path.join(source_dir, file_hash)
        if os.path.exists(version_file):
            print(f"Switching to .NET {version} for submission {source_dir}")
            switch_dotnet_symlinks(version, dotnet_versions)
            return
    switch_dotnet_symlinks(7, dotnet_versions)

def run_submission(source_dir, verbose=False, group="C#"):
    # Save list of filenames and directories before running
    orig_files = get_dir_state(source_dir)

    if group == "C#" and os.path.exists(f"/opt/dotnet/dotnet"):
        switch_dotnet_version(source_dir)

    stats = {}
    global token_failed_tests, token_successful_tests, diff_failed_tests, diff_successful_tests, failed_tests, test_ids
    test_ids = {}
    token_failed_tests = 0
    token_successful_tests = 0
    diff_failed_tests = 0
    diff_successful_tests = 0
    dirs['SOURCE_DIR'] = source_dir
    print(f"Running submission in {source_dir}")
    job_configs = ["job-config.yml", "job.yaml", "job.yml"]
    job_config = None

    for config in job_configs:
        if os.path.exists(f"{source_dir}/{config}"):
            job_config = config
            break

    if job_config is None:
        print(f"No job config file found in {source_dir}/. Tried: {', '.join(job_configs)}")
        sys.exit(1)

    with open(f"{source_dir}/{job_config}") as f:
        data = yaml.safe_load(f)
    
    data = substitute_variables(data, dirs)

    tasks = data.get('tasks', [])
    results = {}
    results.setdefault('results', [])
    for task in tasks:
        print(f"--- Running {task['task-id']} ---")
        run_task(task,results)
        print(f"---------------------------------------------\n")
    with open(f"{source_dir}/my_results.yml", 'w') as f:
        yaml.dump(results, f)
        
    if verbose:
        print(f"--- Summary ---")
        print(f"Total tasks: {len(tasks)}")
        print(f"Total tests: {len(test_ids)}")
        print(f"Successful tests: {token_successful_tests + diff_successful_tests}")
        print(f"Failed tests: {token_failed_tests + diff_failed_tests}")
        print(f"Token judge successful tests: {token_successful_tests}")
        print(f"Token judge failed tests: {token_failed_tests}")
        print(f"Diff successful tests: {diff_successful_tests}")
        print(f"Diff failed tests: {diff_failed_tests}")
        if len(failed_tests) > 0:
            print(f"Failed tests: {', '.join(failed_tests)}")
            failed_submissions.append(source_dir)
    stats["token_successful_tests"] = token_successful_tests
    stats["token_failed_tests"] = token_failed_tests
    stats["diff_successful_tests"] = diff_successful_tests
    stats["diff_failed_tests"] = diff_failed_tests
    stats["failed_tests"] = token_failed_tests + diff_failed_tests
    


    restore_dir(source_dir, orig_files)
    return stats

def switch_dotnet_symlinks(version: int, versions: dict):
    """
    Switch the 'latest' symlinks for the specified .NET major version.

    Args:
        version (int): The major .NET version (6, 7, or 8).
        versions (dict): Dictionary with major version keys (int)
                         and values as tuples of (sdk_version, runtime_version).

    Example:
        versions = {
            6: ("6.0.400", "6.0.10"),
            7: ("7.0.400", "7.0.10"),
            8: ("8.0.200", "8.0.1")
        }
        switch_dotnet_symlinks(7, versions)
    """
    if version not in versions:
        raise ValueError(f"No versions provided for .NET {version}")

    sdk_version, runtime_version = versions[version]

    sdk_path = f"/opt/dotnet/sdk/{sdk_version}"
    runtime_path = f"/opt/dotnet/shared/Microsoft.NETCore.App/{runtime_version}"

    sdk_latest = "/opt/dotnet/sdk/latest"
    runtime_latest = "/opt/dotnet/shared/Microsoft.NETCore.App/latest"

    for target, link in [(sdk_path, sdk_latest), (runtime_path, runtime_latest)]:
        # Remove the old symlink if it exists
        if os.path.islink(link) or os.path.exists(link):
            subprocess.run(['sudo', 'rm', '-f', link])
        # Create the new symlink
        print(f"Creating symlink: {link} -> {target}")
        subprocess.run(['sudo', 'ln', '-s', target, link])

def convert_csc_to_dotnet(csc_args, sdk_root="/opt/dotnet"):
    """
    Converts csc command-line args to dotnet invocation of Roslyn csc.dll.

    Parameters:
        csc_args (list): List of csc arguments (e.g., ['Program.cs', '-main:MyApp.Main', '-out:app.exe']).
        sdk_root (str): Path to .NET SDK root containing `sdk/latest/Roslyn/bincore/csc.dll` and runtime.

    Returns:
        dict: Dictionary with 'bin' and 'args' for the dotnet command.
    """
    dotnet_cmd = {
        "bin": "/opt/dotnet/dotnet",
        "args": [str(Path(sdk_root) / "sdk/latest/Roslyn/bincore/csc.dll")]
    }

    # Add the original arguments as-is, but normalize paths if needed
    dotnet_cmd["args"] += csc_args

    # Add required .NET runtime references
    runtime_dir = Path(sdk_root) / "shared/Microsoft.NETCore.App/latest"
    for dll in sorted(runtime_dir.glob("*.dll")):
        dotnet_cmd["args"].append(f"-r:{dll}")

    return dotnet_cmd

def restore_dir(source_dir, orig_files):
    # Delete any new files or directories that weren't there originally
    current_files = set()
    for root, dirs, files in os.walk(source_dir):
        for name in files:
            current_files.add(os.path.join(root, name))
        for name in dirs:
            current_files.add(os.path.join(root, name))
            
    files_to_delete = current_files - orig_files
    for path in sorted(files_to_delete, reverse=True):  # Reverse sort to handle nested paths
        subprocess.run(['sudo', 'rm', '-rf', path])

def run_groups(submissions_csv, groups=["C#", "Python", "C++", "AdvC++"]):
    stats = {}
    failed_submissions = []
    known_groups = {"882cb969-45d4-4e0d-833c-c3c8f3ade833" : "C#", 
                    "7f6e8f4f-0318-4f5c-befc-be52db78ebda" : "Python", 
                    "2ec6b0ef-268c-41af-b568-70d796e7dba4" : "C++",
                    "7fc24e34-d7e9-4f1f-bcc0-7aded7701de4" : "AdvC++",}
    # Read CSV and sort by group_id
    df = pd.read_csv(submissions_csv)
    df_sorted = df.sort_values('group_id')
    found = 0
    not_found = 0
    # Group by group_id
    for group_id, group_data in df_sorted.groupby('group_id'):
        if group_id in known_groups and known_groups[group_id] in groups:
            print(f"Running submissions from group \"{known_groups[group_id]}\"")
        else:
            print(f"skipping group {group_id}")
            continue
        
        stats[group_id] = {'successful_submissions': [], 'failed_submissions': []}

        
        # Check each reference submission
        for _, row in group_data.iterrows():
            submission_dir = f"{SCRIPT_DIR}/test-data/download/{str(row['reference_submission_id'])}"
            if os.path.isdir(submission_dir):
                found += 1
                submission_stats = run_submission(submission_dir, verbose=False, group=known_groups[group_id])
                # print(f"Stats for submission {submission_dir}: {stats}")
                if submission_stats['failed_tests'] <= 0:
                    stats[group_id]['successful_submissions'].append(submission_dir)
                else:
                    stats[group_id]['failed_submissions'].append(submission_dir)
            else:
                # print(f"Warning: Directory not found for submission {submission_dir}")
                not_found += 1

        
    print(f"\n--- Summary ---")
    print(f"Total submissions found: {found}")
    for group_id, group_stats in stats.items():
        print(f"\nGroup {group_id} ({known_groups.get(group_id, 'Unknown')}):")
        print(f" Successful submissions: {len(group_stats.get('successful_submissions', []))}")
        print(f" Failed submissions: {group_stats.get('failed_submissions', [])}")
if __name__ == "__main__":
    # Initialize before running tests
    subprocess.run([f"{SCRIPT_DIR}/../../scripts/isolator.sh", "purge"], check=False)
    # No init step: --run arranges the isolator_boxes cgroup parent itself (ADR 0007).
    if len(sys.argv) > 2 and sys.argv[1] == "-d":
        run_submission(f"{SCRIPT_DIR}/test-data/download/{sys.argv[2]}", verbose=True)
        sys.exit(0)
    else:
        run_groups(f"{SCRIPT_DIR}/ref-solutions.csv", groups=sys.argv[1:])
