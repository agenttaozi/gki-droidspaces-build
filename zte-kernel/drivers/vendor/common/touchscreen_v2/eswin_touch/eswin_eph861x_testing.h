/* Started by AICoder, pid:zb212i5b97j5bdd141c10a64608cfd3584077ff5 */
// Copyright Eswin 2024
#ifndef EPH861x_TESTING_H
#define EPH861x_TESTING_H

#include <linux/types.h>

// Function prototypes for reading and writing device attributes
ssize_t eph_devattr_test_pt01_show(struct device *dev,
                                   struct device_attribute *attr,
                                   char *buf);
ssize_t eph_devattr_test_pt01_set(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf,
                                  size_t count);
ssize_t eph_devattr_test_pt02_show(struct device *dev,
                                   struct device_attribute *attr,
                                   char *buf);
ssize_t eph_devattr_test_pt02_set(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf,
                                  size_t count);
ssize_t eph_devattr_test_pt03_show(struct device *dev,
                                   struct device_attribute *attr,
                                   char *buf);
ssize_t eph_devattr_test_pt03_set(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf,
                                  size_t count);
ssize_t eph_devattr_test_pt04_show(struct device *dev,
                                   struct device_attribute *attr,
                                   char *buf);
ssize_t eph_devattr_test_pt04_set(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf,
                                  size_t count);

/*========================================================================*/
/* Started by AICoder, pid:1a82398f0dl5fde14e28085ab0b89374ef103e84 */
#define MAX_TEST_ITEM_NUM       8

enum eph_test_type {
    RAWDATA_TEST_CODE,
    OPEN_SHORT_TEST_CODE,
    NOISE_TEST_PART1_CODE,
    NOISE_TEST_PART2_CODE,
};

#define KEY_FIELD_ITEM       "TestItem"
#define KEY_FIELD_PARMETER   "TestParameter"

#define KEY_ITEM_RAWDATA            "RAWDATA_TEST"
#define KEY_ITEM_OPEN_SHORT         "OPEN_SHORT_TEST"
#define KEY_ITEM_NOISE_TEST_PART1   "NOISE_TEST_PART1"
#define KEY_ITEM_NOISE_TEST_PART2   "NOISE_TEST_PART2"

#define KEY_PARAMETER_RAWDATA       "RAWDATA_THRESHOLD"
#define KEY_OPEN_SHORT_THRES        "OPEN_SHORT_THRESHOLD"
#define KEY_NOISE_TEST_PART1_THRES  "NOISE_PART1_THRESHOLD"
#define KEY_NOISE_TEST_PART2_THRES  "NOISE_PART2_THRESHOLD"

#define DEFAULT_RAWDATA_TEST_THRES      10000
#define DEFAULT_OPEN_SHORT_TEST_THRES   10000
#define DEFAULT_NOISE_TEST_PART1_THRES  10000
#define DEFAULT_NOISE_TEST_PART2_THRES  10000

#define EPH_KEYWORD_MAX_NUM  20
#define EPH_KEYWORD_FIELD_MAXLEN 20
#define EPH_KEYWORD_KEY_MAXLEN   30
#define EPH_KEYWORD_VAL_MAXLEN   10
#define EPH_KEYWORD_LINE_MAXLEN  50

typedef struct {
    int thres;
} T_RawdataTestVar;

typedef struct {
    int thres;
} T_OpenShortTestVar;

typedef struct {
    int thres_part1;
    int thres_part2;
} T_NoiseTestVar;

typedef struct TestItem {
    u8 code;
    u8 *data;
} T_TestItem;

typedef struct {
    char field[EPH_KEYWORD_FIELD_MAXLEN];
    char key[EPH_KEYWORD_KEY_MAXLEN];
    char val[EPH_KEYWORD_VAL_MAXLEN];
} T_KeyWord, *PT_KeyWord;

typedef struct {
    char *csv_file_buf;

    u8 testitem_num;
    T_TestItem testitem[MAX_TEST_ITEM_NUM];

    u8 keyword_num;
    T_KeyWord keyword[EPH_KEYWORD_MAX_NUM];

    T_RawdataTestVar rawdata_test_var;
    T_OpenShortTestVar open_test_var;
    T_NoiseTestVar noise_test_var;
} T_SelftestData, *PT_SelftestData;
/* Ended by AICoder, pid:1a82398f0dl5fde14e28085ab0b89374ef103e84 */

#endif // EPH861x_TESTING_H
/* Ended by AICoder, pid:zb212i5b97j5bdd141c10a64608cfd3584077ff5 */