#ifndef _UTILS_H
#define _UTILS_H

#include <stdnoreturn.h>
#include <errno.h>
#include <limits.h>

/**
 * @fn      noreturn void raler(char *message)
 * @brief   Displays a message when an error has occured
 * @param   message     Message to display
 */
noreturn void raler(char *message);

/**
 * @fn      int string_to_int(char *arg)
 * @brief   Transforms a given string to an int
 * @param   arg     String to transform
 * @return  String transformed to int
 */
int string_to_int(char *arg);

/*///////////*/
/* FUNCTIONS */
/*///////////*/

noreturn void raler(char *message)
{
    perror(message);
    exit(1);
}

int string_to_int(char *arg)
{
    // variables
    char *endptr, *str;
    str = arg;

    errno = 0;
    long N = strtol(str, &endptr, 10);

    // check : error
    if ((errno == ERANGE && (N == LONG_MAX || N == LONG_MIN))
        || (errno != 0 && N == 0))
    {
        raler("strtol");
    }

    // if : not found
    if (endptr == str)
        raler("string_to_int nothing found");

    // cast to int (use of signed values later)
    return (int) N;
}

#endif //_UTILS_H
