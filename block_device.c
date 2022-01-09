#include <linux/blk-mq.h>	
#include <linux/slab.h>	
#include <linux/fs.h>	
#include <linux/kdev_t.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vmalloc.h>


#define B_DEV_MINORS 		16	/* number partitions disk */
#define KERNEL_SECTOR_SIZE 	512
#define LEN_NAME 		32	/* max length name = 30 ('\0' and indexer disk) */
#define FORMAT_SIZE 		79	/* max number devices = 26 , FORMAT_SIZE  <= 26*3 */
#define DISK_SIZES_ARRAY_LEN 	26	
#define NDEVICES 		26
#define OVERFLOW_SIZE 		1048576 /* max summary disk sizes = 1048676 byte eq 1000 MB */

static int b_dev_major = 0;
module_param(b_dev_major, int, 0);

static unsigned int disk_sizes[DISK_SIZES_ARRAY_LEN] = {100};
static int disk_sizes_len = DISK_SIZES_ARRAY_LEN;
module_param_array(disk_sizes,int,&disk_sizes_len,0);

static unsigned int nsectors = 204800;					/* number sectors */
static unsigned int b_dev_hardsect_size = 512;

static char format[FORMAT_SIZE] = "MB";					/* this parametr helps users to select volume data */
module_param_string(format, format, sizeof(format), 0);

static unsigned int ndevices = 1;					/* number devices*/
module_param(ndevices, int , 0);

static char b_dev_name[ LEN_NAME ] = "def_name";
module_param_string(name, b_dev_name, sizeof(b_dev_name), 0);

static bool auto_create=1;						/* This parametr set mode to load module */
module_param(auto_create, invbool, 0);

/*
 * this func check mode to load module
 */ 
static void b_dev_check_auto(bool auto_create)
{
        if(auto_create){
                ndevices=1;
                snprintf(b_dev_name,9,"def_name");
                disk_sizes[0] = 100;
                snprintf(format, 3, "MB");
                b_dev_major = 0;
                printk(KERN_INFO "Auto configuration block_device\n");
        }
        else{
                printk(KERN_INFO "User configuration block_device\n");
        }
        printk(KERN_INFO "name=%s , format=%s, disk_sizes=%u , ndevices=%u, b_dev_major=%d\n",b_dev_name, format,
                        disk_sizes[0], ndevices, b_dev_major);

}

/* 
 * main struct my block device
 */
struct b_dev {
        unsigned int size;		/* size block device byte */                     
        u8 *data;                       /* buffer block device */
        short users;                    /* count users */
        spinlock_t lock;                /* mytex */
	struct blk_mq_tag_set tag_set;	
        struct request_queue *queue;	/* request queue */   
        struct gendisk *gd;             /* disk */
};

static struct b_dev *Devices = NULL;

static void b_dev_transfer(struct b_dev *dev, unsigned long sector,
		unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector*KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect*KERNEL_SECTOR_SIZE;

	if ((offset + nbytes) > dev->size) {
		printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
		return;
	}
	if (write)
		memcpy(dev->data + offset, buffer, nbytes);
	else
		memcpy(buffer, dev->data + offset, nbytes);

}

static blk_status_t b_dev_request(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data* bd)   
{
	struct request *req = bd->rq;
	struct b_dev *dev = req->rq_disk->private_data;
        struct bio_vec bvec;
        struct req_iterator iter;
        sector_t pos_sector = blk_rq_pos(req);
	void	*buffer;
	blk_status_t  ret;

	blk_mq_start_request (req);

	if (blk_rq_is_passthrough(req)) {
		printk (KERN_NOTICE "Skip non-fs request\n");
                ret = BLK_STS_IOERR;  
			goto done;
	}
	rq_for_each_segment(bvec, req, iter)
	{
		size_t num_sector = blk_rq_cur_sectors(req);
		printk (KERN_NOTICE "Req dev %u dir %d sec %lld, nr %ld\n",
                        (unsigned)(dev - Devices), rq_data_dir(req),
                        pos_sector, num_sector);
		buffer = page_address(bvec.bv_page) + bvec.bv_offset;
		b_dev_transfer(dev, pos_sector, num_sector,
				buffer, rq_data_dir(req) == WRITE);
		pos_sector += num_sector;
	}
	ret = BLK_STS_OK;
done:
	blk_mq_end_request (req, ret);
	return ret;
}

static int b_dev_open(struct block_device *bdev, fmode_t mode)
{
	struct b_dev *dev = bdev->bd_disk->private_data;
	spin_lock(&dev->lock);
	dev->users++;
	spin_unlock(&dev->lock);
	return 0;
}

static void b_dev_release(struct gendisk *disk, fmode_t mode)
{
	struct b_dev *dev = disk->private_data;

	spin_lock(&dev->lock);
	dev->users--;
	spin_unlock(&dev->lock);
}

int b_dev_ioctl (struct block_device *bdev, fmode_t mode,
                 unsigned int cmd, unsigned long arg)
{
	return -ENOTTY; 
}

static struct block_device_operations b_dev_ops = {
	.owner           = THIS_MODULE,
	.open 	         = b_dev_open,
	.release 	 = b_dev_release,
	.ioctl	         = b_dev_ioctl,
};

static struct blk_mq_ops mq_ops = {
    .queue_rq = b_dev_request,
};

/* this func check valid size and format
 *  format = MB or KB , size disk <= 1000 MB(1048576 byte)
 *  return size disk to check overflow
 */
