#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/slab.h>


#define DEFAULT_PERIOD_SEC  5
#define DEFAULT_TARGET_FILE "/tmp/kernel_output.txt"
#define MAX_PATH_LENGTH     256
#define MIN_PERIOD_SEC      1
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
        pr_err("Некорректное значение периода: %s\n", buf);
        return ret;
    }

    if (new_period < MIN_PERIOD_SEC || new_period > MAX_PERIOD_SEC) {
        pr_err("Значение периода должно быть от %d до %d секунд\n", MIN_PERIOD_SEC, MAX_PERIOD_SEC);
        return -EINVAL;
    }

    mutex_lock(&config_mutex);
    period_sec = new_period;

    mod_timer(&my_timer, jiffies + msecs_to_jiffies(new_period * 1000));
    mutex_unlock(&config_mutex);

    pr_info("Значение периода изменилось на %d секунд\n", new_period);

    return count;
}

static struct kobj_attribute period_attribute = __ATTR(period_sec, 0644, period_show, period_store);

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
        pr_err("Пустой путь к target_file\n");
        return -EINVAL;
    }

    len = count;
    if (buf[len - 1] == '\n')
        len--;

    if (len == 0){
        pr_err("Пустой путь к target_file после удаления знака новой строки\n");
        return -EINVAL;
    }

    if (buf[0] != '/') {
        pr_err("Путь к target_file должен быть абсолютным\n");
        return -EINVAL;
    }

    if (len >= MAX_PATH_LENGTH) {
        pr_err("Путь к target_file слишком длинный (макс длина: %d)", MAX_PATH_LENGTH);
        return -ENAMETOOLONG;
    }

    new_path = kmemdup_nul(buf, len, GFP_KERNEL);
    if (!new_path) {
        return -ENOMEM;
    }

    mutex_lock(&config_mutex);
    old_path = target_file;
    target_file = new_path;
    mutex_unlock(&config_mutex);

    kfree(old_path);

    pr_info("target_file изменился на %s\n", new_path);

    return count;
}

static struct kobj_attribute target_file_attr = __ATTR(target_file, 0644, target_file_show, target_file_store);

static struct attribute *hello_attrs[] = {
    &period_attribute.attr,
    &target_file_attr.attr,
    NULL,
};
static const struct attribute_group hello_group = { .attrs = hello_attrs };

static void write_work_func(struct work_struct *work){
    struct file *file = NULL;
    loff_t pos = 0;
    ssize_t written;
    char *path;

    mutex_lock(&config_mutex);
    path = kstrdup(target_file, GFP_KERNEL);
    mutex_unlock(&config_mutex);
    if (!path)
        return;

    file = filp_open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (IS_ERR(file)) {
        pr_err("Не удалось открыть файл %s: ошибка %ld\n", path, PTR_ERR(file));
        kfree(path);
        return;
    }

    written = kernel_write(file, msg, strlen(msg), &pos);

    filp_close(file, NULL);

    if (written < 0) {
        pr_err("Ошибка записи: %ld\n", written);
    }else {
        pr_info("Записано %ld байт в %s\n", written, path);
    }

    kfree(path);
}

static void my_timer_func(struct timer_list *unused) {
    int current_period = READ_ONCE(period_sec);
    schedule_work(&write_work);
    // pr_info("Added schedule work");

    mod_timer(&my_timer, jiffies + msecs_to_jiffies(current_period * 1000));
}

static int __init hello_init(void) {
    int error;


    target_file = kstrdup(DEFAULT_TARGET_FILE, GFP_KERNEL);
    if (!target_file){
        return -ENOMEM;
    }


    hellomodule = kobject_create_and_add("hello_module", kernel_kobj);
    if (!hellomodule){
        kfree(target_file);
        return -ENOMEM;
    }



    INIT_WORK(&write_work, write_work_func);
    timer_setup(&my_timer, my_timer_func, 0);

    error = sysfs_create_group(hellomodule, &hello_group);
    if (error) {
        pr_err("Не удалось создать sysfs группу: %d\n", error);
        kobject_put(hellomodule);
        kfree(target_file);
        target_file = NULL;
        return error;
    }

    mod_timer(&my_timer, jiffies + msecs_to_jiffies(period_sec * 1000));

    pr_info("Загружен модуль. Период: %d сек, Файл: %s\n", period_sec, target_file);

    return 0;
}

static void __exit hello_exit(void) {
    sysfs_remove_group(hellomodule, &hello_group);
    kobject_put(hellomodule);

    del_timer_sync(&my_timer);
    cancel_work_sync(&write_work);

    kfree(target_file);

    pr_info("Модуль отключен!\n");
}

module_init(hello_init);
module_exit(hello_exit);



MODULE_LICENSE("GPL");
MODULE_AUTHOR("TKCHH");
MODULE_DESCRIPTION("Writing module in file with timer");
