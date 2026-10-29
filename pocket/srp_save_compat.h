 

#ifndef SRP_SAVE_COMPAT_H
#define SRP_SAVE_COMPAT_H
#include <stdio.h>
#include <stddef.h>
#include "srp_save_id.h"
FILE *srp_file_open(const char *path, const char *mode);
size_t srp_file_read(void *ptr, size_t size, size_t count, FILE *stream);
size_t srp_file_write(const void *ptr, size_t size, size_t count, FILE *stream);
int srp_file_seek(FILE *stream, long offset, int whence);
long srp_file_tell(FILE *stream);
int srp_file_close(FILE *stream);
int srp_file_eof(FILE *stream);
int srp_file_getc(FILE *stream);
int srp_file_putc(int ch, FILE *stream);
int srp_file_remove(const char *path);
 

#define fopen srp_file_open
#define fread srp_file_read
#define fwrite srp_file_write
#define fseek srp_file_seek
#define ftell srp_file_tell
#define fclose srp_file_close
#define feof srp_file_eof
#define fgetc srp_file_getc
#define fputc srp_file_putc
#define remove srp_file_remove
#endif
