# rcdx_cntnr
Lightweight Linux containerization tool written "from scratch", primarily intended for evaluation of programming assignments.

### System prerequisites

You need a kernel with cgroupv2 enabled. Check with ```mount | grep cgroup```.

CMake 3.20 and a compiler capable of C++23.

Boost program_options package.

For using limits on disk usage, the container itself has to run on a filesystem that supports QUOTACTL(2) (e.g. ext4).

For now, cleanup of the cgroups created during the run of a container has to be done manually after. The scripts in this repo use [cgdelete](https://command-not-found.com/cgdelete).


### Usage

After installing everything required, run the script ```demoV1/scripts/initialize_system.sh```. It shouldn't be necessary to run it again after reboot.

Build with ```demoV1/scripts/build.sh```

Right now, the program should be launched as follows: ```demoV1/scripts/run.sh --yaml=\<path_to_yaml_configuration_file\>```. The script cleans up and recompiles before running again (aside from the root directory for the box. If it is the same, you have to delete it yourself.) 

An example configuration file can look like this:
```
box-root: "/box"      #path to the directory that will be the root for the isolated tasks. It is required that it doesn't yet exist, for security purposes. 

env:
  dir-rules:                                 # optional user specified list of directory rules declaring the directories that will exist
                                                            inside the box, the syntax is described [here]()
    - "tests=/home/user/project/tests"
  use-defaults: false                        # mount a default list of directories into the box ( like /lib, /lib64, /bin, ...), true is the default if not specified 

tasks:
    - task-id: "example"                    # (mandatory) name of the task (has to be unique within one run of the container), is used in output files of the box
      path: "tests/example"   # (mandatory) path to the executable inside of the box - you have to use a directory rule to get it there.

      args:                           # Optional list of arguments passed to the executable in an execve call.
        - "Hello"
        - "World" 

      rlims:                          # Optional node with specification of resource limits for the task. Times are in seconds and memory sizes in bytes.
                                        
          mem: 5000000          #If any of these is not specified, no limit will be set. The exception is a default wall time limit of 20s.
          cpu-time: 3
          wall-time: 5
          disk-usage: 1000000

```