#include <stdio.h>
#include <string.h>
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_ota_ops.h"

static const char *OTA_TAG = "USER_OTA";

#define MAX_BLOCK_TIMER (3000 / portTICK_PERIOD_MS)             //最大等待时间
#define MAX_TIMEOUT_NONRESPONE  (15000 / portTICK_PERIOD_MS)    //最大超时时间
#define MAX_RECV_BUFF   1024
typedef struct
{
    int length;
    char data[MAX_RECV_BUFF];
}HttpOtaQueue;

enum{
    OTA_UPDATE_BIT = BIT0,              // OTA升级事件
    OTA_RUN_HTTP_CLIENT_BIT = BIT1,     //启动http client事件,获取升级文件
    OTA_TIMEOUT_NONRESPONE_BIT = BIT2,  //http client无数据回应超时事件
    OTA_UPDATE_SUCCESS = BIT3,          //升级成功事件
    OTA_UPDATE_FAIL = BIT4,             //升级失败事件
};

uint32_t ota_http_file_size = 0;
xQueueHandle ota_http_queue_handle = NULL;      //队列handle
EventGroupHandle_t ota_event_group_handle = NULL;//事件handle
TimerHandle_t ota_nonrespone_timeout_handle = NULL; //超时检测

bool created_ota_queue_http_task = false;

//接收无响应超时检测事件回调
static void ota_nonrespone_timeout_cb(void *arg)
{
    //置位超时检测事件
    xEventGroupSetBits(ota_event_group_handle, OTA_TIMEOUT_NONRESPONE_BIT);
}
//复位超时检测定时器
static void ota_restart_nonrespone_timer()
{
    // ESP_LOGI(OTA_TAG,"ota_restart_nonrespone_timer...");
    xTimerStop(ota_nonrespone_timeout_handle, 0);
    xTimerStart(ota_nonrespone_timeout_handle, 0);
}

