#include "tinyparam.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "aml_log.h" // Assume logging functions are defined here

/**
 * @brief Open and parse a JSON file
 *
 * @param file Path to the JSON file
 * @return tp_handle_t* Handle for the opened file, or NULL on failure
 */
tp_handle_t *tp_open(char *file)
{
    tp_handle_t *handle = NULL;
    cJSON *root = NULL;
    char *buf = NULL;
    FILE *fp = NULL;

    // Check if file exists
    if (access(file, F_OK) != 0) {
        AML_LOGE("File %s does not exist\n", file);
        return NULL;
    }

    // Allocate handle
    handle = (tp_handle_t *)calloc(1, sizeof(tp_handle_t));
    if (!handle) {
        AML_LOGE("Memory allocation failed for handle\n");
        return NULL;
    }

    // Initialize mutex
    if (pthread_mutex_init(&handle->lock, NULL) != 0) {
        AML_LOGE("Mutex initialization failed\n");
        free(handle);
        return NULL;
    }

    // Save filename
    handle->filename = strdup(file);
    if (!handle->filename) {
        AML_LOGE("Memory allocation failed for filename\n");
        pthread_mutex_destroy(&handle->lock);
        free(handle);
        return NULL;
    }

    // Open file in read-write mode
    fp = fopen(file, "r+");
    if (!fp) {
        AML_LOGE("Failed to open file %s: %s\n", file, strerror(errno));
        pthread_mutex_destroy(&handle->lock);
        free(handle->filename);
        free(handle);
        return NULL;
    }
    handle->fp = fp;

    // Get file size
    struct stat st;
    if (stat(file, &st) != 0) {
        AML_LOGE("Failed to get file size for %s: %s\n", file, strerror(errno));
        goto fail;
    }
    size_t size = st.st_size;

    // Allocate buffer for file content
    buf = (char *)malloc(size + 1);
    if (!buf) {
        AML_LOGE("Memory allocation failed for buffer\n");
        goto fail;
    }

    // Read file content
    size_t n = fread(buf, 1, size, handle->fp);
    buf[size] = '\0'; // Ensure null-terminated string
    if (n != size) {
        AML_LOGE("Failed to read file %s, read %zu bytes, expected %zu\n", file, n, size);
        goto fail;
    }

    // Parse JSON content
    handle->root = cJSON_Parse(buf);
    if (!handle->root) {
        AML_LOGE("Failed to parse JSON content: %s\n", cJSON_GetErrorPtr());
        goto fail;
    }

    free(buf);
    return handle;

fail:
    if (buf) free(buf);
    if (handle) {
        if (handle->fp) fclose(handle->fp);
        if (handle->filename) free(handle->filename);
        pthread_mutex_destroy(&handle->lock);
        free(handle);
    }
    return NULL;
}

/**
 * @brief Close the handle and release resources
 *
 * @param h Handle to the JSON file
 */
void tp_close(tp_handle_t *h)
{
    if (h) {
        if (h->fp) {
            fclose(h->fp);
        }
        if (h->root) {
            cJSON_Delete(h->root);
        }
        if (h->filename) {
            free(h->filename);
        }
        pthread_mutex_destroy(&h->lock);
        free(h);
    }
}

/**
 * @brief Get a value from the JSON tree using a dotted key or single-level key
 *
 * @param h Handle to the JSON file
 * @param key Key in format "system.audio.volume" or single-level "volume"
 * @return char* Value corresponding to the key, or NULL if not found
 */
char* tp_get(tp_handle_t *h, char *key)
{
    if (!h || !key) {
        AML_LOGE("Invalid handle or key\n");
        return NULL;
    }
    if (!h->root) {
        AML_LOGE("JSON root is empty\n");
        return NULL;
    }

    // Duplicate key to avoid modifying the original
    char *str = strdup(key);
    if (!str) {
        AML_LOGE("Memory allocation failed for key\n");
        return NULL;
    }

    cJSON *cur = h->root;
    char *ptr = NULL;
    char *p = NULL;

    pthread_mutex_lock(&h->lock);

    // Check if key is single-level (no dots)
    if (strchr(str, '.') == NULL) {
        cur = cJSON_GetObjectItem(h->root, str);
        if (!cur || !cur->valuestring) {
            AML_LOGE("Single-level key not found or invalid: %s\n", str);
            free(str);
            pthread_mutex_unlock(&h->lock);
            return NULL;
        }
        char *result = strdup(cur->valuestring);
        free(str);
        pthread_mutex_unlock(&h->lock);
        if (!result) {
            AML_LOGE("Memory allocation failed for result\n");
            return NULL;
        }
        return result;
    }

    // Handle multi-level key
    ptr = strtok_r(str, ".", &p);
    if (!ptr) {
        AML_LOGE("Invalid key format: %s\n", key);
        free(str);
        pthread_mutex_unlock(&h->lock);
        return NULL;
    }

    cur = cJSON_GetObjectItem(h->root, ptr);
    while (cur && cur->type == cJSON_Object) {
        ptr = strtok_r(NULL, ".", &p);
        if (!ptr) {
            AML_LOGE("Incomplete key path: %s\n", key);
            free(str);
            pthread_mutex_unlock(&h->lock);
            return NULL;
        }
        cur = cJSON_GetObjectItem(cur, ptr);
    }

    free(str);
    if (!cur || !cur->valuestring) {
        AML_LOGE("Key not found or invalid: %s\n", key);
        pthread_mutex_unlock(&h->lock);
        return NULL;
    }

    char *result = strdup(cur->valuestring);
    pthread_mutex_unlock(&h->lock);
    if (!result) {
        AML_LOGE("Memory allocation failed for result\n");
        return NULL;
    }

    return result;
}

