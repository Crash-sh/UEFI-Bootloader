#define _XOPEN_SOURCE 700
#include "support.h"

/* Test-only EDK II authenticated variable store and EFI_SIGNATURE_LIST writers.
 * Layout references are in QUICK_REF.md. Only pristine raw templates are accepted;
 * all output is confined to the disposable test directory, never host efivars. */
static const unsigned char nv_guid[16] = {0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
                                          0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50};
static const unsigned char auth_guid[16] = {0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
                                            0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92};
static const unsigned char global_guid[16] = {0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
                                              0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c};
static const unsigned char db_guid[16] = {0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
                                          0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f};
static const unsigned char x509_guid[16] = {0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
                                            0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72};
static const unsigned char enable_guid[16] = {0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
                                              0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44};
static const unsigned char custom_guid[16] = {0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
                                              0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f};

struct store {
    unsigned char *data;
    size_t size, cursor, end;
};

static int store_open(struct store *store, unsigned char *data, size_t size)
{
    for (size_t offset = 0; offset <= size && size - offset >= 64; offset += 1024) {
        if (memcmp(data + offset + 16, nv_guid, 16) != 0) {
            continue;
        }
        uint64_t volume_size = get64(data + offset + 32);
        size_t header = get16(data + offset + 48);
        if (get32(data + offset + 40) != 0x4856465f || header < 64 || volume_size > size - offset ||
            header > volume_size || volume_size - header < 28) {
            return(-1);
        }
        size_t start = offset + header;
        uint32_t length = get32(data + start + 16);
        if (memcmp(data + start, auth_guid, 16) != 0 || length < 28 ||
            length > volume_size - header || data[start + 20] != 0x5a || data[start + 21] != 0xfe ||
            (start + 28) % 4) {
            return(-1);
        }
        for (size_t i = start + 28; i < start + length; ++i) {
            if (data[i] != 0xff) {
                return(-1);
            }
        }
        *store = (struct store){data, size, start + 28, start + length};
        return(0);
    }
    return(-1);
}

static int add_variable(struct store *store, const char *name, const unsigned char guid[16],
                        uint32_t attributes, const unsigned char *payload, size_t size)
{
    size_t namesize = (strlen(name) + 1) * 2;
    if (namesize > UINT32_MAX || size > UINT32_MAX || store->cursor > store->end ||
        namesize > store->end - store->cursor || size > store->end - store->cursor - namesize ||
        store->end - store->cursor - namesize - size < 63) {
        return(-1);
    }
    size_t length = (60 + namesize + size + 3) & ~(size_t)3;
    unsigned char *p = store->data + store->cursor;
    memset(p, 0, length);
    put_le(p, 0x55aa, 2);
    p[2] = 0x3f;
    put_le(p + 4, attributes, 4);
    if (attributes & 0x20) {
        put_le(p + 16, 2020, 2);
        p[18] = 1;
        p[19] = 1;
    }
    put_le(p + 36, namesize, 4);
    put_le(p + 40, size, 4);
    memcpy(p + 44, guid, 16);
    for (size_t i = 0; name[i]; ++i) {
        p[60 + 2 * i] = (unsigned char)name[i];
    }
    memcpy(p + 60 + namesize, payload, size);
    store->cursor += length;
    return(0);
}

static unsigned char *signature_list(const char *cert, size_t *size)
{
    size_t cert_size;
    unsigned char *der = read_bytes(cert, &cert_size);
    CHECK(cert_size > 0 && cert_size < 65536);
    *size = 44 + cert_size;
    unsigned char *list = calloc(1, *size);
    CHECK(list != NULL);
    memcpy(list, x509_guid, 16);
    put_le(list + 16, *size, 4);
    put_le(list + 24, 16 + cert_size, 4);
    memcpy(list + 28, global_guid, 16); /* Disposable signature owner. */
    memcpy(list + 44, der, cert_size);
    free(der);
    return(list);
}

