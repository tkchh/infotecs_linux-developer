#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TKCHH");
MODULE_DESCRIPTION("Writing module in file with timer");

#define PERIOD HZ/5

static struct timer_list my_timer;

static void my_timer_func(struct timer_list *unused) {
    pr_info("Thats from timer:)");

    my_timer.expires = jiffies + PERIOD;
    add_timer(&my_timer);
}

static int __init hello_2_init(void) {
    pr_info("Hello, world 2!\n");

    timer_setup(&my_timer, my_timer_func, 0);
    my_timer.expires = jiffies + PERIOD;
    add_timer(&my_timer);

    return 0;
}

static void __exit hello_2_exit(void) {
    del_timer_sync(&my_timer);
    pr_info("Goodbye, world 2!\n");
}

module_init(hello_2_init);
module_exit(hello_2_exit);
