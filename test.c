#include "tinyparam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// 模拟日志函数（如果 aml_log.h 不存在）
#ifndef AML_LOGE
#define AML_LOGE(fmt, ...) printf("ERROR: " fmt "\n", ##__VA_ARGS__)
#endif

// 测试 JSON 文件路径
#define TEST_JSON_FILE "test.json"

// 创建测试 JSON 文件
static void create_test_json(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        AML_LOGE("Failed to create test JSON file %s\n", filename);
        return;
    }
    const char *json_content =
        "{\n"
        "    \"system\": {\n"
        "        \"audio\": {\n"
        "            \"volume\": \"50\",\n"
        "            \"mute\": \"false\"\n"
        "        },\n"
        "        \"display\": {\n"
        "            \"brightness\": \"75\"\n"
        "        }\n"
        "    }\n"
        "}";
    fwrite(json_content, 1, strlen(json_content), fp);
    fclose(fp);
}

// 测试用例：基本功能
void test_basic_operations() {
    printf("\n=== Test Basic Operations ===\n");

    // 创建测试文件
    create_test_json(TEST_JSON_FILE);

    // 测试打开文件
    tp_handle_t *handle = tp_open(TEST_JSON_FILE);
    if (!handle) {
        AML_LOGE("Failed to open %s\n", TEST_JSON_FILE);
        return;
    }

    // 测试获取值
    char *value = tp_get(handle, "system.audio.volume");
    printf("Get system.audio.volume: %s (Expected: 50)\n", value ? value : "NULL");
    if (value && strcmp(value, "50") == 0) {
        printf("PASS: Get system.audio.volume\n");
    } else {
        printf("FAIL: Get system.audio.volume\n");
    }
    free(value);

    // 测试设置值
    int ret = tp_set(handle, "system.audio.volume", "75");
    printf("Set system.audio.volume to 75: %s\n", ret == 0 ? "Success" : "Failed");
    if (ret == 0) {
        value = tp_get(handle, "system.audio.volume");
        printf("Get system.audio.volume after set: %s (Expected: 75)\n", value ? value : "NULL");
        if (value && strcmp(value, "75") == 0) {
            printf("PASS: Set and get system.audio.volume\n");
        } else {
            printf("FAIL: Set and get system.audio.volume\n");
        }
        free(value);
    }

    // 测试关闭句柄
    tp_close(handle);
    printf("PASS: Closed handle\n");
}

// 测试用例：错误情况
void test_error_cases() {
    printf("\n=== Test Error Cases ===\n");

    // 测试打开不存在的文件
    tp_handle_t *handle = tp_open("nonexistent.json");
    if (!handle) {
        printf("PASS: Failed to open nonexistent file as expected\n");
    } else {
        printf("FAIL: Opened nonexistent file\n");
        tp_close(handle);
    }

    // 创建无效 JSON 文件
    FILE *fp = fopen("invalid.json", "w");
    fwrite("invalid json content", 1, 19, fp);
    fclose(fp);

    // 测试打开无效 JSON 文件
    handle = tp_open("invalid.json");
    if (!handle) {
        printf("PASS: Failed to parse invalid JSON as expected\n");
    } else {
        printf("FAIL: Parsed invalid JSON\n");
        tp_close(handle);
    }

    // 测试获取无效键
    create_test_json(TEST_JSON_FILE);
    handle = tp_open(TEST_JSON_FILE);
    if (!handle) {
        AML_LOGE("Failed to open %s\n", TEST_JSON_FILE);
        return;
    }

    char *value = tp_get(handle, "system.invalid.key");
    if (!value) {
        printf("PASS: Failed to get nonexistent key as expected\n");
    } else {
        printf("FAIL: Got value for nonexistent key: %s\n", value);
        free(value);
    }

    // 测试设置无效键
    int ret = tp_set(handle, "system.invalid.key", "100");
    if (ret != 0) {
        printf("PASS: Failed to set nonexistent key as expected\n");
    } else {
        printf("FAIL: Set nonexistent key\n");
    }

    // 测试非法参数
    value = tp_get(NULL, "system.audio.volume");
    if (!value) {
        printf("PASS: Failed to get with NULL handle as expected\n");
    } else {
        printf("FAIL: Got value with NULL handle\n");
        free(value);
    }

    ret = tp_set(handle, NULL, "100");
    if (ret != 0) {
        printf("PASS: Failed to set with NULL key as expected\n");
    } else {
        printf("FAIL: Set with NULL key\n");
    }

    tp_close(handle);
}

// 线程任务：并发读取
void *thread_read(void *arg) {
    tp_handle_t *handle = (tp_handle_t *)arg;
    for (int i = 0; i < 10; i++) {
        char *value = tp_get(handle, "system.audio.volume");
        printf("Thread %ld read system.audio.volume: %s\n", pthread_self(), value ? value : "NULL");
        if (value) free(value);
        usleep(100000); // 模拟延迟
    }
    return NULL;
}

// 线程任务：并发写入
void *thread_write(void *arg) {
    tp_handle_t *handle = (tp_handle_t *)arg;
    char value[16];
    for (int i = 0; i < 10; i++) {
        snprintf(value, sizeof(value), "%d", 50 + i);
        int ret = tp_set(handle, "system.audio.volume", value);
        printf("Thread %ld set system.audio.volume to %s: %s\n",
               pthread_self(), value, ret == 0 ? "Success" : "Failed");
        usleep(100000); // 模拟延迟
    }
    return NULL;
}

// 测试用例：线程安全
void test_thread_safety() {
    printf("\n=== Test Thread Safety ===\n");

    create_test_json(TEST_JSON_FILE);
    tp_handle_t *handle = tp_open(TEST_JSON_FILE);
    if (!handle) {
        AML_LOGE("Failed to open %s\n", TEST_JSON_FILE);
        return;
    }

    // 创建多个线程进行并发读写
    pthread_t threads[4];
    pthread_create(&threads[0], NULL, thread_read, handle);
    pthread_create(&threads[1], NULL, thread_read, handle);
    pthread_create(&threads[2], NULL, thread_write, handle);
    pthread_create(&threads[3], NULL, thread_write, handle);

    // 等待线程结束
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    // 验证最终值
    char *value = tp_get(handle, "system.audio.volume");
    printf("Final system.audio.volume: %s\n", value ? value : "NULL");
    if (value) free(value);

    tp_close(handle);
    printf("PASS: Thread safety test completed\n");
}

int main() {
    printf("Starting TinyParam Demo Test\n");

    // 运行所有测试用例
    test_basic_operations();
    test_error_cases();
    test_thread_safety();

    // 清理测试文件
    unlink(TEST_JSON_FILE);
    unlink("invalid.json");

    printf("\nAll tests completed\n");
    return 0;
}