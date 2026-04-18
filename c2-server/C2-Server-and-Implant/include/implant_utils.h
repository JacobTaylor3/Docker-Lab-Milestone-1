#ifndef IMPLANT_UTILS_H
#define IMPLANT_UTILS_H

char *operating_system_info(void);

/*
 * BP3 — Cryptographically secure jitter.
 *
 * Returns a random sleep duration in the range [base - range, base + range]
 * seconds, drawn from BCryptGenRandom (Windows) or /dev/urandom (Linux).
 * Replaces the weak srand(time ^ pid) + rand() pattern.
 */
int secure_jitter_sec(int base_sec, int range_sec);

/* File utility functions */
char *read_file_heap_plain(const char *path, int *len_out);
char *read_file_heap(const char *path, int *len_out);
void  write_file_safe(const char *path, const char *data, int len);

#endif