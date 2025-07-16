
#ifndef __TINYPARAM_H__
#define __TINYPARAM_H__

#include <stdio.h>
#include <pthread.h>
#include <cjson/cJSON.h>

typedef struct tp_handle {
    FILE *fp;           // File pointer for the opened JSON file
    pthread_mutex_t lock; // Mutex to prevent concurrent writes
    cJSON *root;        // Parsed JSON tree
    char *filename;     // Path to the JSON file
} tp_handle_t;

/**
 * @brief Open a JSON file
 *
 * @param file Path to the JSON file
 * @return tp_handle_t* Handle for the opened file, or NULL on failure
 */
tp_handle_t *tp_open(char *file);

/**
 * @brief Get a parameter value
 *
 * @param h Handle to the JSON file
 * @param key Key in format "system.audio.volume"
 * @return char* Value corresponding to the key, or NULL if not found
 */
char* tp_get(tp_handle_t *h, char *key);

/**
 * @brief Set a parameter value
 *
 * @param h Handle to the JSON file
 * @param key Key in format "system.audio.volume"
 * @param value Value to set
 * @return int 0 on success, -1 on failure
 */
int tp_set(tp_handle_t *h, char *key, char *value);

/**
 * @brief Close the handle and release resources
 *
 * @param h Handle to the JSON file
 */
void tp_close(tp_handle_t *h);

#endif
