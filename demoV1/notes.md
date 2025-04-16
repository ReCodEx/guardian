konzultace:

    1. cast - zatim asi necham na pozdeji

    rozdeleni 2 casti na podkapitoly, 
    styl psani technickych detailu, (vzorky kodu + popis) - detaily na levelu syscallu popisovat 

    co patri do bakalarky a co do dokumentace?,

    demo
    co dal - 

    vyresit problemy se spustenim - 
    jak ma vypadat rozhrani ReCoDex isolate - sepsat co isolate dela,
    keeper daemon proces? - prirazovani ID, directory, cgroup - zatim neresit, vyresit zkontrolovanim existence
    

ISOLATE OPTIONS vs NEW ISOLATE

 *-b, --box-id=*'id':: CONSULT
	When you run multiple sandboxes in parallel, you have to assign unique
	IDs to them by this option. See the discussion on UIDs in the INSTALLATION
	section. The ID defaults to 0.

*-M, --meta=*'file':: CONSULT SYNTAX
	Output meta-data on the execution of the program to a given file.
	See below for syntax of the meta-files.

*-i, --stdin=*'file':: DONE
	Redirect standard input from 'file'. The 'file' has to be accessible
	inside the sandbox (which means that the sandboxed program can manipulate
	it arbitrarily). If not specified, standard input is inherited from the
	parent process.

*-o, --stdout=*'file':: DONE
	Redirect standard output to 'file'. The 'file' has to be accessible
	inside the sandbox (which means that the sandboxed program can manipulate
	it arbitrarily). If not specified, standard output is inherited from the
	parent process and the sandbox manager does not write anything to it.

*-r, --stderr=*'file':: DONE
	Redirect standard error output to 'file'. The 'file' has to be accessible
	inside the sandbox (which means that the sandboxed program can manipulate
	it arbitrarily). If not specified, standard error output is inherited from the
	parent process. See also *--stderr-to-stdout*,

*--stderr-to-stdout*:: DONE
	Redirect standard error output to standard output. This is performed after
	the standard output is redirected by *--stdout*. Mutually exclusive with *--stderr*.

*-c, --chdir=*'dir':: DONE
	Change directory to 'dir' before executing the program. This path must be
	relative to the root of the sandbox.

*-v, --verbose*:: CONSULT
	Tell the sandbox manager to be verbose and report on what is going on.
	Using *-v* multiple times produces even more jabber.

*-s, --silent*:: 
	Tell the sandbox manager to keep silence. No status messages are printed
	to stderr except for fatal errors of the sandbox itself. The combination of
	*--verbose* and *--silent* has an undefined effect. 

*--wait*:: CONSULT
	Multiple instances of Isolate cannot manage the same sandbox simultaneously.
	If you attempt to do that, the new instance refuses to run. With this option,
	the new instance waits for the other instance to finish. 



LIMITS
------
The following options can limit system resources consumed by the program.

*-m, --mem=*'size':: DONE, pomoci cgroups, muzu pridat i setrlimit().
	Limit address space of the program to 'size' kilobytes. If more processes
	are allowed, this applies to each of them separately. If this limit is reached,
	further memory allocations fail (e.g., malloc returns NULL).

    

*-t, --time=*'time':: DONE, pomoci cgroups
	Limit run time of the program to 'time' seconds. Fractional numbers are allowed.
	Time in which the OS assigns the processor to other tasks is not counted.
	If this limit is exceeded, the program is killed (after *--extra-time*, if set).

    

*-w, --wall-time=*'time':: DONE
	Limit wall-clock time to 'time' seconds. Fractional values are allowed.
	This clock measures the time from the start of the program to its exit,
	so it does not stop when the program has lost the CPU or when it is waiting
	for an external event. We recommend to use *--time* as the main limit,
	but set *--wall-time* to a much higher value as a precaution against
	sleeping programs.
	If this limit is exceeded, the program is killed.

    
*-x, --extra-time=*'time':: CONSULT.
	When the *--time* limit is exceeded, do not kill the program immediately,
	but wait until *--extra-time* seconds elapse since the start of the program.
	This allows to report the real execution time, even if it exceeds the limit
	slightly. Fractional numbers are allowed.

