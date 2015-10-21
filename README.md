Sysmon
===========================================
by [Lei Liu](http://www.escience.cn/people/LiuLei2010ict/index.html), Hao Yang, Mengyao Xie, Yong Li, Mingjie Xing


System Monitor - The Kernel Module Edition
-------------------------------------------

To characterize application memory behaviour, we develop SysMon, an efficient on-line tool integrated in the Linux kernel to monitor system-level application activities such as page access frequency, memory footprint (all used pages), active pages, page re-use time,etc.

Collectively, these metrics from SysMon can be used to classify applications into different categories and select appropriate memory allocation policies.

Release Overview
----------------

* sysmon.c

  recording situations of hot pages and cold pages, including:

  (1) the accessing frequency of each hot page of every iteration.

  (2) the accessing frequency of each physical page of every iteration.
* sysmon_random.c

  recording situations of hot pages and cold pages with the method of random sampling, including:

  (1) the accessing frequency of each hot page of every iteration.
  
  (2) the accessing frequency of each physical page of every iteration.
  
  (3) the prediction of the total number of hot pages and LLC demand.
* sysmon_reuse_distance.c

  based on sysmon.c, add: recording the reuse distance of one page in some loops.
* sysmon_reuse_distance.c

  based on sysmon_random.c add: recording the reuse distance of one page in some loops with the method of random sampling.

Instructions
------------
Writing Makefile:
```
obj-m += sysmon.o
obj-m += sysmon_random.o
obj-m += sysmon_reuse_distance.o
obj-m += sysmon_random_reuse_distance.o
```

Build the module
```
make -C /home/liulei/linux-2.6.32.bak M=$PWD modules
/home/liulei/linux-2.6.32.bak is kernel directory.
```

Insert the module into kernel
```
insmod sysmon.ko process_id=5360
or
insmod sysmon_reuse_distance.ko process_id=5360
5360 is the pid of the application which is running.
```

Output
```
dmesg
```
You should see output like the following from `dmesg`:
![sysmon_dmesg](https://raw.githubusercontent.com/Sys-Inventor-Research-Group-ICT/sss/master/sysmon_dmesg.jpg)

Troubleshooting
---------------
### ERROR: insmod: error inserting 'sysmon.ko': -1 File exists

insert module error:
```
insmod: error inserting 'sysmon.ko': -1 File exists
```
This can happen when you `insmod` the same module twice.You can run
```
rmmod *.ko
```
to remove the exists module and try again.
