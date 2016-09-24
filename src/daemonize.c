/* From http://www-theorie.physik.unizh.ch/~dpotter/howto/daemonize,
   released into the public domain.
   To properly daemonize, the following steps must be followed.
    * The fork() call is used to create a separate process.
 *    The setsid() call is used to detach the process from the
 *    parent (possibly a shell). The file mask should be reset.
    * The current directory should be changed to something benign.
    * The standard files (stdin,stdout and stderr) need to be reopened.

Failure to do any of these steps will lead to a daemon process that can
misbehave. The typical symptoms are as follows.

    * Starting the daemon and then logging out will cause the terminal to hang. This is particularly nasty with ssh.
    * The directory from which the daemon was launched remains locked.
    * Spurious output appears in the shell from which the daemon was started.

    CN - various changes made to this original code
 *  1) lock file not created by parent process as the lock
 *  appears not to be inherited by children across fork()
 *  2) fork called twice to properly isolate daemon. Don't
 *  really understand the need for this.
 *  3) pid file only opened/written by the daemon child.
 *  4) Daemon process signals intermediate process that all is
 *  OK. Intermediate child signals parent process.
        Otherwise, the parent sees SIGCHLD signal when intermediate process exits, and calls this a failure
        (non-zero exit code).

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <pthread.h>
#include <limits.h>

static int fcntl_lock(int fd, int cmd, int type, int whence, int start, int len, pid_t pid)
{
    struct flock lock;

    lock.l_type = type;
    lock.l_whence = whence;
    lock.l_start = start;
    lock.l_len = len;
    lock.l_pid = pid;

    return fcntl(fd, cmd, &lock);
}

static int lock_pidfile(const char * pidfile)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    struct stat statbuf_fd[1], statbuf_fs[1];
    int pid_fd;

start:

    if ((pid_fd = open(pidfile, O_RDWR | O_CREAT | O_EXCL, mode)) == -1)
    {
        if (errno != EEXIST)
        {
            return -1;
        }

        /*
        ** The pidfile already exists. Is it locked?
        ** If so, another invocation is still alive.
        ** If not, the invocation that created it has died.
        ** Open the pidfile to attempt a lock.
        */
        pid_fd = open(pidfile, O_RDWR);
        if (pid_fd == -1)
        {
            /*
            ** We couldn't open the file. That means that it existed
            ** a moment ago but has been since been deleted. Maybe if
            ** we try again now, it'll work (unless another process is
            ** about to re-create it before we do, that is).
            */

            if (errno == ENOENT)
            {
                goto start;
            }

            return -1;
        }
    }

    if (fcntl_lock(pid_fd, F_SETLK, F_WRLCK, SEEK_SET, 0, 0, 0) == -1)
    {
        close(pid_fd);
        return -1;
    }

    /*
    ** The pidfile may have been unlinked after we opened it by another daemon
    ** process that was dying between the last open() and the fcntl(). There's
    ** no use hanging on to a locked file that doesn't exist (and there's
    ** nothing to stop another daemon process from creating and locking a
    ** new instance of the file. So, if the pidfile has been deleted, we
    ** abandon this lock and start again. Note that it may have been deleted
    ** and subsequently re-created by yet another daemon process just starting
    ** up so check that that hasn't happened as well by comparing inode
    ** numbers. If it has, we also abandon this lock and start again.
    */

    if (fstat(pid_fd, statbuf_fd) == -1)
    {
        /* This shouldn't happen */
        close(pid_fd);
        return -1;
    }

    if (stat(pidfile, statbuf_fs) == -1)
    {
        /* The pidfile has been unlinked so we start again */

        if (errno == ENOENT)
        {
            close(pid_fd);
            goto start;
        }

        close(pid_fd);
        return -1;
    }
    else if (statbuf_fd->st_ino != statbuf_fs->st_ino)
    {
        /* The pidfile has been unlinked and re-created so we start again */

        close(pid_fd);
        goto start;
    }

    return pid_fd;
}

static volatile sig_atomic_t parent_signalled;
static volatile sig_atomic_t got_signal_from_child;

static void child_handler(int signum)
{
    switch (signum)
    {
        case SIGALRM:
            break;
        case SIGUSR1:
            parent_signalled = 1;
            break;
        case SIGCHLD:
            break;
    }
}

static void child_handler2(int signum)
{
    switch (signum)
    {
        case SIGALRM:
            break;
        case SIGUSR1:
            got_signal_from_child = 1; 
            break;
        case SIGCHLD:
            break;
    }
}

