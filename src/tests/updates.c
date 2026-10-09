#define _XOPEN_SOURCE 700
#include "support.h"
#include <sys/file.h>
#include <sys/resource.h>

static void kernel_fixture(const char *path)
{
    unsigned char data[6656] = {0};
    struct field {
        size_t offset;
        uint64_t value;
        unsigned int bytes;
    } fields[] = {{0x1f1, 4, 1},          {0x1fe, 0xaa55, 2},   {0x200, 0x66eb, 2},
                  {0x202, 0x53726448, 4}, {0x206, 0x20c, 2},    {0x211, 1, 1},
                  {0x234, 1, 1},          {0x236, 1, 2},        {0x230, 0x200000, 4},
                  {0x258, 0x1000000, 8},  {0x260, 0x400000, 4}, {0x1f4, 256, 4},
                  {0x238, 2047, 4}};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        put_le(data + fields[i].offset, fields[i].value, fields[i].bytes);
    }
    write_bytes(path, data, sizeof(data));
}

static void uki_fixture(const char *path, unsigned char marker)
{
    unsigned char data[1024] = {0};

    const char *names[] = {".linux", ".initrd", ".cmdline", ".osrel"};

    memcpy(data, "MZ", 2);
    memcpy(data + 64, "PE\0\0", 4);

    put_le(data + 60, 64, 4);
    put_le(data + 68, 0x8664, 2);
    put_le(data + 70, 4, 2);
    put_le(data + 84, 240, 2);
    put_le(data + 88, 0x20b, 2);
    put_le(data + 156, 10, 2);

    for (size_t i = 0; i < 4; ++i) {
        unsigned char *section = data + 328 + 40 * i;
        memcpy(section, names[i], strlen(names[i]));
        put_le(section + 16, 1, 4);
        put_le(section + 20, 512 + i, 4);
    }

    data[900] = marker;

    write_bytes(path, data, sizeof(data));
}

/* Invoked as the updater's sibling stage-linux, with the inherited directory
 * lock still open. Stop after the real stage tool finishes, before publication. */
static int interrupted_stage(int argc, char **argv)
{
    CHECK(argc == 5);

    const char *real = getenv("NEUROS_TEST_STAGE");
    const char *marker = getenv("NEUROS_TEST_MARKER");

    CHECK(real && marker);

    pid_t child = fork();

    CHECK(child >= 0);

    if (!child) {
        argv[0] = (char *)real;
        execv(real, argv);
        _exit(127);
    }

    int status;

    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    write_text(marker, "copied");

    for (;;) {
        pause();
    }
}

