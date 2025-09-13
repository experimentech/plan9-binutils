9front system calls (reference)
===============================

This is a compact reference table for common 9front system calls. For programmatic use, see `9front-syscalls.csv` and `9front-syscalls.json` in this directory.

| Num | Name             | Notes                   |
|-----|------------------|-------------------------|
| 0   | sysexec          | replace current image   |
| 1   | sysfork          | create new process      |
| 2   | sysrfork         | create process w/flags  |
| 3   | sysopen          | open file               |
| 4   | sysclose         | close fd                |
| 5   | sysread          | read fd                 |
| 6   | syswrite         | write fd                |
| 7   | sysseek          | seek fd                 |
| 8   | sysstat          | stat file               |
| 9   | syserrstr        | get error string        |
| 10  | syswstat         | write stat              |
| 11  | syschdir         | change directory        |
| 12  | sysdup           | duplicate fd            |
| 13  | sysmount         | mount fs                |
| 14  | sysunmount       | unmount fs              |
| 15  | sysnotify        | setup note handler      |
| 16  | sysnoted         | return from note        |
| 17  | syssleep         | sleep ms                |
| 18  | sysalarm         | alarm                   |
| 19  | sysrfork2        | rfork variant           |
| 20  | sysbind          | bind name space         |
| 21  | sysunbind        | unbind name space       |
| 22  | sysreboot        | reboot                  |
| 23  | syspipe          | create pipe             |
| 24  | syscreate        | create file             |
| 25  | sysfd2path       | fd to path              |
| 26  | sysbrk           | adjust data segment     |
| 27  | sysremove        | remove file             |
| 28  | syswstatfd       | wstat by fd             |
| 29  | sysstatfd        | stat by fd              |
| 30  | syssegattach     | attach segment          |
| 31  | syssegdetach     | detach segment          |
| 32  | syssegfree       | free segment            |
| 33  | syssegflush      | flush segment           |
| 34  | syserrstr2       | set error string        |
| 35  | syswait          | wait child              |
| 36  | sysfwstat        | fwstat                  |
| 37  | sysfstat         | fstat                   |
| 38  | sysnintr         | notify interrupts       |
| 39  | sysrendezvous    | rendezvous              |
| 40  | syssemacquire    | sem acquire             |
| 41  | syssemrelease    | sem release             |
| 42  | sysseek64        | seek 64-bit             |
| 43  | sysfversion      | fs version              |
| 44  | sysfauth         | fs auth                 |
| 45  | syserrdeprecated | deprecated              |
| 46  | sysstat64        | stat 64-bit             |
| 47  | syswstat64       | wstat 64-bit            |
| 48  | sysopenfd        | open returning fd       |
| 49  | sysexits         | exit with status        |

Notes
-----
- The numbering and names above are representative and may vary across architectures or system revisions. Use the CSV/JSON as a machine-readable source and validate against your kernel headers when building tooling.
