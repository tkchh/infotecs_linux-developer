#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TKCHH");
MODULE_DESCRIPTION("Writing module in file with timer");

static const char msg[] = "Hello from kernel module\n";

static int period_sec = 3;
static char *target_file = "/tmp/kernel_output.txt";

static struct work_struct write_work;
static struct timer_list my_timer;

static void write_work_func(struct work_struct *work){
    struct file *file = NULL;
    loff_t pos = 0;
    ssize_t written;

    file = filp_open(target_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (IS_ERR(file)) {
        pr_err("Не удалось открыть файл %s: ошибка %ld\n", target_file, PTR_ERR(file));
        return;
    }

    pos = file_inode(file)->i_size;

    written = kernel_write(file, msg, strlen(msg), &pos);

    if (written < 0) {
        pr_err("Ошибка записи: %ld\n", written);
    }else {
        pr_info("Записано %ld байт в %s\n", written, target_file);
    }
}

static void my_timer_func(struct timer_list *unused) {
    schedule_work(&write_work);
    pr_info("Added schedule work");

    mod_timer(&my_timer, jiffies + msecs_to_jiffies(period_sec * 1000));
}

static int __init hello_init(void) {
    pr_info("Загружен модуль. Период: %d сек, файл: %s\n", period_sec, target_file);

    INIT_WORK(&write_work, write_work_func);

    timer_setup(&my_timer, my_timer_func, 0);

    mod_timer(&my_timer, jiffies + msecs_to_jiffies(period_sec * 1000));

    return 0;
}

static void __exit hello_exit(void) {
    del_timer_sync(&my_timer);

    cancel_work_sync(&write_work);

    pr_info("Модуль отключен!\n");
}

module_init(hello_init);
module_exit(hello_exit);