*-k, --stack=*'size':: CONSULT
	Limit process stack to 'size' kilobytes. By default, the whole address
	space is available for the stack, but it is subject to the *--mem* limit.
	If this limit is exceeded, the program receives the SIGSEGV signal.

*-n, --open-files=*'max':: TODO
	Limit number of open files to 'max'. The default value is 64. Setting this
	option to 0 will result in unlimited open files.
	If this limit is reached, system calls creating file descriptors fail
	with error EMFILE.

*-f, --fsize=*'size':: CONSULT
	Limit size of each file created (or modified) by the program to 'size' kilobytes.
	In most cases, it is better to restrict overall disk usage by a disk quota
	(see below). This option can help in cases when quotas are not enabled
	on the underlying filesystem.
	If this limit is reached, system calls expanding files fail with error
	EFBIG and the program receives the SIGXFSZ signal.

*-q, --quota=*'blocks'*,*'inodes':: TODO: zlepsit
	Set disk quota to a given number of blocks and inodes. This requires the
	filesystem to be mounted with support for quotas. Unlike other options,
	this one must be given to *isolate --init*. Please note that this
	currently works only on the ext family of filesystems (other filesystems
	use other interfaces for setting quotas).
	If the quota is reached, system calls expanding files fail with error EDQUOT.

*--core=*'size':: CONSULT
	Limit size of core files created when a process crashes to 'size' kilobytes.
	Defaults to zero, meaning that no core files are produced inside the sandbox.

*-p, --processes*[*=*'max']:: DONE
	Permit the program to create up to 'max' processes and/or threads. Please
	keep in mind that time and memory limit do not work with multiple processes
	unless you enable the control group mode. If 'max' is not given, an arbitrary
	number of processes can be run. By default, only one process is permitted.
	If this limit is exceeded, system calls creating processes fail with error
	EAGAIN.


ENVIRONMENT RULES
-----------------
UNIX processes normally inherit all environment variables from their parent. The
sandbox however passes only those variables which are explicitly requested by
environment rules:

*-E, --env=*'var':: DONE
	Inherit the variable 'var' from the parent.

*-E, --env=*'var'*=*'value':: DONE
	Set the variable 'var' to 'value'. When the 'value' is empty, the
	variable is removed from the environment.

*-e, --full-env*:: DONE
	Inherit all variables from the parent.

The rules are applied in the order in which they were given, except for
*--full-env*, which is applied first.

The list of rules is automatically initialized with *-ELIBC_FATAL_STDERR_=1*.

DIRECTORY RULES
---------------
The sandboxed process gets its own filesystem namespace, which contains only subtrees
requested by directory rules:

*-d, --dir=*'in'*=*'out'[*:*'options']:: DONE
	Bind the directory 'out' as seen by the caller to the path 'in' inside the sandbox.
	If there already was a directory rule for 'in', it is replaced.

*-d, --dir=*'dir'[*:*'options']:: DONE
	Bind the dir        constexpr auto STATS_YAML = "stats-yaml";
        constexpr auto CONFIG_YAML = "yaml";ectory +/+'dir' to 'dir' inside the sandbox.
	If there already was a directory rule for 'in', it is replaced.

*-d, --dir=*'in'*=*:: CONSULT
	Remove a directory rule for the path 'in' inside the sandbox.

By default, all directories are bound read-only and restricted (no devices,
no setuid binaries). This behavior can be modified using the 'options':

*rw*:: DONE
	Allow read-write access. 

*dev*:: DONE
	Allow access to character and block devices.

*noexec*:: DONE
	Disallow execution of binaries.

*maybe*:: DONE 
	Silently ignore the rule if the directory to be bound does not exist.

*fs*:: DONE
	Instead of binding a directory, mount a device-less filesystem called 'in'.
	For example, this can be 'proc' or 'sysfs'. 

*tmp*:: DONE
	Bind a freshly created temporary directory writeable for the sandbox user.
	Accepts no 'out', implies *rw*.

*norec*:: DONE
	Do not bind recursively. Without this option, mount points in the outside
	directory tree are automatically propagated to the sandbox.

Unless *--no-default-dirs* is specified, the default set of directory rules binds +/bin+,
+/dev+ (with devices allowed), +/lib+, +/lib64+ (if it exists), and +/usr+. It also binds
the working directory to +/box+ (read-write), mounts the proc filesystem at +/proc+, and
creates a temporary directory +/tmp+.

