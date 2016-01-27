
#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/dma-contiguous.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#define  DEVICE_NAME "xaxaxadma"    ///< The device will appear at /dev/xaxaxadma using this value
#define  CLASS_NAME  "xaxaxa"        ///< The device class -- this is a character device driver
 
MODULE_LICENSE("GPL");            ///< The license type -- this affects available functionality
MODULE_AUTHOR("Derek Molloy; xaxaxa");    ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("Allows userspace to allocate contiguous dma memory");  ///< The description -- see modinfo
MODULE_VERSION("0.1");            ///< A version number to inform users
 
static int    majorNumber;                  ///< Stores the device number -- determined automatically
static struct class*  cls  = NULL; ///< The device-driver class struct pointer
static struct device* dev = NULL; ///< The device-driver device struct pointer
 
typedef struct {
	spinlock_t lock;
	dma_addr_t alloc_physAddr;
	void* alloc_virtAddr;
	size_t alloc_size;
} xaxaxadma_fileprivate;
typedef struct {
	size_t size;
	unsigned long physAddr;
} xaxaxadma_allocArg;

void do_free(xaxaxadma_fileprivate* priv);

 
/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit xaxaxadma_exit(void){
   device_destroy(cls, MKDEV(majorNumber, 0));     // remove the device
   class_unregister(cls);                          // unregister the device class
   class_destroy(cls);                             // remove the device class
   unregister_chrdev(majorNumber, DEVICE_NAME);             // unregister the major number
   printk(KERN_INFO "xaxaxadma: unload\n");
}
 

static int dev_open(struct inode *inodep, struct file *filep){
	xaxaxadma_fileprivate* priv=(xaxaxadma_fileprivate*)
		kzalloc(sizeof(xaxaxadma_fileprivate),GFP_USER);
	if(IS_ERR(priv)) return PTR_ERR(priv);
	spin_lock_init(&priv->lock);
	filep->private_data=priv;
	return 0;
}
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
	return -EPERM;
}
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
	return -EPERM;
}
 
/** @brief The device release function that is called whenever the device is closed/released by
 *  the userspace program
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *f){
	xaxaxadma_fileprivate* priv=(xaxaxadma_fileprivate*)f->private_data;
	spin_lock(&priv->lock);
	do_free(priv);
	spin_unlock(&priv->lock);
	kfree(priv);
	return 0;
}

long dev_ioctl(struct file * f, unsigned int cmd, unsigned long argp) {
	xaxaxadma_fileprivate* priv=(xaxaxadma_fileprivate*)f->private_data;
	int ret=0;
	xaxaxadma_allocArg arg;
	if((ret=copy_from_user(&arg,(void* __user)argp,sizeof(arg)))<0) return ret;
	
	//spin_lock(&priv->lock);
	switch(cmd) {
		case 1:		//XAXAXADMA_CMD_ALLOCATE
			do_free(priv);
			
			priv->alloc_virtAddr=dma_alloc_coherent(dev,
						arg.size,&priv->alloc_physAddr,GFP_KERNEL);
			if(IS_ERR(priv->alloc_virtAddr) || priv->alloc_virtAddr==NULL) {
				printk(KERN_ERR "xaxaxadma: allocation failed: %d\n",(int)PTR_ERR(priv->alloc_virtAddr));
				ret=-ENOMEM;
				goto out_unlock;
			}
			printk(KERN_INFO "xaxaxadma: allocated %d bytes at physical address: %lx; virtual address %lx\n",
				(int)arg.size, (unsigned long)priv->alloc_physAddr,(unsigned long)priv->alloc_virtAddr);
			priv->alloc_size=arg.size;
			arg.physAddr=(unsigned long)priv->alloc_physAddr;
			ret=copy_to_user((void* __user)argp,&arg,sizeof(arg));
			if(ret<0) {
				do_free(priv);
			}
			break;
		default:
			ret=-ENODEV;
			break;
	}
out_unlock:
	//spin_unlock(&priv->lock);
	return ret;
}
void do_free(xaxaxadma_fileprivate* priv) {
	if(priv->alloc_size==0) return;
	dma_free_coherent(dev,priv->alloc_size,priv->alloc_virtAddr,priv->alloc_physAddr);
	printk(KERN_INFO "xaxaxadma: freed %d bytes at physical address: %lx\n",(int)priv->alloc_size,
				(unsigned long)priv->alloc_physAddr);
	priv->alloc_size=0;
	priv->alloc_physAddr=0;
	priv->alloc_virtAddr=NULL;
}


/** @brief Devices are represented as file structure in the kernel. The file_operations structure from
 *  /linux/fs.h lists the callback functions that you wish to associated with your file operations
 *  using a C99 syntax structure. char devices usually implement open, read, write and release calls
 */
static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
   .unlocked_ioctl = dev_ioctl,
};


 
/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point.
 *  @return returns 0 if successful
 */
static int __init xaxaxadma_init(void){
	int ret=0;
   // Try to dynamically allocate a major number for the device -- more difficult but worth it
   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
   if (majorNumber<0){
      printk(KERN_ALERT "xaxaxadma: failed to register a major number\n");
      return majorNumber;
   }
   printk(KERN_INFO "xaxaxadma: registered correctly with major number %d\n", majorNumber);
 
   // Register the device class
   cls = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(cls)){                // Check for error and clean up if there is
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to register device class\n");
      return PTR_ERR(cls);          // Correct way to return an error on a pointer
   }
   printk(KERN_INFO "xaxaxadma: device class registered correctly\n");
 
   // Register the device driver
   dev = device_create(cls, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
   if (IS_ERR(dev)){               // Clean up if there is an error
      class_destroy(cls);
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to create the device\n");
      return PTR_ERR(dev);
   }
	dev->dma_mask=&(dev->coherent_dma_mask);
	if((ret=dma_set_mask_and_coherent(dev,DMA_BIT_MASK(32)))<0) {
		printk(KERN_WARNING "xaxaxadma: dma_set_mask_and_coherent() failed: %i\n",ret);
	}
   printk(KERN_INFO "xaxaxadma: device class created correctly\n"); // Made it! device was initialized
   return 0;
}
 
/** @brief A module must use the module_init() module_exit() macros from linux/init.h, which
 *  identify the initialization function at insertion time and the cleanup function (as
 *  listed above)
 */
module_init(xaxaxadma_init);
module_exit(xaxaxadma_exit);