static void enroll(const char *template, const char *output, const char *trusted,
                   const char *revoked)
{
    size_t size, trusted_size, revoked_size = 0;
    unsigned char *data = read_bytes(template, &size);
    struct store store;
    CHECK(store_open(&store, data, size) == 0);
    unsigned char *trust = signature_list(trusted, &trusted_size);
    unsigned char *deny = revoked ? signature_list(revoked, &revoked_size) : NULL;
    unsigned char *db = malloc(trusted_size + revoked_size);
    CHECK(db != NULL);
    memcpy(db, trust, trusted_size);
    if (deny) {
        memcpy(db + trusted_size, deny, revoked_size);
    }
    CHECK(add_variable(&store, "PK", global_guid, 0x27, trust, trusted_size) == 0);
    CHECK(add_variable(&store, "KEK", global_guid, 0x27, trust, trusted_size) == 0);
    CHECK(add_variable(&store, "db", db_guid, 0x27, db, trusted_size + revoked_size) == 0);
    if (deny) {
        CHECK(add_variable(&store, "dbx", db_guid, 0x27, deny, revoked_size) == 0);
    }
    const unsigned char one = 1, zero = 0;
    CHECK(add_variable(&store, "SecureBootEnable", enable_guid, 3, &one, 1) == 0);
    CHECK(add_variable(&store, "CustomMode", custom_guid, 3, &zero, 1) == 0);
    write_bytes(output, data, size);
    free(data);
    free(trust);
    free(deny);
    free(db);
}

struct pe {
    unsigned char *data;
    size_t size, table;
    unsigned int count;
    uint64_t base, end;
    uint32_t align;
};

static int parse_pe(struct pe *pe, unsigned char *data, size_t size)
{
    if (size < 64 || memcmp(data, "MZ", 2)) {
        return(-1);
    }
    size_t header = get32(data + 60);
    if (header > size || size - header < 24 || memcmp(data + header, "PE\0\0", 4) ||
        get16(data + header + 4) != 0x8664) {
        return(-1);
    }
    unsigned int optional = get16(data + header + 20), count = get16(data + header + 6);
    if (optional < 112 || optional > size - header - 24 || get16(data + header + 24) != 0x20b) {
        return(-1);
    }
    size_t table = header + 24 + optional;
    if (!count || count > (size - table) / 40) {
        return(-1);
    }
    uint32_t align = get32(data + header + 56);
    if (!align || (align & (align - 1))) {
        return(-1);
    }
    *pe = (struct pe){data, size, table, count, get64(data + header + 48), 0, align};
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned char *s = data + table + 40 * i;
        uint32_t virtual = get32(s + 8), raw = get32(s + 16), ptr = get32(s + 20);
        if (raw && (ptr > size || raw > size - ptr)) {
            return(-1);
        }
        uint64_t end = (uint64_t)get32(s + 12) + (virtual > raw ? virtual : raw);
        if (end > UINT32_MAX) {
            return(-1);
        }
        if (end > pe->end) {
            pe->end = end;
        }
    }
    return(0);
}

static void sign_image(const char *signer, const char *input, const char *output,
                       const char *identity)
{
    const char *args[] = {signer,
                          "sign",
                          fmt("--private-key=%s/%s.key", test_root, identity),
                          fmt("--certificate=%s/%s.crt", test_root, identity),
                          fmt("--output=%s", output),
                          input,
                          NULL};
    run_command(args, 1);
}

static void make_uki(const char *stub, const char *kernel, const char *initrd, const char *output)
{
    size_t size;
    unsigned char *data = read_bytes(stub, &size);
    struct pe pe;
    CHECK(parse_pe(&pe, data, size) == 0);
    const char *cmdline = fmt("%s/cmdline", test_root), *osrel = fmt("%s/osrel", test_root);
    const char cmd[] = "console=ttyS0,115200 neuros_test=direct panic=-1";
    write_bytes(cmdline, cmd, sizeof(cmd));
    write_text(osrel, "ID=neuros-test\nNAME=\"NeurOS Test\"\n");
    const char *names[] = {".osrel", ".cmdline", ".linux", ".initrd"};
    const char *files[] = {osrel, cmdline, kernel, initrd};
    const char *args[32];
    size_t argc = 0;
    args[argc++] = "objcopy";
    uint64_t offset = pe.end;
    for (size_t i = 0; i < 4; ++i) {
        struct stat info;
        CHECK(stat(files[i], &info) == 0 && info.st_size > 0);
        offset = (offset + pe.align - 1) & ~((uint64_t)pe.align - 1);
        CHECK(offset <= UINT32_MAX && (uint64_t)info.st_size <= UINT32_MAX - offset);
        CHECK(pe.base <= UINT64_MAX - offset);
        args[argc++] = "--add-section";
        args[argc++] = fmt("%s=%s", names[i], files[i]);
        args[argc++] = "--change-section-vma";
        args[argc++] = fmt("%s=0x%llx", names[i], (unsigned long long)(pe.base + offset));
        args[argc++] = "--set-section-flags";
        args[argc++] = fmt("%s=contents,alloc,load,readonly,data", names[i]);
        offset += (uint64_t)info.st_size;
    }
    args[argc++] = stub;
    args[argc++] = output;
    args[argc] = NULL;
    run_command(args, 1);
    free(data);
}

