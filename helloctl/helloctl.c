#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_PERIOD_SEC  5
#define MIN_PERIOD_SEC      1
#define MAX_PERIOD_SEC      3600
#define MAX_PATH_LENGTH     256

#define SYSFS_DIR   "/sys/kernel/hello_module"
#define PERIOD_ATTR SYSFS_DIR "/period_sec"
#define FILE_ATTR   SYSFS_DIR "/target_file"

// коды возврата: 0 — ок, 1 — ошибка аргументов, 2 — ошибка выполнения
#define EXIT_USAGE  1
#define EXIT_RUNTIME 2

static void usage(FILE *out)
{
    fprintf(out,
        "CLI-команда для управления модулем hello_module\n"
        "Usage: helloctl <команда> [аргумент]\n"
        "\n"
        "Команды:\n"
        "  status                показать период записи и файл для записи\n"
        "  period [<сек>]    показать или изменить период записи (%d..%d секунд)\n"
        "  file [<путь>]         показать или изменить файл для записи (абсолютный путь)\n"
        "  help                  показать это сообщение\n"
        "\n"
        "Чтение атрибутов без прав root, изменение атрибутов только с правами root\n",
        MIN_PERIOD_SEC, MAX_PERIOD_SEC);
}

/* Единая точка для ошибок sysfs — с расшифровкой типичных случаев */
static void die_attr()
{
    switch (errno) {
    case ENOENT:
        fprintf(stderr, "helloctl: модуль не загружен (загрузите с помощью insmod hello_module.ko)\n");
        break;
    case EACCES:
    case EPERM:
        fprintf(stderr, "helloctl: отказано в доступе (запустите от имени root)\n");
        break;
    default:
        fprintf(stderr, "helloctl: %s\n", strerror(errno));
    }
}

static int read_attr(const char *attr, char *buf, size_t size)
{
    int fd;
    ssize_t n;

    fd = open(attr, O_RDONLY);
    if (fd < 0)
        return -1;

    n = read(fd, buf, size - 1);
    close(fd);

    if (n < 0) {
        return -1;
    }

    buf[n] = '\0';
    while (n > 0 && buf[n - 1] == '\n')
        buf[--n] = '\0';
    return 0;
}

//за один системный вызов write без stdio
static int write_attr(const char *attr, const char *value)
{
    char buf[MAX_PATH_LENGTH + 16];
    int fd;
    ssize_t n;
    int len;

    len = snprintf(buf, sizeof(buf), "%s\n", value);	/* store() срезает '\n' */
    if (len < 0 || (size_t)len >= sizeof(buf)) {
        errno = EOVERFLOW;
        return -1;
    }

    fd = open(attr, O_WRONLY);
    if (fd < 0)
        return -1;

    n = write(fd, buf, len);
    close(fd);

    if (n != len) {
        return -1;
    }
    return 0;
}

//helloctl period
static int cmd_period(const char *arg)
{
    char buf[32];

    if (!arg) {
        if (read_attr(PERIOD_ATTR, buf, sizeof(buf)) < 0) {
            die_attr();
            return EXIT_RUNTIME;
        }
        printf("%s\n", buf);
        return 0;
    }

    /* установка: строгий парсинг целого */
    char *end;
    errno = 0;
    long val = strtol(arg, &end, 10);

    if (errno != 0 || end == arg || *end != '\0') {
        fprintf(stderr, "helloctl: '%s' некорректное значение периода\n", arg);
        return EXIT_USAGE;
    }
    if (val < MIN_PERIOD_SEC || val > MAX_PERIOD_SEC) {
        fprintf(stderr, "helloctl: значение периода должно быть от %d до %d секунд\n",
            MIN_PERIOD_SEC, MAX_PERIOD_SEC);
        return EXIT_USAGE;
    }

    snprintf(buf, sizeof(buf), "%ld", val);
    if (write_attr(PERIOD_ATTR, buf) < 0) {
        die_attr();
        return EXIT_RUNTIME;
    }

    printf("Значение периода установлено в %ld секунд\n", val);
    return 0;
}

//helloctl file
static int cmd_file(const char *arg)
{
    char buf[MAX_PATH_LENGTH + 8];

    if (!arg) {						/* чтение */
        if (read_attr(FILE_ATTR, buf, sizeof(buf)) < 0) {
            die_attr();
            return EXIT_RUNTIME;
        }
        printf("%s\n", buf);
        return 0;
    }

    size_t len = strlen(arg);

    if (len == 0) {
        fprintf(stderr, "helloctl: пустой путь\n");
        return EXIT_USAGE;
    }
    if (arg[0] != '/') {
        fprintf(stderr, "helloctl: путь должен быть абсолютным (начинаться с '/')\n");
        return EXIT_USAGE;
    }
    if (len >= MAX_PATH_LENGTH) {
        fprintf(stderr, "helloctl: путь слишком длинный (макс длина %d)\n",
            MAX_PATH_LENGTH - 1);
        return EXIT_USAGE;
    }

    if (write_attr(FILE_ATTR, arg) < 0) {
        die_attr();
        return EXIT_RUNTIME;
    }

    printf("Файл для записи изменен на %s\n", arg);
    return 0;
}

//helloctl status
static int cmd_status(void)
{
    char pbuf[32], fbuf[MAX_PATH_LENGTH + 8];

    if (read_attr(PERIOD_ATTR, pbuf, sizeof(pbuf)) < 0) {
        die_attr();
        return EXIT_RUNTIME;
    }
    if (read_attr(FILE_ATTR, fbuf, sizeof(fbuf)) < 0) {
        die_attr();
        return EXIT_RUNTIME;
    }

    printf("Период:      %s sec\n", pbuf);
    printf("Файл для записи: %s\n", fbuf);

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return EXIT_USAGE;
    }

    if (!strcmp(argv[1], "help") || !strcmp(argv[1], "-h") ||
        !strcmp(argv[1], "--help")) {
        usage(stdout);
        return 0;
    }

    if (!strcmp(argv[1], "status")) {
        if (argc != 2) {
            usage(stderr);
            return EXIT_USAGE;
        }
        return cmd_status();
    }

    if (!strcmp(argv[1], "period")) {
        if (argc > 3) {
            usage(stderr);
            return EXIT_USAGE;
        }
        return cmd_period(argc == 3 ? argv[2] : NULL);
    }

    if (!strcmp(argv[1], "file")) {
        if (argc > 3) {
            usage(stderr);
            return EXIT_USAGE;
        }
        return cmd_file(argc == 3 ? argv[2] : NULL);
    }

    fprintf(stderr, "helloctl: неизвестная команда '%s'\n\n", argv[1]);
    usage(stderr);
    return EXIT_USAGE;
}
