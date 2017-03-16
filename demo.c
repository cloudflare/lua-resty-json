#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#include "ljson_parser.h"

char* load_json(const char* file_path, size_t* len) {
    struct stat buf;
    if (stat(file_path, &buf)) {
        perror("stat");
        exit(1);
    }

    if (!S_ISREG(buf.st_mode)) {
        fprintf(stderr, "not regular file");
        exit(1);
    }

    size_t file_len = buf.st_size;

    int fd = open(file_path, 0);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    char *payload = malloc(file_len);
    if (payload == NULL) {
        perror("malloc");
        exit(1);
    }

    if (read(fd, payload, file_len) != file_len) {
        perror("read");
        exit(1);
    }

    close(fd);

    *len = file_len;
    return payload;
}

int
main (int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: argv[0] json-file\n");
        return 1;
    }

    size_t len;
    char* json = load_json(argv[1], &len);

    struct json_parser* jp = jp_create();
    if (!jp) {
        fprintf(stderr, "WTF\n");
        return 1;
    }

#if 1
    int i = 0;
    int ret = 0;
    for (; i < 10000; i++) {
        if (jp_parse(jp, json, len) == NULL) {
            ret = 1;
            fprintf(stderr, "parsing failed: %s\n", jp_get_err(jp));
            break;
        }
    }
#else
    obj_t* obj = jp_parse(jp, json, len);
    if (obj) {
        dump_obj(stderr, obj);
    } else {
        fprintf(stderr, "err: %s\n", jp_get_err(jp));
    }
#endif
    jp_destroy(jp);
    free(json);
    return ret;
}
