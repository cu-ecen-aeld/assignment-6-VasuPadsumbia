#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG_DEBUG(fmt, ...) fprintf(stdout, "[DEBUG]: " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) fprintf(stderr, "[ERROR]: " fmt "\n", ##__VA_ARGS__)

int main(int argc, char *argv[]) {
    if (argc < 2) {
        LOG_ERR("No filename provided");
        fprintf(stderr, "Usage: %s <filename>\n", argv[1]);
        return EXIT_FAILURE;
    }

    FILE *file = fopen(argv[1], "w");
    if (!file) {
        LOG_ERR("Failed to open file");
        perror("Failed to open file");
        return EXIT_FAILURE;
    }

    LOG_DEBUG("Writing %s to file %s", argv[2], argv[1]);
    fprintf(file, "%s\n", argv[2]);

    if (ferror(file)) {
        LOG_ERR("Failed to write to file");
        perror("Failed to write to file");
        fclose(file);
        return EXIT_FAILURE;
    }

    fclose(file);

    return EXIT_SUCCESS;
}