int main(int argc, char **argv)
{
    const char *base = strrchr(argv[0], '/');

    if (strcmp(base ? base + 1 : argv[0], "stage-linux") == 0) {
        return(interrupted_stage(argc, argv));
    }
    CHECK(argc == 2);
    char build[PATH_MAX], self[PATH_MAX];
    CHECK(realpath(argv[1], build) != NULL);
    CHECK(realpath(argv[0], self) != NULL);
    start_test("neuros-updates");
    test_log = fmt("%s/test-logs/updates.log", build);
    CHECK(make_parents(test_log) == 0);
    write_text(test_log, "");
    const char *update = fmt("%s/neuros-update-linux", build);
    const char *stage = fmt("%s/stage-linux", build);
    const char *k = fmt("%s/kernel", test_root), *cmd = fmt("%s/cmdline", test_root);
    const char *initrd = fmt("%s/initrd", test_root), *boot = fmt("%s/boot", test_root);
    const char *conf = fmt("%s/boot.conf", boot);
    kernel_fixture(k);
    write_text(cmd, "root=UUID=test quiet\n");
    unsigned char init[16384];
    memset(init, 42, sizeof(init));
    write_bytes(initrd, init, sizeof(init));
    const char *args[] = {update, k, cmd, initrd, boot, NULL};
    const char *rollback[] = {update, "--rollback", boot, NULL};
    run_command(args, 1);
    size_t size;
    unsigned char *text = read_bytes(conf, &size);
    char first[64], second[64], previous[64];
    CHECK(sscanf((char *)text, "NEUROS1\n%63s\n%63s", first, previous) == 2);
    CHECK(strcmp(previous, "-") == 0);
    free(text);
    equal_files(k, fmt("%s/%s/vmlinuz", boot, first));
    equal_files(cmd, fmt("%s/%s/cmdline.txt", boot, first));
    equal_files(initrd, fmt("%s/%s/initrd", boot, first));
    write_text(cmd, "root=UUID=test quiet version=2\n");
    run_command(args, 1);
    text = read_bytes(conf, &size);
    CHECK(sscanf((char *)text, "NEUROS1\n%63s\n%63s", second, previous) == 2);
    CHECK(strcmp(first, second) != 0 && strcmp(first, previous) == 0);
    free(text);
    equal_text(fmt("%s/%s/cmdline.txt", boot, first), "root=UUID=test quiet\n");
    run_command(rollback, 1);
    equal_text(conf, fmt("NEUROS1\n%s\n%s\n", first, second));
    run_command(rollback, 1);
    const char *old = fmt("NEUROS1\n%s\n%s\n", second, first);
    const char *previous_kernel = fmt("%s/%s/vmlinuz", boot, first);
    write_text(previous_kernel, "corrupt previous kernel");
    run_command(rollback, 0);
    equal_text(conf, old);
    CHECK(copy_image(k, previous_kernel, 0) == 0);
    int lock = open(boot, O_RDONLY | O_DIRECTORY);
    CHECK(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0);
    run_command(args, 0);
    CHECK(close(lock) == 0);
    equal_text(conf, old);
    const char *stale = fmt("%s/.boot.conf.new", boot);
    write_text(stale, "interrupted update");
    run_command(args, 0);
    equal_text(conf, old);
    equal_text(stale, "interrupted update");
    CHECK(unlink(stale) == 0);
    write_bytes(cmd, "quiet\0injected", 14);
    run_command(args, 0);
    equal_text(conf, old);
    write_text(cmd, "quiet\n");
    struct rlimit original, small;
    CHECK(getrlimit(RLIMIT_FSIZE, &original) == 0);
    small = original;
    small.rlim_cur = 1024;
    CHECK(setrlimit(RLIMIT_FSIZE, &small) == 0);
    spawn(args, "/dev/null");
    CHECK(setrlimit(RLIMIT_FSIZE, &original) == 0);
    int status = wait_command(10);
    CHECK(!WIFEXITED(status) || WEXITSTATUS(status) != 0);
    equal_text(conf, old);

    const char *helper_update = fmt("%s/helpers/neuros-update-linux", test_root);
    const char *helper_stage = fmt("%s/helpers/stage-linux", test_root);
    const char *marker = fmt("%s/copied", test_root);
    CHECK(copy_image(update, helper_update, 1) == 0 && chmod(helper_update, 0700) == 0);
    CHECK(copy_image(self, helper_stage, 1) == 0 && chmod(helper_stage, 0700) == 0);
    CHECK(setenv("NEUROS_TEST_STAGE", stage, 1) == 0);
    CHECK(setenv("NEUROS_TEST_MARKER", marker, 1) == 0);
    const char *interrupt_args[] = {helper_update, k, cmd, initrd, boot, NULL};
    spawn(interrupt_args, test_log);
    double deadline = seconds() + 10;
    while (access(marker, F_OK) != 0) {
        CHECK(seconds() < deadline);
        poll_pause();
    }
    stop_child();
    lock = open(boot, O_RDONLY | O_DIRECTORY);
    CHECK(lock >= 0);
    deadline = seconds() + 5;
    while (flock(lock, LOCK_EX | LOCK_NB) != 0) {
        CHECK(errno == EWOULDBLOCK && seconds() < deadline);
        poll_pause();
    }
    CHECK(close(lock) == 0);
    equal_text(conf, old);
    run_command(args, 1);
    write_text(conf, "NEUROS1\n../escape\n-\n");
    run_command(args, 0);
    equal_text(conf, "NEUROS1\n../escape\n-\n");
    const char *legacy = fmt("%s/legacy", test_root);
    const char *legacy_args[] = {stage, k, cmd, initrd, legacy, NULL};
    run_command(legacy_args, 1);
    legacy_args[0] = update;
    run_command(legacy_args, 1);
    text = read_bytes(fmt("%s/boot.conf", legacy), &size);
    CHECK(sscanf((char *)text, "NEUROS1\n%63s\n%63s", second, previous) == 2);
    CHECK(strcmp(previous, "legacy") == 0);
    free(text);
    const char *legacy_rollback[] = {update, "--rollback", legacy, NULL};
    run_command(legacy_rollback, 1);
    equal_text(fmt("%s/boot.conf", legacy), fmt("NEUROS1\nlegacy\n%s\n", second));

    const char *source = fmt("%s/source.efi", test_root), *active = fmt("%s/active.efi", test_root);
    const char *expected = fmt("%s/expected.efi", test_root);
    const char *backup = fmt("%s.previous", active);
    const char *uki_args[] = {fmt("%s/stage-uki", build), source, active, NULL};
    uki_fixture(source, 1);
    uki_fixture(expected, 1);
    run_command(uki_args, 1);
    uki_fixture(source, 2);
    run_command(uki_args, 1);
    equal_files(active, source);
    equal_files(backup, expected);
    write_text(source, "invalid");
    run_command(uki_args, 0);
    equal_files(backup, expected);
    uki_fixture(expected, 2);
    equal_files(active, expected);
    puts("Boot updates: migration, rollback, locking, interrupted/failed writes, and UKI backup "
         "passed.");
    return(0);
}