static void tamper(const char *source, const char *destination)
{
    size_t size;
    unsigned char *data = read_bytes(source, &size);
    struct pe pe;
    CHECK(parse_pe(&pe, data, size) == 0);
    int found = 0;
    for (unsigned int i = 0; i < pe.count; ++i) {
        unsigned char *s = data + pe.table + 40 * i;
        if (memcmp(s, ".linux\0\0", 8) != 0) {
            continue;
        }
        CHECK(get32(s + 16) > 4096);
        data[(size_t)get32(s + 20) + 4096] ^= 1;
        found = 1;
    }
    CHECK(found);
    write_bytes(destination, data, size);
    free(data);
}

static void boot_case(const char *name, const char *payload, const char *variables, int trusted,
                      const char *build, const char *qemu, const char *firmware)
{
    const char *esp = fmt("%s/%s", test_root, name);
    CHECK(copy_image(fmt("%s/loader.efi", test_root), fmt("%s/EFI/BOOT/BOOTX64.EFI", esp), 1) == 0);
    CHECK(copy_image(payload, fmt("%s/EFI/Linux/arch-linux.efi", esp), 1) == 0);
    write_text(fmt("%s/EFI/NeurOS/vmlinuz", esp), "untrusted direct input");
    const char *vars = fmt("%s/%s.fd", test_root, name);
    CHECK(copy_image(variables, vars, 1) == 0);
    const char *log = fmt("%s/test-logs/%s.log", build, name);
    const char *error = fmt("%s/test-logs/%s.stderr", build, name);
    write_text(log, "");
    write_text(error, "");
    const char *args[] = {qemu,         "-accel",
                          "tcg",        "-machine",
                          "q35,smm=on", "-cpu",
                          "max",        "-m",
                          "512",        "-display",
                          "none",       "-monitor",
                          "none",       "-net",
                          "none",       "-no-reboot",
                          "-global",    "driver=cfi.pflash01,property=secure,value=on",
                          "-drive",     fmt("if=pflash,format=raw,readonly=on,file=%s", firmware),
                          "-drive",     fmt("if=pflash,format=raw,file=%s", vars),
                          "-drive",     fmt("format=raw,file=fat:rw:%s", esp),
                          "-serial",    fmt("file:%s", log),
                          NULL};
    spawn(args, error);
    double deadline = seconds() + 120;
    for (;;) {
        size_t size;
        unsigned char *data = read_bytes(log, &size);
        const char *text = (char *)data;
        const char *forbidden[] = {"Linux header:",
                                   "NEUROS: test child reached",
                                   "Kernel panic",
                                   "init verification failed",
                                   "BdsDxe: failed to load Boot",
                                   "LoadImage: Load Error"};
        for (size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
            if (strstr(text, forbidden[i])) {
                fprintf(stderr, "%s: see %s\n", name, log);
                fail(forbidden[i]);
            }
        }
        int rejected = strstr(text, "LoadImage: Security Violation") != NULL ||
                       strstr(text, "LoadImage: Access Denied") != NULL;
        int reached = strstr(text, "NEUROS: direct Linux init reached") != NULL;
        int verified = strstr(text, "NEUROS: initramfs and command line verified") != NULL;
        CHECK(trusted || !reached);
        CHECK(!trusted || (!rejected && !strstr(text, "RECOVERY")));
        free(data);
        if (trusted ? verified : rejected) {
            break;
        }
        int status;
        pid_t result = waitpid(test_child, &status, WNOHANG);
        if (result == test_child) {
            test_child = 0;
            fprintf(stderr, "%s: QEMU exited; see %s and %s\n", name, error, log);
            fail("QEMU exited before expected result");
        }
        CHECK(result == 0 || (result < 0 && errno == EINTR));
        if (seconds() >= deadline) {
            fprintf(stderr, "Timed out: %s\n", log);
            fail(name);
        }
        poll_pause();
    }
    stop_child();
    printf("%s: passed\n", name);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *build = NULL, *kernel = NULL, *qemu = "qemu-system-x86_64";
    const char *firmware = NULL, *vars = NULL, *signer = "/usr/lib/systemd/systemd-sbsign";
    const char *stub = "/usr/lib/systemd/boot/efi/linuxx64.efi.stub";
    for (int i = 1; i < argc; i += 2) {
        CHECK(i + 1 < argc);
        if (!strcmp(argv[i], "--build")) {
            build = argv[i + 1];
        } else if (!strcmp(argv[i], "--kernel")) {
            kernel = argv[i + 1];
        } else if (!strcmp(argv[i], "--qemu")) {
            qemu = argv[i + 1];
        } else if (!strcmp(argv[i], "--firmware")) {
            firmware = argv[i + 1];
        } else if (!strcmp(argv[i], "--vars")) {
            vars = argv[i + 1];
        } else if (!strcmp(argv[i], "--signer")) {
            signer = argv[i + 1];
        } else if (!strcmp(argv[i], "--stub")) {
            stub = argv[i + 1];
        } else {
            fail("Unknown argument");
        }
    }
    CHECK(build && kernel && firmware && vars);
    start_test("neuros-secureboot");
    test_log = fmt("%s/test-logs/secureboot-setup.log", build);
    write_text(test_log, "");
    const char *identities[] = {"trusted", "untrusted"};
    for (size_t i = 0; i < 2; ++i) {
        const char *key = fmt("%s/%s.key", test_root, identities[i]);
        const char *cert = fmt("%s/%s.crt", test_root, identities[i]);
        const char *der = fmt("%s/%s.der", test_root, identities[i]);
        const char *args[] = {
            "openssl", "req",     "-new",
            "-x509",   "-newkey", "rsa:2048",
            "-nodes",  "-keyout", key,
            "-out",    cert,      "-days",
            "2",       "-subj",   fmt("/CN=NeurOS disposable %s test/", identities[i]),
            NULL};
        run_command(args, 1);
        const char *convert[] = {"openssl", "x509", "-in", cert, "-outform",
                                 "DER",     "-out", der,   NULL};
        run_command(convert, 1);
    }
    const char *enrolled = fmt("%s/enrolled.fd", test_root),
               *revoked = fmt("%s/revoked.fd", test_root);
    const char *trusted_cert = fmt("%s/trusted.der", test_root);
    const char *untrusted_cert = fmt("%s/untrusted.der", test_root);
    enroll(vars, enrolled, trusted_cert, NULL);
    enroll(vars, revoked, trusted_cert, untrusted_cert);
    sign_image(signer, fmt("%s/esp/EFI/BOOT/BOOTX64.EFI", build), fmt("%s/loader.efi", test_root),
               "trusted");
    const char *untrusted = fmt("%s/untrusted.efi", test_root);
    sign_image(signer, fmt("%s/test.efi", build), untrusted, "untrusted");
    const char *unsigned_uki = fmt("%s/unsigned-uki.efi", test_root);
    const char *signed_uki = fmt("%s/uki.efi", test_root),
               *tampered = fmt("%s/tampered.efi", test_root);
    make_uki(stub, kernel, fmt("%s/test-initramfs.cpio", build), unsigned_uki);
    sign_image(signer, unsigned_uki, signed_uki, "trusted");
    tamper(signed_uki, tampered);
    boot_case("secure-trusted-uki", signed_uki, enrolled, 1, build, qemu, firmware);
    boot_case("secure-unsigned-rejected", unsigned_uki, enrolled, 0, build, qemu, firmware);
    boot_case("secure-tampered-rejected", tampered, enrolled, 0, build, qemu, firmware);
    boot_case("secure-untrusted-rejected", untrusted, enrolled, 0, build, qemu, firmware);
    boot_case("secure-revoked-rejected", untrusted, revoked, 0, build, qemu, firmware);
    return(0);
}
