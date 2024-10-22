#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdio.h>
#include <errno.h>
#include <string.h>

#define LOG_HEADER     "\n================ %s ================\n"
#define LOG_SUB_HEADER "\n************ %s ************\n"

extern FILE *log_fp;

#define clean_errno() (errno == 0 ? "None" : strerror(errno))

#define log_err(M, ...) {                                               \
    fprintf(stderr, "[ERROR] (%s:%d:%s: errno: %s) " M "\n", __FILE__,  \
            __LINE__, __func__, clean_errno(), ##__VA_ARGS__);          \
}

#define log_file(M, ...) {                      \
    fprintf(log_fp, "" M "\n", ##__VA_ARGS__);  \
    fflush(log_fp);                             \
}

#define check(A, M, ...) {          \
    if(!(A)) {                      \
        log_err(M, ##__VA_ARGS__);  \
        errno=0;                    \
        goto error;                 \
    }                               \
}

#define log(M, ...) {               \
    log_file (M, ##__VA_ARGS__);    \
}

#endif /* __DEBUG_H__ */
