// Ivy Meter Scheduler using esp_schedule code
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_schedule.h" //esp_scheduler library
#include "meter_control.h" //me

static const char *TAG = "SCHED";

#define MAX_SLOTS 8 // Max # of schedule slots for now

typedef struct {
    int day_mask;
    int start_hour;
    int start_min;
    int end_hour;
    int end_min;
} slot_t;

static slot_t slots[MAX_SLOTS]; // Array of schedule slots
static int slot_count = 0; // Number of loaded slots
static int meter_addr = 4; // Modbus address (not sure)

// Parse time string into hour and minute integers
static void parse_time(const char *txt, int *hour, int *min) {
    if (strlen(txt) >= 5) {
        char hh[3]; 
        char mm[3];
        hh[0] = txt[0]; 
        hh[1] = txt[1]; 
        hh[2] = '\0';
        mm[0] = txt[3]; 
        mm[1] = txt[4]; 
        mm[2] = '\0';
        *hour = atoi(hh);
        *min = atoi(mm);
    } else { 
        *hour = 0; 
        *min = 0; 
    }
}

// Load JSON config from SPIFFS
static void load_json_config(void) {
    FILE *f = fopen("/spiffs/devices.json", "r");
    if (!f) {
        ESP_LOGW(TAG, "Could not open config");
        // slot_count = 1;
        // slots[0].day_mask = 159;
        // slots[0].start_hour = 4; 
        // slots[0].start_min = 30;
        // slots[0].end_hour = 7; 
        // slots[0].end_min = 0;
        return;
    }
    // Read json file line by line
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        // Look for lines with '[' indicating a new slot
        if (strchr(line, '[')) {
            int mask = -1;
            char start_txt[16] = {0};
            char end_txt[16] = {0};

            // read day_mask
            if (sscanf(line, " [%d,", &mask) == 1) {
                char line2[128]; 
                char line3[128];

                if (fgets(line2, sizeof(line2), f) && fgets(line3, sizeof(line3), f)) {
                    char *q1 = strchr(line2, '"');
                    char *q2;
                    if (q1) {
                        q2 = strchr(q1+1, '"');
                    } else {
                        q2 = NULL;
                    }
                    
                    if (q1 && q2) { 
                        int len = q2 - q1 - 1; 
                        if (len < 15) { 
                            strncpy(start_txt, q1+1, len); 
                            start_txt[len] = '\0'; 
                        } 
                    }
                    int len = q2 - q1 - 1; 
                    if (len < 15) { 
                        strncpy(start_txt, q1+1, len); 
                        start_txt[len] = '\0'; 
                    } 
                    }
                    char *q3 = strchr(line3, '"');
                    char *q4;
                    if (q3) {
                        q4 = strchr(q3+1, '"');
                    } else {
                        q4 = NULL;
                    }
                    
                    if (q3 && q4) { 
                        int len2 = q4 - q3 - 1; 
                        if (len2 < 15) { 
                            strncpy(end_txt, q3+1, len2); 
                            end_txt[len2] = '\0'; 
                        } 
                    }
                    // If valid, store in slots array
                    if (slot_count < MAX_SLOTS && mask >= 0 && strlen(start_txt) > 0 && strlen(end_txt) > 0) {
                        parse_time(start_txt, &slots[slot_count].start_hour, &slots[slot_count].start_min);
                        parse_time(end_txt, &slots[slot_count].end_hour, &slots[slot_count].end_min);
                        slots[slot_count].day_mask = mask;
                        slot_count++;
                    }
                }
            }
        }
    }
    fclose(f);
    ESP_LOGI(TAG, "Loaded %d slots", slot_count);
}

static void init_uart(void) {

    //To do
    // initialize UART for Modbus communication
    // baud rate, parity, stop bits etc

}

// Callback for esp_schedule
static void schedule_cb(esp_schedule_handle_t handle, void *priv_data) {
    int *on_ptr = (int *)priv_data;
    if (on_ptr && *on_ptr) {
        meter_on(meter_addr);
        ESP_LOGI(TAG, "Schedule: Meter ON");
    } else {
        meter_off(meter_addr);
        ESP_LOGI(TAG, "Schedule: Meter OFF");
    }
}


void app_main(void) {
    ESP_LOGI(TAG, "Starting scheduler with esp_schedule");
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    for (int i = 0; i < 10 && sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    //init_uart();
    load_json_config();

    // Setup esp_schedule for each slot
    for (int i = 0; i < slot_count; i++) {
        static int on_val = 1;
        static int off_val = 0;
        esp_schedule_config_t sched_on = {0};
        strcpy(sched_on.name, "meter_on");
        sched_on.trigger.type = ESP_SCHEDULE_TYPE_DAYS_OF_WEEK;
        sched_on.trigger.hours = slots[i].start_hour;
        sched_on.trigger.minutes = slots[i].start_min;
        sched_on.trigger.day.repeat_days = slots[i].day_mask;
        sched_on.trigger_cb = schedule_cb;
        sched_on.priv_data = &on_val;
        esp_schedule_handle_t h_on = esp_schedule_create(&sched_on);
        esp_schedule_enable(h_on);

        esp_schedule_config_t sched_off = {0};
        strcpy(sched_off.name, "meter_off");
        sched_off.trigger.type = ESP_SCHEDULE_TYPE_DAYS_OF_WEEK;
        sched_off.trigger.hours = slots[i].end_hour;
        sched_off.trigger.minutes = slots[i].end_min;
        sched_off.trigger.day.repeat_days = slots[i].day_mask;
        sched_off.trigger_cb = schedule_cb;
        sched_off.priv_data = &off_val;
        esp_schedule_handle_t h_off = esp_schedule_create(&sched_off);
        esp_schedule_enable(h_off);
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000)); // nothing to do, schedules run in background
    }
}