static int  b_dev_valid_param_size(unsigned int size_disk, const char  *format,
                unsigned int b_dev_hardsect_size, unsigned int *nsectors)
{

        if(size_disk > 0) {
                if(!(strncmp(format, "MB", 2)) && (size_disk <= 1000)) {
                        printk(KERN_INFO "format=MB and size_disk=%u\n", size_disk);
                        *nsectors=(1048576/b_dev_hardsect_size)*size_disk;
			return size_disk*1024;
                } else if(!(strncmp(format, "KB", 2)) && (size_disk <= 1048576)) {
                                printk(KERN_INFO "format=KB and size_disk=%u\n", size_disk);
                                *nsectors=(1024/b_dev_hardsect_size)*size_disk;
				return size_disk;
                        } else {
                                *nsectors=0;
                                printk(KERN_INFO "INVALID FORMAT OR SIZE DISK!\n");
                                return 0;
                        }
                
        } else {
          	*nsectors=0;
         	printk(KERN_INFO "INVALID SIZE_DISK! SIZE_DISK MIN 1 (MB/KB)\
                                --- SIZE_DISK MAX 1000 MB OR 1048576 KB\n");
                return 0;
        }

        printk(KERN_INFO "size disk=%u and nsectors=%u\n",*nsectors*b_dev_hardsect_size, *nsectors);
        return 0;
}

/* add letter to name */
static int b_dev_valid_name(char *name, int which, size_t len)
{
        if(name != NULL){
		name[len] = which + 'a';
        	name[len + 1] = '\0';
		return 1;
	}
	return -1;
}

static void setup_device(struct b_dev *dev, int which)
{
	/* check len name and
	 * created name for which
	 * devices
	 */
	char temp[LEN_NAME];
        size_t b_dev_name_len = strlen(b_dev_name);
	if(b_dev_name_len + 2 <= LEN_NAME){
		strncpy(temp,b_dev_name,b_dev_name_len);
		b_dev_valid_name(temp,which, b_dev_name_len);
	} else{
		printk(KERN_WARNING "INVALID NAME DISK\n");
		return;
	}

	memset (dev, 0, sizeof (struct b_dev));
	dev->size = nsectors*b_dev_hardsect_size;
	printk(KERN_INFO "nsectors=%u\n", nsectors);
	dev->data = vmalloc(dev->size);
	if (dev->data == NULL) {
		printk (KERN_WARNING "ALLOC BUFF DISK FAILURE\n");
		return;
	}
	spin_lock_init(&dev->lock);
	
	dev->queue = blk_mq_init_sq_queue(&dev->tag_set, &mq_ops, 128, BLK_MQ_F_SHOULD_MERGE);
	if (dev->queue == NULL) {
		printk(KERN_WARNING "QUEUE INIT FAILURE\n");
		goto out_vfree;
	}

	blk_queue_logical_block_size(dev->queue, b_dev_hardsect_size);
	dev->queue->queuedata = dev;
	printk(KERN_INFO "queue init success\n");

	dev->gd = alloc_disk(B_DEV_MINORS);
	if (! dev->gd) {
		printk (KERN_NOTICE "ALLOC DISK FAILURE\n");
		goto out_vfree;
	}
	printk(KERN_INFO "alloc disk success\n");
	dev->gd->major = b_dev_major;
	dev->gd->first_minor = which*B_DEV_MINORS;
	dev->gd->fops = &b_dev_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	sprintf (dev->gd->disk_name,temp);
	set_capacity(dev->gd, nsectors*(b_dev_hardsect_size/KERNEL_SECTOR_SIZE));
	add_disk(dev->gd);
	printk(KERN_INFO "disk %s add success\n", dev->gd->disk_name);
	return;

  out_vfree:
	if (dev->data)
		vfree(dev->data);
}


static int __init b_dev_init(void)
{	
	int i=0;
	int j=0;
	int b_dev_valid_overflow=OVERFLOW_SIZE;

	printk(KERN_INFO "Module load\n");
	
	b_dev_check_auto(auto_create);
	
	b_dev_major = register_blkdev(b_dev_major, b_dev_name);
	if (b_dev_major <= 0) {
		printk(KERN_WARNING "UNABLE TO GET MAJOR NUMBER\n");
		return -EBUSY;
	}
	printk(KERN_INFO "get major number=%d success\n", b_dev_major);

	Devices = kmalloc(ndevices*sizeof (struct b_dev), GFP_KERNEL);
	if (Devices == NULL) {
		printk(KERN_WARNING "ALLOC STRUCT B_DEV FAILURE\n");
		goto out_unregister;
	}

	if(ndevices > 0 && ndevices <= NDEVICES) {
		for(i = 0; i < ndevices; i++) 
		{	
			char *p_format = format + j;

			b_dev_valid_overflow -= b_dev_valid_param_size(disk_sizes[ i ],
				       	p_format, b_dev_hardsect_size, &nsectors);
			
			if(b_dev_valid_overflow < 0) {
				printk(KERN_WARNING "OVERFLOW! MAX SUM DISK SIZES 1000MB\n"); 
				return 0;
			}

			setup_device(Devices + i,i);
			j += 3;
		}

	} else {
		printk(KERN_WARNING "INVALID NDEVICES! NDEVICES <= 26\n");
		goto out_unregister;
	}

	return 0;

  out_unregister:
	unregister_blkdev(b_dev_major, b_dev_name);
	return -ENOMEM;
}

static void b_dev_exit(void)
{
	int i;

	for (i = 0; i < ndevices; i++) {
		struct b_dev *dev = Devices + i;

		if (dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if (dev->queue) 
				blk_cleanup_queue(dev->queue);
		
		if (dev->data)
			vfree(dev->data);
	}
	unregister_blkdev(b_dev_major, b_dev_name);
	kfree(Devices);
	printk(KERN_INFO "Module upload!\n");
}
module_init(b_dev_init);
module_exit(b_dev_exit);

MODULE_LICENSE("GPL");
