
stoch
======

A simple linux device driver / kernel module
It stores a histogram of data written to it and generates (random) output
distributed from the histogram.
To clear out the stored data, you must remove the kernel module and load it again

1. First compile the module,
$ ./mk.sh
 
2. Create the device node and associate with the module
$ mknod stoch c 60 0

3. Load the module
$ insmod stoch.ko

4. Write to it to "train" it
$ echo hello > /dev/stoch

5. generate random output
$ cat /dev/stoch ----> hllle

6. remove the module
rmmod stoch

Frank James December 2013