int daemonize(char const * const daemon_name, char const * const lock_file, char const * const user)
{
    pid_t pid; 
    pid_t sid; 
    pid_t parent_pid; 
    pid_t intermediate_child_pid;
    FILE * pfp;
    int lfp = -1;
    int result;

    if (getppid() == 1)
    {
        /* Already a daemon. */
        result = -1;
        goto done;
    }

    /*
        CN - We don't create the lock file (and pid file) yet as it appears that the lock is lost across
        the call to fork(), meaning that when the parent process exits, the file becomes unlocked,
        even though the daemon child process has an open file handle still.
        For this reason, I'm only going to create the lock file (and PID file) once the daemon child
        is running.
        I suppose there is a chance that we may come across a permissions problem if we run as a different
        user.
    */

    /* Trap signals that we expect to receive */
    signal(SIGCHLD, child_handler);
    signal(SIGUSR1, child_handler);
    signal(SIGALRM, child_handler);

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0)
    {
        result = -1;
        goto done;
    }

    /* If we got a good PID, wait for the child to start and 
     * signal that it is running. 
     */
    if (pid > 0)
    {
        /* Wait for confirmation from the child via SIGTERM or SIGCHLD, or
         * for two seconds to elapse (SIGALRM). 
         */
        alarm(2);
        pause();
        result = parent_signalled != 0 ? 0 : -1;
        goto done;
    }

    /* At this point we are executing as the child process */
    parent_pid = getppid();
    intermediate_child_pid = getpid();

    /* Cancel certain signals */
    signal(SIGCHLD, SIG_DFL); /* A child process dies */
    signal(SIGTSTP, SIG_IGN); /* Various TTY signals */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP, SIG_IGN); /* Ignore hangup signal */
    signal(SIGTERM, SIG_DFL); /* Die on SIGTERM */

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0)
    {
        exit(EXIT_FAILURE);
    }

    /* Second fork to lose session leadership. */
    pid = fork();
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    else if (pid > 0)
    {
        /*
         *  Exit intermediate child thread. We'll wait for a signal from
         *  the child (daemon) thread and signal the parent then.
        */
        /* set up handlers for signals we're interested in. */
        signal(SIGCHLD, child_handler2);
        signal(SIGUSR1, child_handler2);
        signal(SIGALRM, child_handler2);

        alarm(2);
        pause();
        if (got_signal_from_child != 0)
        {
            kill(parent_pid, SIGUSR1);
            exit(EXIT_SUCCESS);
        }
        /*
            Didn't get the 'all OK' signal.
            Parent will get SIGCHLD signal and return EXIT_FAILURE.
        */
        exit(EXIT_FAILURE);
    }

    /* we've now forked twice, and are a nicely isolated daemon process */
    pid = getpid();

    /* Create the lock file as the current user */
    if (lock_file != NULL && lock_file[0] != '\0')
    {
        lfp = lock_pidfile(lock_file);
        if (lfp < 0)
        {
            exit(EXIT_FAILURE);
        }
    }

    /* Open .pid file now. */
    umask(0022);
    if (daemon_name != NULL && daemon_name[0] != '\0')
    {
        char pid_filename[PATH_MAX];

        snprintf(pid_filename, sizeof pid_filename, "/var/run/%s.pid", daemon_name);
        pfp = fopen(pid_filename, "w+");
        if (pfp == NULL)
        {
            exit(EXIT_FAILURE);
        }
        if (fprintf(pfp, "%ld\n", (long)pid) <= 0)
        {
            exit(EXIT_FAILURE);
        }
        if (fclose(pfp) != 0)
        {
            exit(EXIT_FAILURE);
        }
    }

    if (lfp >= 0)
    {
        ftruncate(lfp, 0);
        dprintf(lfp, "%ld\n",(long)pid);
    }
    /* NB: Leave the lock file open. Closing it would release the 
     * lock. 
     */
#define ROOT_USER_ID 0
    /* Drop user if there is one, and we were run as root */
    if (user != NULL && (getuid() == ROOT_USER_ID || geteuid() == ROOT_USER_ID))
    {
        struct passwd const * const pw = getpwnam(user);

        if (pw != NULL)
        {
            if (setregid(pw->pw_gid, pw->pw_gid) < 0)
            {
                result = -1;
                goto done;
            }
            if (setuid(pw->pw_uid) < 0)
            {
                result = -1;
                goto done;
            }
        }
    }

    /* Change the file mode mask */
    umask(0);

    /* Change the current working directory.  This prevents the current
       directory from being locked; hence not being able to remove it. */
    if ((chdir("/")) < 0)
    {
        result = -1;
        goto done;
    }

    /* Redirect standard files to /dev/null */
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);

    /* Tell the intermediate child process that the daemon process 
       is running.*/
    kill(intermediate_child_pid, SIGUSR1);
    result = 1;

done:
    return result;
}

