#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TKCHH");
MODULE_DESCRIPTION("Writing module in file with timer");

#define DEFAULT_PERIOD_SEC  5
#define DEFAULT_TARGET_FILE "/tmp/kernel_output.txt"
#define MAX_PATH_LENGTH     256
#define MIN_PERIOD_SEC      0
#define MAX_PERIOD_SEC      3600

static const char msg[] = "Hello from kernel module\n";

static struct kobject *hellomodule;
static DEFINE_MUTEX(config_mutex);

static int period_sec = DEFAULT_PERIOD_SEC;
static char *target_file = NULL;


static struct timer_list my_timer;
static struct work_struct write_work;


//Чтение/изменение периода
static ssize_t period_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
    return sprintf(buf, "%d\n", READ_ONCE(period_sec));
}

static ssize_t period_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
    int new_period;
    int ret;

    ret = kstrtoint(buf, 10, &new_period);
    if (ret < 0) {
        pr_err("Invalid period vlaue: %s\n", buf);
        return ret;
    }

    if (new_period < MIN_PERIOD_SEC || new_period > MAX_PERIOD_SEC) {
        pr_err("Period must be between %d and %d seconds\n", MIN_PERIOD_SEC, MAX_PERIOD_SEC);
        return -EINVAL;
    }

    mutex_lock(&config_mutex);
    period_sec = new_period;

    mod_timer(&my_timer, jiffies + msecs_to_jiffies(new_period * 1000));
    mutex_unlock(&config_mutex);

    pr_info("Period changed to %d seconds\n", new_period);

    return count;
}

static struct kobj_attribute period_attribute = __ATTR(period_sec, 0660, period_show, period_store);

//Чтение/изменение файла для записи target_file

static ssize_t target_file_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
    ssize_t len;

    mutex_lock(&config_mutex);

    if (!target_file) {
        mutex_unlock(&config_mutex);
        return sprintf(buf, "(null)\n");
    }

    len = sprintf(buf, "%s\n", target_file);
    mutex_unlock(&config_mutex);

    return len;
}

static ssize_t target_file_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
    char *new_path = NULL;
    char *old_path = NULL;
    size_t len;

    if (count == 0) {
        pr_err("Empty path not allowed\n");
        return -EINVAL;
    }

    if (count > PATH_MAX) {
        pr_err("Path too long (max length %d)\n", PATH_MAX);
        return -ENAMETOOLONG;
    }

    len = count;
    if (buf[len - 1] == '\n'){
        len--;
    }

    if (len == 0){
        pr_err("Path is empty after removing newline\n");
        return -EINVAL;
    }

    new_path = kmalloc(len+1, GFP_KERNEL);
    if (!new_path) {
        pr_err("Failed to allocate mem for path\n");
        return -ENOMEM;
    }

    memcpy(new_path, buf, len);
    new_path[len] = '\0';

    mutex_lock(&config_mutex);
    old_path = target_file;
    target_file = new_path;
    mutex_unlock(&config_mutex);

    kfree(old_path);

    pr_info("Target file changed to: %s\n", new_path);

    return count;
}

static struct kobj_attribute target_file_attr = __ATTR(target_file, 0660, target_file_show, target_file_store);

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
    int error = 0;


    target_file = kstrdup(DEFAULT_TARGET_FILE, GFP_KERNEL);
    if (!target_file){
        pr_err("Failed to allocate target_file\n");
        return -ENOMEM;
    }
    pr_info("Загружен модуль. Период: %d сек, Файл: %s\n", period_sec, target_file);


    hellomodule = kobject_create_and_add("hello_module", kernel_kobj);
    if (!hellomodule)
        return -ENOMEM;

    error = sysfs_create_file(hellomodule, &period_attribute.attr);
    if (error) {
        kobject_put(hellomodule);
        pr_info("failed to create the period file in /sys/kernel/hello_module");
    }

    error = sysfs_create_file(hellomodule, &target_file_attr.attr);
    if (error) {
        kobject_put(hellomodule);
        pr_info("failed to create the target_file file in /sys/kernel/hello_module");
    }

    INIT_WORK(&write_work, write_work_func);

    timer_setup(&my_timer, my_timer_func, 0);

    mod_timer(&my_timer, jiffies + msecs_to_jiffies(period_sec * 1000));

    return 0;
}

static void __exit hello_exit(void) {
    kobject_put(hellomodule);

    del_timer_sync(&my_timer);

    cancel_work_sync(&write_work);

    kfree(target_file);

    pr_info("Модуль отключен!\n");
}

module_init(hello_init);
module_exit(hello_exit);