/**
 * @brief Set a value in the JSON tree using a dotted key or single-level key and write to file
 *
 * @param h Handle to the JSON file
 * @param key Key in format "system.audio.volume" or single-level "volume"
 * @param value Value to set
 * @return int 0 on success, -1 on failure
 */
int tp_set(tp_handle_t *h, char *key, char *value)
{
    if (!h || !key || !value || !h->filename) {
        AML_LOGE("Invalid handle, key, value, or filename\n");
        return -1;
    }
    if (!h->root) {
        AML_LOGE("JSON root is empty\n");
        return -1;
    }

    // Duplicate key to avoid modifying the original
    char *str = strdup(key);
    if (!str) {
        AML_LOGE("Memory allocation failed for key\n");
        return -1;
    }

    cJSON *cur = h->root;
    char *ptr = NULL;
    char *p = NULL;

    pthread_mutex_lock(&h->lock);

    // Handle single-level key
    if (strchr(str, '.') == NULL) {
        cur = cJSON_GetObjectItem(h->root, str);
        if (!cur) {
            AML_LOGE("Single-level key not found: %s\n", str);
            free(str);
            pthread_mutex_unlock(&h->lock);
            return -1;
        }

        // Update JSON node
        free(cur->valuestring);
        cur->valuestring = strdup(value);
        if (!cur->valuestring) {
            AML_LOGE("Memory allocation failed for value\n");
            free(str);
            pthread_mutex_unlock(&h->lock);
            return -1;
        }
    } else {
        // Handle multi-level key
        ptr = strtok_r(str, ".", &p);
        if (!ptr) {
            AML_LOGE("Invalid key format: %s\n", key);
            free(str);
            pthread_mutex_unlock(&h->lock);
            return -1;
        }

        cur = cJSON_GetObjectItem(h->root, ptr);
        while (cur && cur->type == cJSON_Object) {
            ptr = strtok_r(NULL, ".", &p);
            if (!ptr) {
                AML_LOGE("Incomplete key path: %s\n", key);
                free(str);
                pthread_mutex_unlock(&h->lock);
                return -1;
            }
            cur = cJSON_GetObjectItem(cur, ptr);
        }

        if (!cur) {
            AML_LOGE("Key not found: %s\n", key);
            free(str);
            pthread_mutex_unlock(&h->lock);
            return -1;
        }

        // Update JSON node
        free(cur->valuestring);
        cur->valuestring = strdup(value);
        if (!cur->valuestring) {
            AML_LOGE("Memory allocation failed for value\n");
            free(str);
            pthread_mutex_unlock(&h->lock);
            return -1;
        }
    }

    // Write to temporary file
    char temp_file[256];
    snprintf(temp_file, sizeof(temp_file), "%s.tmp", h->filename);
    FILE *temp_fp = fopen(temp_file, "w");
    if (!temp_fp) {
        AML_LOGE("Failed to open temporary file %s: %s\n", temp_file, strerror(errno));
        free(cur->valuestring);
        cur->valuestring = NULL;
        free(str);
        pthread_mutex_unlock(&h->lock);
        return -1;
    }

    // Print JSON to buffer and write to temporary file
    char *buf = cJSON_Print(h->root);
    if (!buf) {
        AML_LOGE("Failed to serialize JSON\n");
        fclose(temp_fp);
        free(cur->valuestring);
        cur->valuestring = NULL;
        free(str);
        pthread_mutex_unlock(&h->lock);
        return -1;
    }

    size_t len = fwrite(buf, 1, strlen(buf), temp_fp);
    if (len != strlen(buf)) {
        AML_LOGE("Failed to write to temporary file %s, wrote %zu bytes, expected %zu: %s\n",
                 temp_file, len, strlen(buf), strerror(errno));
        fclose(temp_fp);
        free(buf);
        free(cur->valuestring);
        cur->valuestring = NULL;
        free(str);
        pthread_mutex_unlock(&h->lock);
        return -1;
    }

    fclose(temp_fp);
    free(buf);

    // Replace original file with temporary file
    if (rename(temp_file, h->filename) != 0) {
        AML_LOGE("Failed to rename %s to %s: %s\n", temp_file, h->filename, strerror(errno));
        free(cur->valuestring);
        cur->valuestring = NULL;
        free(str);
        pthread_mutex_unlock(&h->lock);
        return -1;
    }

    // Reopen original file to keep handle->fp valid
    fclose(h->fp);
    h->fp = fopen(h->filename, "r+");
    if (!h->fp) {
        AML_LOGE("Failed to reopen original file %s: %s\n", h->filename, strerror(errno));
        free(cur->valuestring);
        cur->valuestring = NULL;
        free(str);
        pthread_mutex_unlock(&h->lock);
        return -1;
    }

    free(str);
    pthread_mutex_unlock(&h->lock);
    return 0;
}
