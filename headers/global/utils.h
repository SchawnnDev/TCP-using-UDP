#ifndef _UTILS_H
#define _UTILS_H

/**
 * @param message
 */
noreturn void raler(char *message);
/**
 * @param string
 */
int string_to_int(char *arg);

noreturn void raler(char *message) {
    perror(message);
    exit(1);
}

int string_to_int(char *arg) {
    // variables
    char *endptr, *str;
    str = arg;

    errno = 0;
    long N = strtol(str, &endptr, 10);

    // check : error
    if ((errno == ERANGE && (N == LONG_MAX || N == LONG_MIN))
        || (errno != 0 && N == 0)) {
        raler("strtol");
    }

    // if : not found
    if (endptr == str)
        raler("string_to_int nothing found");

    // cast to int (use of signed values later)
    return (int) N;
}

#endif //_UTILS_H