*-D, --no-default-dirs*:: DONE
	Do not bind the default set of directories. Care has to be taken to specify
	the correct set of rules (using *--dir*) for the executed program to run
	correctly. In particular, +/box+ has to be bound.

The rules are executed in the order in which they are given. Default rules come before
all user rules. When a rule is replaced, it retains the original position
in the order. This matters when one rule's 'in' is a sub-directory of another
rule's 'in'. For example if you first bind to 'a' and then to 'a/b', it will work as
expected, but a sub-directory 'b' must have existed in the directory bound to 'a' (isolate
never creates subdirectories in bound directories for security reasons). If the
order is 'a/b' before 'a', then the directory bound to 'a/b' becomes invisible
by the later binding on 'a'.

CONTROL GROUPS
--------------
Isolate can make use of system control groups provided by the kernel
to constrain programs consisting of multiple processes. Please note
that this feature needs special system setup described in the INSTALLATION
section.

*--cg*:: DONE
	Enable use of control groups. This should be specified with *--init*,
	*--run* and *--cleanup*.

*--cg-mem=*'size':: DONE
	Limit total memory usage by the whole control group to 'size' kilobytes.
	This should be specified with *--run*.
	Effect of reaching this limit depends on circumstances.
	If it happens during memory allocation, the allocation can fail or memory
	can be over-committed by the kernel.
	If it happens when handling a page fault, the whole process is killed
	by the OOM killer with the SIGSEGV signal.

*--print-cg-root*:: CONSIDER
	Print the root of the control group hierarchy in */sys/* and exit.
	This is used by the *isolate-check-environment* script.

META-FILES
----------
CONSULT
 
The meta-file contains miscellaneous meta-information on execution of the
program within the sandbox. It is a textual file consisting of lines
of format 'key'*:*'value'. The following keys are defined:

*cg-mem*::
	When control groups are enabled, this is the total memory use
	by the whole control group (in kilobytes). If you use *isolate --run*
	multiple times in the same sandbox, the control group retains cached
	data from the previous runs, which also contributes to *cg-mem*.
*cg-oom-killed*::
	Present when the program was killed by the out-of-memory killer
	(e.g., because it has exceeded the memory limit of its control group).
	This is reported only on Linux 4.13 and later.
*csw-forced*::
	Number of context switches forced by the kernel.
*csw-voluntary*::
	Number of context switches caused by the process giving up the CPU
	voluntarily.
*exitcode*::
	The program has exited normally with this exit code.
*exitsig*::
	The program has exited after receiving this fatal signal.
*killed*::
	Present when the program was terminated by the sandbox
	(e.g., because it has exceeded the time limit).
*max-rss*::
	Maximum resident set size of the process (in kilobytes).
*message*::
	Status message, not intended for machine processing.
	E.g., "Time limit exceeded."
*status*::
	Two-letter status code:
	* *RE* -- run-time error, i.e., exited with a non-zero exit code
	* *SG* -- program died on a signal
	* *TO* -- timed out
	* *XX* -- internal error of the sandbox
*time*::
	Run time of the program in fractional seconds.
*time-wall*::
	Wall clock time of the program in fractional seconds.

Please note that not all keys have to be present.
For example, no *status* nor *message* is reported upon normal termination.

TEMATA + PROBLEMY DO BAKALARKY:

- kolize pri behu vice kontejneru najednou - navrh reseni
- failed to bind mount pivot directory na novem pristroji - spatne prirazeni box root - reseni: single source of truth (root_credentials_manager)
- MOUNT_DETACH a busy chyba pri umount() 

- redirectovani stdin a stderr - pomoci freopen, nejdrive err, err to out pomoci dup2()

-directory rules - syntax, bezpecnost, ...

-proc zrovna waitpid pri cekani na task

-klonovani tasku, execve, pracovani s PID.

-pripraveni tasku "zvenku" s pouzitim prlimit a cgroup freezer? asi je to nesmysl.

- DETACH flag pri unmountovani rootu.

-system settings - napriklad swap musi byt vypnuty aby fungoval memory limit.

- CLONE_INTO_CGROUP - ano/ne