esp_err_t ota_http_event_cb(esp_http_client_event_t *evt)
{
    HttpOtaQueue ota_queue_item;

    //判断事件类型
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(OTA_TAG,"HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(OTA_TAG,"HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(OTA_TAG,"HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        // ESP_LOGI(OTA_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);

        if (strstr((char *)evt->header_key, "Content-Length"))
        {
            ota_http_file_size = atoi(evt->header_value);
            ESP_LOGI(OTA_TAG,"file info %s:%d", evt->header_key, ota_http_file_size);
        }
        break;
    case HTTP_EVENT_ON_DATA:
    {
        //接收数据
        // ESP_LOGI(OTA_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        int len = 0;
        while (len < evt->data_len)
        {
            memset(&ota_queue_item, 0, sizeof(ota_queue_item));

            if ((evt->data_len - len) >= MAX_RECV_BUFF)
            {
                ota_queue_item.length = MAX_RECV_BUFF;
            }
            else
            {
                ota_queue_item.length = evt->data_len - len;
            }
            memcpy(ota_queue_item.data, evt->data + len, ota_queue_item.length);

            len += ota_queue_item.length;
            // ESP_LOGI(OTA_TAG, "\t|-in while len=%d", ota_queue_item.length);
            //发送数据到队列
            if (xQueueSend(ota_http_queue_handle, &ota_queue_item, MAX_BLOCK_TIMER) != pdPASS)
            {
                ESP_LOGE(OTA_TAG, "ota xQueueSend failed");
            }
        }
    }
    break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(OTA_TAG,"HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(OTA_TAG,"HTTP_EVENT_DISCONNECTED");
        break;
    default:
        break;
    }
    return ESP_OK;
}

int run_ota_http_client(char *url)
{
    esp_http_client_handle_t client;
    esp_err_t err;

    //URL校验
    if (strlen(url) < 5)
    {
        ESP_LOGE(OTA_TAG,"ota_url %s error!", url);
        return ESP_FAIL;
    }
    //配置http参数
    esp_http_client_config_t config = {
        .url = url,                             //请求目标文件路径
        .event_handler = ota_http_event_cb,     //http回调
        .buffer_size = (MAX_RECV_BUFF),         //设置接收缓存大小
    };
    client = esp_http_client_init(&config);
    ESP_LOGI(OTA_TAG,"Starting ota http client...");

    err = esp_http_client_perform(client);      //发起http连接

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return ESP_OK;
}

void ota_queue_http_task(void *pvParameters)
{
    esp_err_t err;
    HttpOtaQueue ota_queue_item;
    uint32_t frame_count = 0, recv_length = 0;

    created_ota_queue_http_task = true;
    ESP_LOGI(OTA_TAG,"ota_queue_http_task starting...");
    //分区相关定义
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(OTA_TAG, "Partition info configured:%d ruunig:%d",
             configured->address, running->address);
    if (configured != running) {
        ESP_LOGW(OTA_TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(OTA_TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(OTA_TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(OTA_TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);
    if (update_partition == NULL)
    {
        ESP_LOGE(OTA_TAG,"update_partition invalided...");
        xEventGroupSetBits(ota_event_group_handle, OTA_UPDATE_FAIL);
        goto OTA_QUEUE_TASK_EXIT;
    }
    //开启进入擦写flash
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(OTA_TAG,"esp_ota_begin failed, error=%d", err);
        xEventGroupSetBits(ota_event_group_handle, OTA_UPDATE_FAIL);
        goto OTA_QUEUE_TASK_EXIT;
    }


    xEventGroupSetBits(ota_event_group_handle, OTA_RUN_HTTP_CLIENT_BIT);        //设置启动http client下载固件文件
    ota_restart_nonrespone_timer();

    while(true)
    {
        //阻塞等待队列里的数据
        memset(&ota_queue_item, 0, sizeof(ota_queue_item));
        if (xQueueReceive(ota_http_queue_handle, &ota_queue_item, portMAX_DELAY))
        {
            // ESP_LOGI(OTA_TAG, "queue get data len=%d", ota_queue_item.length);
            if(ota_http_file_size <= 0 || ota_http_file_size >= (uint32_t)1800*1024)    //在分区中定义了大小为1800K
            {
                xEventGroupSetBits(ota_event_group_handle, OTA_UPDATE_FAIL);
                goto OTA_QUEUE_TASK_EXIT;
            }

            if (ota_queue_item.length > 0)
            {                
                if(frame_count % 10 == 0)
                {
                    ESP_LOGI(OTA_TAG,"File total size: (%d Byte), download(%d Byte : %d%%)", 
                    ota_http_file_size, 
                    recv_length,
                    (recv_length * 100 / ota_http_file_size)
                    );
                }
                frame_count++;

                //将数据写入到flash
                err = esp_ota_write(update_handle, (const void *)ota_queue_item.data, ota_queue_item.length);
                if (err != ESP_OK)
                {
                    ESP_LOGE(OTA_TAG, "esp_ota_write failed! error=0x%x", err);
                    xEventGroupSetBits(ota_event_group_handle, OTA_UPDATE_FAIL);
                    goto OTA_QUEUE_TASK_EXIT;
                }

                recv_length += ota_queue_item.length;
                if (recv_length >= ota_http_file_size)
                {
                    ESP_LOGI(OTA_TAG,"File total size: (%d Byte), download(%d Byte : %d%%)", 
                    ota_http_file_size, 
                    recv_length,
                    (recv_length * 100 / ota_http_file_size)
                    );

                    // 结束ota
                    if (esp_ota_end(update_handle) != ESP_OK)
                    {
                        ESP_LOGE(OTA_TAG, "esp_ota_end failed!");
                        xEventGroupSetBits(ota_event_group_handle, OTA_UPDATE_FAIL);
                        goto OTA_QUEUE_TASK_EXIT;
                    }
                    //更新OTA data区的数据成新的固件分区地址，将在下次启动时加载
                    err = esp_ota_set_boot_partition(update_partition);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(OTA_TAG, "esp_ota_set_boot_partition failed! error=0x%x", err);
                        xEventGroupSetBits(ota_event_group_handle, OTA_UPDATE_FAIL);
                        goto OTA_QUEUE_TASK_EXIT;
                    }
                    //升级成功，置位成功事件
                    xEventGroupSetBits(ota_event_group_handle, OTA_UPDATE_SUCCESS);
                    break;
                }
            }
            ota_restart_nonrespone_timer();
        }
    }

OTA_QUEUE_TASK_EXIT:
    created_ota_queue_http_task = false;
    vTaskDelete(NULL);
    return;
}

void ota_event_handle_task(void *pvParameters)
{
    EventBits_t event_bits;
    while (true)
    {
        /* Wait a maximum of 100ms for either bit 0 or bit 4 to be set within
        the event group. Clear the bits before exiting. */
        event_bits = xEventGroupWaitBits(
            ota_event_group_handle, /* The event group being tested. */
            OTA_UPDATE_BIT | OTA_RUN_HTTP_CLIENT_BIT |
                OTA_TIMEOUT_NONRESPONE_BIT | OTA_UPDATE_SUCCESS |
                OTA_UPDATE_FAIL, /* The bits within the event group to wait for. */
            pdTRUE,              /* should be cleared before returning. */
            pdFALSE,             /* Don't wait for both bits, either bit will do. */
            portMAX_DELAY);      /* Wait a maximum of 100ms for either bit to be set. */
        //创建http client获取固件文件
        if ((event_bits & OTA_RUN_HTTP_CLIENT_BIT) != 0)
        {
            ESP_LOGI(OTA_TAG,"OTA_RUN_HTTP_CLIENT_BIT get.");
            ESP_LOGI(OTA_TAG,"run_ota_http_client:%d",run_ota_http_client(CONFIG_EXAMPLE_FIRMWARE_UPG_URL));
        }
        //ota升级事件，创建新的升级任务
        if ((event_bits & OTA_UPDATE_BIT) != 0)
        {
            ESP_LOGI(OTA_TAG,"OTA_UPDATE_BIT, created_ota_queue_http_task:%d", created_ota_queue_http_task);
            if (created_ota_queue_http_task == false)
            {
                // OTA接收队列处理任务
                xTaskCreate(ota_queue_http_task, "ota_queue_http_task", (8 * 1024), NULL, 5, NULL);
            }
        }
        //http client无回应超时事件
        if((event_bits & OTA_TIMEOUT_NONRESPONE_BIT) != 0)
        {
            ESP_LOGE(OTA_TAG, "OTA_TIMEOUT_NONRESPONE_BIT envet get. \r\nSystem update fail and will restart later.");
            //升级失败，重启系统
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            esp_restart();
        }
        //升级失败事件
        if ((event_bits & OTA_UPDATE_FAIL) != 0)
        {
            ESP_LOGE(OTA_TAG, "OTA_UPDATE_FAIL envet get. \r\nSystem will restart later.");
            //升级失败，重启系统
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            esp_restart();
        }        
        //升级成功事件
        if ((event_bits & OTA_UPDATE_SUCCESS) != 0)
        {
            ESP_LOGW(OTA_TAG, "OTA_UPDATE_SUCCESS envet get. \r\nSystem will restart later.");
            //升级成功，重启系统
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            esp_restart();
        }
    }
    
    vTaskDelete(NULL);
    return;
}

int create_ota_bussiness(void)
{
    ESP_LOGI(OTA_TAG,"create_ota_bussiness running...");

    //创建接收队列
    ota_http_queue_handle = xQueueCreate(4, sizeof(HttpOtaQueue));
    if (ota_http_queue_handle == NULL)
    {
        ESP_LOGE(OTA_TAG,"ota xQueueCreate failed");
        return ESP_FAIL;
    }
    //创建业务事件
    ota_event_group_handle = xEventGroupCreate();
    if (ota_event_group_handle == NULL)
    {
        ESP_LOGE(OTA_TAG, "ota xEventGroupCreate failed");
        return ESP_FAIL;
    }
    //创建业务处理任务
    xTaskCreate(ota_event_handle_task, "ota_event_handle_task", (8 * 1024), NULL, 5, NULL);
    //创建接收无响应超时检测
    ota_nonrespone_timeout_handle = xTimerCreate("OTA_NONRESPONE_TIMENOUT", MAX_TIMEOUT_NONRESPONE, pdFALSE, NULL, ota_nonrespone_timeout_cb);
   
    return ESP_OK;
}

void test_ota_with_no_factory_partitions(void)
{
    esp_app_desc_t running_app_info;
    const esp_partition_t *running = esp_ota_get_running_partition();

    ESP_LOGI(OTA_TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK)
    {
        ESP_LOGI(OTA_TAG, "Running firmware version: %s", running_app_info.version);
    }
    if(create_ota_bussiness() == ESP_FAIL)
    {
        return;
    }

    while (true)
    {
        vTaskDelay(30000 / portTICK_PERIOD_MS);

        //发送OTA升级事件,模拟外部触发OTA升级
        xEventGroupSetBits(ota_event_group_handle, OTA_UPDATE_BIT);
    }
}



