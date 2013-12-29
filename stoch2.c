
/*
 * A simple linux device driver / kernel module
 * It stores a histogram of data written to it and generates (random) output
 * distributed from the histogram.
 * To clear out the stored data, you must remove the kernel module and load it again
 *
 * 1. First compile the module,
 * $ ./mk.sh
 * 
 * 2. Create the device node and associate with the module
 * $ mknod stoch c 60 0
 *
 * 3. Load the module
 * $ insmod stoch.ko
 *
 * 4. Write to it to "train" it
 * $ echo hello > /dev/stoch
 *
 * 5. generate random output
 * $ cat /dev/stoch ----> hllle
 *
 * 6. remove the module
 * rmmod stoch
 *
 * Frank James December 2013
 */



#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/init.h>		/* Needed for the macros */
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <asm/uaccess.h> /* copy_from/to_user */
#include <linux/random.h>

#include <linux/string.h>

// define this to enable debug printk messages
#if 0
#define STOCHDBG
#endif

#define DRIVER_AUTHOR "Frank James <frank.a.james@gmail.com>"
#define DRIVER_DESC "Simple driver that generates stochastic output"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);  
MODULE_DESCRIPTION(DRIVER_DESC); 
MODULE_SUPPORTED_DEVICE("stoch"); // device /dev/stoch

/* driver major number */
#define STOCH_MAJOR 60

#define STOCH_HIST_SIZE 256

struct _stoch_hist {
	unsigned int data[STOCH_HIST_SIZE];
	unsigned int total;
};

/* function declarations */
static int stoch_open(struct inode *inode, struct file *filp);
static int stoch_release(struct inode *inode, struct file *filp);
static ssize_t stoch_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t stoch_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);

static void stoch_hist_update( struct _stoch_hist *h, unsigned char x );
static void stoch_hist_clear( struct _stoch_hist *h );
static void stoch_hists_clear( void );
static unsigned char stoch_hist_val( struct _stoch_hist *h, unsigned char prev );
static size_t stoch_hist_gen( unsigned char *buff, size_t size );

/* Structure that declares the usual file access functions */
struct file_operations stoch_fops = {
  read: stoch_read,
  write: stoch_write,
  open: stoch_open,
  release: stoch_release
};

/* ------- hist --------------- */

static struct {
	struct _stoch_hist data[STOCH_HIST_SIZE];
	unsigned int total;
} stoch_hist;

static unsigned char stoch_prev = 0;

static void stoch_hist_update( struct _stoch_hist *h, unsigned char x ) {
	int p;
	
	p = (int)x;
	if (p >= 0 && p < STOCH_HIST_SIZE) {
		// should check here for an overflow...		
		h->data[x]++;
		h->total++;
		stoch_hist.total++;
	}
}

static void stoch_hist_clear( struct _stoch_hist *h ) {
	memset( h, sizeof(struct _stoch_hist), 0 );
}

static void stoch_hists_clear( void ) {
#if 0
	int i;
	
	for (i = 0; i < STOCH_HIST_SIZE; i++) {
		stoch_hist.data[0].data[i] = 1;
	}
	stoch_hist.data[0].total = STOCH_HIST_SIZE;
	
	for (i = 1; i < STOCH_HIST_SIZE; i++) {
		stoch_hist_clear( &stoch_hist.data[i] );
	}
	stoch_hist.total = STOCH_HIST_SIZE;
#else
	memset( &stoch_hist, sizeof(stoch_hist), 0 );
#endif
}

// generate a random number from the hist
static unsigned char stoch_hist_val( struct _stoch_hist *h, unsigned char prev ) {
	unsigned int j, p, tot;
	unsigned char i, val;

	// if no data has been written to the histogram then just return 0
	if (h->total == 0) {
		return 0;
	}
	
	get_random_bytes( &j, sizeof(unsigned int) );
	p = (unsigned char)(j % h->total);
	tot = 0;
	val = 0;
	for (i = 0; i < STOCH_HIST_SIZE; i++) {
		tot += h->data[i];
		
		val = i;		
		if (tot >= p) {
			// found the bin, break out and return
			break;
		}
	}
	
#ifdef STOCHDBG
	printk( KERN_INFO "stoch: %d->%d\n", prev, val );
#endif
	
	return val;		
}

static size_t stoch_hist_gen( unsigned char *buff, size_t size ) {
	size_t i, j, tot;
	int pos = size;
	unsigned char prev = 0;

	// first choose a starting point
	if (stoch_hist.total == 0) {
		pos = 0;
		prev = 0;
	} else {
		get_random_bytes( &i, sizeof(size_t) );
		i = i % stoch_hist.total;
		tot = 0;
		for (j = 0; j < STOCH_HIST_SIZE; j++) {
			tot += stoch_hist.data[j].total;
			prev = j;
			if (tot >= i) {
				break;
			}
		}
	}
	
#ifdef STOCHDBG
	printk( KERN_INFO "stoch: prev %d\n", prev );
#endif
	
	// now generate the buffer output
   
	for (i = 0; i < size; i++) {
		if (pos == size) {
			buff[i] = stoch_hist_val( &stoch_hist.data[prev], prev );
			prev = buff[i];
			if (buff[i] == 0) {
				pos = i;
			}
		} else {
			buff[i] = 0;
		}
	}

#ifdef STOCHDBG
	printk( KERN_INFO "stoch: pos %d\n", pos );
#endif
	
	return pos;
}

/* --------------------------------- */

static int __init stoch_init( void ) {
	int result;
	
	/* Registering device */
	result = register_chrdev( STOCH_MAJOR, "stoch", &stoch_fops );
	if (result < 0) {
		printk( KERN_INFO "stoch: cannot obtain major number %d\n", STOCH_MAJOR );
		return result;
	}

	// clear out the histograms
	stoch_hists_clear();
	
	printk( KERN_INFO "stoch: init\n" );
	
	return 0;
}

static void __exit stoch_exit( void ) {
	printk( KERN_INFO "stoch: exit\n" );
	unregister_chrdev( STOCH_MAJOR, "stoch" );
}

static int stoch_open(struct inode *inode, struct file *filp) {
	return 0;
}

static int stoch_release(struct inode *inode, struct file *filp) {
	return 0;
}

// generate random output from the histogram
static ssize_t stoch_read(struct file *filp, char *buf, size_t count, loff_t *f_pos) {
	char *tmp;
	size_t n;
	
	tmp = (char *)kmalloc( count, GFP_KERNEL );
	n = stoch_hist_gen( (unsigned char *)tmp, count );
	tmp[count-1] = 0;
	
	copy_to_user( buf, tmp, count );

	kfree( tmp );
	
	return n;
}

// populate the histogram
static ssize_t stoch_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) {
	int i;
	unsigned char x;
	unsigned char *tmp;
	
	tmp = kmalloc( count, GFP_KERNEL );

	copy_from_user( tmp, buf, count );
	for (i = 0; i < count; i++) {
		x = tmp[i];

		stoch_hist_update( &stoch_hist.data[stoch_prev], x );
		stoch_prev = x;
	}

	kfree( tmp );
	
	return count;
}

module_init(stoch_init);
module_exit(stoch_exit);

