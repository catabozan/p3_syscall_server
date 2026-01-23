#define DISABLE_PRINT
#ifdef DISABLE_PRINT

#include <stdarg.h>
#include <stddef.h>

typedef __SIZE_TYPE__ size_t;
typedef struct _FILE FILE;

/* Override printf - properly consume variadic arguments */
int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    (void)format;
    va_end(args);  /* Critical: properly clean up variadic args */
    return 0;  /* success, printed nothing */
}

/* Override fprintf - properly consume variadic arguments */
int fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    (void)stream;
    (void)format;
    va_end(args);  /* Critical: properly clean up variadic args */
    return 0;  /* success, printed nothing */
}

/* Override vfprintf - the variadic backend used by fprintf */
int vfprintf(FILE *stream, const char *format, va_list args)
{
    (void)stream;
    (void)format;
    (void)args;
    return 0;  /* success, printed nothing */
}

/* Override snprintf - properly consume variadic arguments */
int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    (void)format;

    if (size > 0 && str != NULL) {
        str[0] = '\0';  /* valid empty C string */
    }

    va_end(args);  /* Critical: properly clean up variadic args */
    return 0;  /* success, zero chars "would have been written" */
}

/* Override perror - suppress error messages */
void perror(const char *s)
{
    (void)s;
    /* Silently do nothing */
}

/* Override fwrite - used internally by fprintf/printf */
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    (void)ptr;
    (void)stream;
    /* Return that we "wrote" everything to suppress errors */
    return size * nmemb;
}

/* Override fputs */
int fputs(const char *s, FILE *stream)
{
    (void)s;
    (void)stream;
    return 0;  /* success */
}

/* Override puts */
int puts(const char *s)
{
    (void)s;
    return 0;  /* success */
}

/* Override fputc */
int fputc(int c, FILE *stream)
{
    (void)c;
    (void)stream;
    return c;  /* return the character to indicate success */
}

/* Override putc (usually a macro, but provide function version) */
int putc(int c, FILE *stream)
{
    (void)c;
    (void)stream;
    return c;
}

/* Override putchar */
int putchar(int c)
{
    (void)c;
    return c;
}
#endif