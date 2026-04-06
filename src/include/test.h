#ifndef TEST_H
#define TEST_H

/**
 * @brief Test task that sends warning and alert level SMS once
 *
 * This task will:
 * 1. Send a WARNING level SMS
 * 2. Wait 2 seconds
 * 3. Send an ALARM level SMS
 * 4. Delete itself
 *
 * @param pvParameters Task parameters (unused)
 */
void test_sms(void* pvParameters);

#endif // TEST_H
